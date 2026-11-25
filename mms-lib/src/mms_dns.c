/*
 * Copyright (C) 2026 Jolla Mobile Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "mms_dns.h"

#include <gio/gio.h>
#include <ares.h>
#include <arpa/inet.h>
#include <string.h>

/* Logging */
#define GLOG_MODULE_NAME mms_dns_log
#include "mms_lib_log.h"
#include <gutil_log.h>
GLOG_MODULE_DEFINE("mms-dns");

#define MMS_DNS_TIMEOUT_MS (2000)
#define MMS_DNS_TRIES (2)

struct mms_dns_host {
    char* name;
    char* servers;              /* Comma separated, for c-ares */
    unsigned int local_ip;      /* Host byte order, for c-ares */
};

typedef struct mms_dns_lookup {
    char* hostname;
    GResolverNameLookupFlags flags;
} MMSDnsLookup;

typedef struct mms_dns_result {
    int status;
    GList* addresses;
} MMSDnsResult;

/*
 * Lookups run in GLib worker threads. They look up the registered hosts
 * under the lock and work on a copy.
 */
typedef struct mms_dns_resolver {
    GResolver parent;
    GResolver* fallback;
} MMSDnsResolver;

typedef GResolverClass MMSDnsResolverClass;
G_DEFINE_TYPE(MMSDnsResolver, mms_dns_resolver, G_TYPE_RESOLVER)
#define MMS_TYPE_DNS_RESOLVER (mms_dns_resolver_get_type())
#define MMS_DNS_RESOLVER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
   MMS_TYPE_DNS_RESOLVER, MMSDnsResolver))
#define FALLBACK(r) (MMS_DNS_RESOLVER(r)->fallback)
#define FALLBACK_CLASS(r) G_RESOLVER_GET_CLASS(FALLBACK(r))

static GMutex mms_dns_lock;
static GSList* mms_dns_hosts;   /* Newest first */

static
MMSDnsHost*
mms_dns_host_new(
    const char* name,
    const char* servers,
    unsigned int local_ip)
{
    MMSDnsHost* host = g_new(MMSDnsHost, 1);
    host->name = g_strdup(name);
    host->servers = g_strdup(servers);
    host->local_ip = local_ip;
    return host;
}

static
void
mms_dns_host_free(
    MMSDnsHost* host)
{
    g_free(host->name);
    g_free(host->servers);
    g_free(host);
}

static
MMSDnsHost*
mms_dns_host_find(
    const char* name)
{
    MMSDnsHost* found = NULL;
    GSList* l;
    g_mutex_lock(&mms_dns_lock);
    for (l = mms_dns_hosts; l && !found; l = l->next) {
        const MMSDnsHost* host = l->data;
        if (!g_ascii_strcasecmp(host->name, name)) {
            found = mms_dns_host_new(host->name, host->servers,
                host->local_ip);
        }
    }
    g_mutex_unlock(&mms_dns_lock);
    return found;
}

static
void
mms_dns_free_addresses(
    gpointer list)
{
    g_list_free_full(list, g_object_unref);
}

static
void
mms_dns_ares_done(
    void* arg,
    int status,
    int timeouts,
    struct ares_addrinfo* res)
{
    MMSDnsResult* result = arg;
    result->status = status;
    if (res) {
        const struct ares_addrinfo_node* node;
        for (node = res->nodes; node; node = node->ai_next) {
            if (node->ai_family == AF_INET) {
                const struct sockaddr_in* sin = (void*)node->ai_addr;
                result->addresses = g_list_append(result->addresses,
                    g_inet_address_new_from_bytes((const guint8*)
                    &sin->sin_addr, G_SOCKET_FAMILY_IPV4));
            }
        }
        ares_freeaddrinfo(res);
    }
}

static
GList*
mms_dns_ares_lookup(
    const MMSDnsHost* host,
    GError** error)
{
    struct ares_options opts;
    ares_channel_t* channel = NULL;
    MMSDnsResult result = { ARES_ENOTINITIALIZED, NULL };
    int status;

    memset(&opts, 0, sizeof(opts));
    opts.flags = ARES_FLAG_NOSEARCH;
    opts.lookups = (char*)"b";
    opts.timeout = MMS_DNS_TIMEOUT_MS;
    opts.tries = MMS_DNS_TRIES;
    opts.qcache_max_ttl = 0;
    opts.evsys = ARES_EVSYS_DEFAULT;
    status = ares_init_options(&channel, &opts, ARES_OPT_FLAGS |
        ARES_OPT_LOOKUPS | ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES |
        ARES_OPT_QUERY_CACHE | ARES_OPT_EVENT_THREAD);
    if (status == ARES_SUCCESS) {
        status = ares_set_servers_csv(channel, host->servers);
    }
    if (status == ARES_SUCCESS) {
        struct ares_addrinfo_hints hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        ares_set_local_ip4(channel, host->local_ip);
        ares_getaddrinfo(channel, host->name, NULL, &hints,
            mms_dns_ares_done, &result);
        ares_queue_wait_empty(channel, -1);
        status = result.status;
    }
    if (channel) {
        ares_destroy(channel);
    }
    if (status == ARES_SUCCESS && result.addresses) {
        return result.addresses;
    }
    mms_dns_free_addresses(result.addresses);
    g_set_error(error, G_RESOLVER_ERROR, G_RESOLVER_ERROR_NOT_FOUND,
        "%s via %s: %s", host->name, host->servers, ares_strerror(status));
    return NULL;
}

static
GList*
mms_dns_resolver_lookup(
    GResolver* resolver,
    const char* hostname,
    GResolverNameLookupFlags flags,
    GCancellable* cancellable,
    GError** error)
{
    MMSDnsHost* host = mms_dns_host_find(hostname);
    if (host) {
        GList* list = NULL;
        GError* err = NULL;
        if (flags & G_RESOLVER_NAME_LOOKUP_FLAGS_IPV6_ONLY) {
            /* GLib still uses the IPv4 answer */
            g_set_error(error, G_RESOLVER_ERROR, G_RESOLVER_ERROR_NOT_FOUND,
                "%s: no IPv6 lookups over the MMS connection", hostname);
            mms_dns_host_free(host);
            return NULL;
        }
        list = mms_dns_ares_lookup(host, &err);
        mms_dns_host_free(host);
        if (list) {
            GDEBUG("Resolved %s over the MMS connection", hostname);
            return list;
        }
        GWARN("%s, trying the system resolver", err->message);
        g_error_free(err);
    }
    return g_resolver_lookup_by_name_with_flags(FALLBACK(resolver), hostname,
        flags, cancellable, error);
}

static
void
mms_dns_lookup_free(
    gpointer data)
{
    MMSDnsLookup* lookup = data;
    g_free(lookup->hostname);
    g_free(lookup);
}

static
void
mms_dns_resolver_lookup_thread(
    GTask* task,
    gpointer resolver,
    gpointer data,
    GCancellable* cancellable)
{
    const MMSDnsLookup* lookup = data;
    GError* error = NULL;
    GList* list = mms_dns_resolver_lookup(resolver, lookup->hostname,
        lookup->flags, cancellable, &error);
    if (list) {
        g_task_return_pointer(task, list, mms_dns_free_addresses);
    } else {
        g_task_return_error(task, error);
    }
}

/*==========================================================================*
 * GResolver
 *
 * Name lookups go through mms_dns_resolver_lookup(), everything else is
 * passed to the fallback (the previous default) resolver.
 *==========================================================================*/

static
GList*
mms_dns_resolver_lookup_by_name(
    GResolver* resolver,
    const char* hostname,
    GCancellable* cancellable,
    GError** error)
{
    return mms_dns_resolver_lookup(resolver, hostname,
        G_RESOLVER_NAME_LOOKUP_FLAGS_DEFAULT, cancellable, error);
}

static
void
mms_dns_resolver_lookup_by_name_with_flags_async(
    GResolver* resolver,
    const char* hostname,
    GResolverNameLookupFlags flags,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
    GTask* task = g_task_new(resolver, cancellable, callback, user_data);
    MMSDnsLookup* lookup = g_new(MMSDnsLookup, 1);
    lookup->hostname = g_strdup(hostname);
    lookup->flags = flags;
    g_task_set_task_data(task, lookup, mms_dns_lookup_free);
    g_task_run_in_thread(task, mms_dns_resolver_lookup_thread);
    g_object_unref(task);
}

static
void
mms_dns_resolver_lookup_by_name_async(
    GResolver* resolver,
    const char* hostname,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
    mms_dns_resolver_lookup_by_name_with_flags_async(resolver, hostname,
        G_RESOLVER_NAME_LOOKUP_FLAGS_DEFAULT, cancellable, callback,
        user_data);
}

static
GList*
mms_dns_resolver_lookup_by_name_finish(
    GResolver* resolver,
    GAsyncResult* result,
    GError** error)
{
    g_return_val_if_fail(g_task_is_valid(result, resolver), NULL);
    return g_task_propagate_pointer(G_TASK(result), error);
}

static
char*
mms_dns_resolver_lookup_by_address(
    GResolver* resolver,
    GInetAddress* address,
    GCancellable* cancellable,
    GError** error)
{
    return FALLBACK_CLASS(resolver)->lookup_by_address(FALLBACK(resolver),
        address, cancellable, error);
}

static
void
mms_dns_resolver_lookup_by_address_async(
    GResolver* resolver,
    GInetAddress* address,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
    FALLBACK_CLASS(resolver)->lookup_by_address_async(FALLBACK(resolver),
        address, cancellable, callback, user_data);
}

static
char*
mms_dns_resolver_lookup_by_address_finish(
    GResolver* resolver,
    GAsyncResult* result,
    GError** error)
{
    return FALLBACK_CLASS(resolver)->lookup_by_address_finish
        (FALLBACK(resolver), result, error);
}

static
GList*
mms_dns_resolver_lookup_service(
    GResolver* resolver,
    const char* rrname,
    GCancellable* cancellable,
    GError** error)
{
    return FALLBACK_CLASS(resolver)->lookup_service(FALLBACK(resolver),
        rrname, cancellable, error);
}

static
void
mms_dns_resolver_lookup_service_async(
    GResolver* resolver,
    const char* rrname,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
    FALLBACK_CLASS(resolver)->lookup_service_async(FALLBACK(resolver),
        rrname, cancellable, callback, user_data);
}

static
GList*
mms_dns_resolver_lookup_service_finish(
    GResolver* resolver,
    GAsyncResult* result,
    GError** error)
{
    return FALLBACK_CLASS(resolver)->lookup_service_finish
        (FALLBACK(resolver), result, error);
}

static
GList*
mms_dns_resolver_lookup_records(
    GResolver* resolver,
    const char* rrname,
    GResolverRecordType type,
    GCancellable* cancellable,
    GError** error)
{
    return FALLBACK_CLASS(resolver)->lookup_records(FALLBACK(resolver),
        rrname, type, cancellable, error);
}

static
void
mms_dns_resolver_lookup_records_async(
    GResolver* resolver,
    const char* rrname,
    GResolverRecordType type,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
    FALLBACK_CLASS(resolver)->lookup_records_async(FALLBACK(resolver),
        rrname, type, cancellable, callback, user_data);
}

static
GList*
mms_dns_resolver_lookup_records_finish(
    GResolver* resolver,
    GAsyncResult* result,
    GError** error)
{
    return FALLBACK_CLASS(resolver)->lookup_records_finish
        (FALLBACK(resolver), result, error);
}

static
void
mms_dns_resolver_init(
    MMSDnsResolver* self)
{
    self->fallback = g_resolver_get_default();
}

static
void
mms_dns_resolver_finalize(
    GObject* object)
{
    g_object_unref(MMS_DNS_RESOLVER(object)->fallback);
    G_OBJECT_CLASS(mms_dns_resolver_parent_class)->finalize(object);
}

static
void
mms_dns_resolver_class_init(
    MMSDnsResolverClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = mms_dns_resolver_finalize;
    klass->lookup_by_name = mms_dns_resolver_lookup_by_name;
    klass->lookup_by_name_async = mms_dns_resolver_lookup_by_name_async;
    klass->lookup_by_name_finish = mms_dns_resolver_lookup_by_name_finish;
    klass->lookup_by_name_with_flags = mms_dns_resolver_lookup;
    klass->lookup_by_name_with_flags_async =
        mms_dns_resolver_lookup_by_name_with_flags_async;
    klass->lookup_by_name_with_flags_finish =
        mms_dns_resolver_lookup_by_name_finish;
    klass->lookup_by_address = mms_dns_resolver_lookup_by_address;
    klass->lookup_by_address_async = mms_dns_resolver_lookup_by_address_async;
    klass->lookup_by_address_finish =
        mms_dns_resolver_lookup_by_address_finish;
    klass->lookup_service = mms_dns_resolver_lookup_service;
    klass->lookup_service_async = mms_dns_resolver_lookup_service_async;
    klass->lookup_service_finish = mms_dns_resolver_lookup_service_finish;
    klass->lookup_records = mms_dns_resolver_lookup_records;
    klass->lookup_records_async = mms_dns_resolver_lookup_records_async;
    klass->lookup_records_finish = mms_dns_resolver_lookup_records_finish;
}

/*==========================================================================*
 * API
 *==========================================================================*/

static
gpointer
mms_dns_install(
    gpointer unused)
{
    int status;

    if (!ares_threadsafety()) {
        GERR("c-ares is built without thread support");
        return GINT_TO_POINTER(FALSE);
    }
    status = ares_library_init(ARES_LIB_INIT_ALL);
    if (status != ARES_SUCCESS) {
        GERR("Failed to initialize c-ares: %s", ares_strerror(status));
        return GINT_TO_POINTER(FALSE);
    } else {
        /* Stays the default resolver until exit */
        GResolver* resolver = g_object_new(MMS_TYPE_DNS_RESOLVER, NULL);
        g_resolver_set_default(resolver);
        g_object_unref(resolver);
        return GINT_TO_POINTER(TRUE);
    }
}

static
char*
mms_dns_ipv4_servers(
    const char* const* servers)
{
    GString* csv = g_string_new(NULL);
    const char* const* ptr;
    for (ptr = servers; ptr && *ptr; ptr++) {
        const char* port = strchr(*ptr, ':');
        char* ip = port ? g_strndup(*ptr, port - *ptr) : g_strdup(*ptr);
        struct in_addr addr;
        if (inet_pton(AF_INET, ip, &addr) == 1) {
            if (csv->len) g_string_append_c(csv, ',');
            g_string_append(csv, *ptr);
        }
        g_free(ip);
    }
    return g_string_free(csv, !csv->len);
}

MMSDnsHost*
mms_dns_register(
    const char* name,
    const char* const* servers,
    const struct in_addr* local_ip)
{
    static GOnce install_once = G_ONCE_INIT;
    MMSDnsHost* host;
    char* csv;

    if (!name || !name[0] || g_hostname_is_ip_address(name)) {
        return NULL;
    }
    csv = mms_dns_ipv4_servers(servers);
    if (!csv) {
        GDEBUG("No IPv4 DNS servers for %s", name);
        return NULL;
    }
    if (!GPOINTER_TO_INT(g_once(&install_once, mms_dns_install, NULL))) {
        g_free(csv);
        return NULL;
    }
    host = mms_dns_host_new(name, csv, ntohl(local_ip->s_addr));
    g_free(csv);
    GDEBUG("Resolving %s via %s", host->name, host->servers);
    g_mutex_lock(&mms_dns_lock);
    mms_dns_hosts = g_slist_prepend(mms_dns_hosts, host);
    g_mutex_unlock(&mms_dns_lock);
    return host;
}

void
mms_dns_unregister(
    MMSDnsHost* host)
{
    if (host) {
        g_mutex_lock(&mms_dns_lock);
        mms_dns_hosts = g_slist_remove(mms_dns_hosts, host);
        g_mutex_unlock(&mms_dns_lock);
        mms_dns_host_free(host);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
