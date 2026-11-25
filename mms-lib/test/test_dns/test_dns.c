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

#include "test_util.h"

#include "mms_dns.h"

#include <gutil_log.h>

#include <gio/gio.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>

#define TEST_HOST "mms.example.test"
#define TEST_SERVER_ADDRESS "192.0.2.1"
#define TEST_FALLBACK_ADDRESS "198.51.100.1"

static TestOpt test_opt;

/*==========================================================================*
 * Fake DNS server, answers every query with TEST_SERVER_ADDRESS
 *==========================================================================*/

typedef struct test_dns_server {
    int fd;
    char* address;      /* "127.0.0.1:port" */
    gint queries;
    gint stop;
    GThread* thread;
} TestDnsServer;

static
int
test_udp_socket(
    guint16* port)
{
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    g_assert_cmpint(fd, >=, 0);
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    g_assert_cmpint(bind(fd, (struct sockaddr*)&sin, sizeof(sin)), ==, 0);
    g_assert_cmpint(getsockname(fd, (struct sockaddr*)&sin, &len), ==, 0);
    *port = ntohs(sin.sin_port);
    return fd;
}

/* Returns the reply length, 0 if the query is malformed */
static
gsize
test_dns_reply(
    const guint8* query,
    gsize len,
    guint8* reply,
    gsize size)
{
    static const guint8 answer[] = {
        0xc0, 0x0c,                 /* Name: pointer to the question */
        0x00, 0x01, 0x00, 0x01,     /* Type A, class IN */
        0x00, 0x00, 0x00, 0x3c,     /* TTL 60 */
        0x00, 0x04, 192, 0, 2, 1    /* TEST_SERVER_ADDRESS */
    };
    gsize pos = 12;

    /* Skip the question name, type and class */
    while (pos < len && query[pos]) {
        pos += query[pos] + 1;
    }
    pos += 5;
    if (pos > len || pos + sizeof(answer) > size) {
        return 0;
    }

    /* Header and question, without the EDNS record */
    memcpy(reply, query, pos);
    reply[2] = 0x81;            /* QR, RD */
    reply[3] = 0x80;            /* RA, no error */
    reply[6] = 0; reply[7] = 1; /* ANCOUNT */
    reply[8] = 0; reply[9] = 0; /* NSCOUNT */
    reply[10] = 0; reply[11] = 0; /* ARCOUNT */
    memcpy(reply + pos, answer, sizeof(answer));
    return pos + sizeof(answer);
}

static
gpointer
test_dns_server_thread(
    gpointer data)
{
    TestDnsServer* server = data;
    struct pollfd pfd;

    pfd.fd = server->fd;
    pfd.events = POLLIN;
    while (!g_atomic_int_get(&server->stop)) {
        if (poll(&pfd, 1, 100) > 0) {
            guint8 query[512], reply[512];
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            gssize len = recvfrom(server->fd, query, sizeof(query), 0,
                (struct sockaddr*)&from, &fromlen);
            gsize n = (len > 12) ?
                test_dns_reply(query, len, reply, sizeof(reply)) : 0;

            if (n) {
                g_atomic_int_inc(&server->queries);
                sendto(server->fd, reply, n, 0, (struct sockaddr*)&from,
                    fromlen);
            }
        }
    }
    return NULL;
}

static
TestDnsServer*
test_dns_server_new(
    void)
{
    TestDnsServer* server = g_new0(TestDnsServer, 1);
    guint16 port;

    server->fd = test_udp_socket(&port);
    server->address = g_strdup_printf("127.0.0.1:%u", port);
    server->thread = g_thread_new("dns", test_dns_server_thread, server);
    return server;
}

static
void
test_dns_server_free(
    TestDnsServer* server)
{
    g_atomic_int_set(&server->stop, TRUE);
    g_thread_join(server->thread);
    close(server->fd);
    g_free(server->address);
    g_free(server);
}

/* Returns "127.0.0.1:port" where nothing listens */
static
char*
test_closed_port(
    void)
{
    guint16 port;
    close(test_udp_socket(&port));
    return g_strdup_printf("127.0.0.1:%u", port);
}

/*==========================================================================*
 * Fallback resolver, answers every query with TEST_FALLBACK_ADDRESS
 *==========================================================================*/

typedef GResolver TestResolver;
typedef GResolverClass TestResolverClass;
G_DEFINE_TYPE(TestResolver, test_resolver, G_TYPE_RESOLVER)

static gint test_fallback_lookups;

static
GList*
test_resolver_lookup_by_name(
    GResolver* resolver,
    const char* hostname,
    GCancellable* cancellable,
    GError** error)
{
    g_atomic_int_inc(&test_fallback_lookups);
    return g_list_append(NULL,
        g_inet_address_new_from_string(TEST_FALLBACK_ADDRESS));
}

static
void
test_resolver_init(
    TestResolver* self)
{
}

static
void
test_resolver_class_init(
    TestResolverClass* klass)
{
    klass->lookup_by_name = test_resolver_lookup_by_name;
}

/*==========================================================================*
 * Common
 *==========================================================================*/

static struct in_addr test_local_ip;

/* Returns the only address found or NULL */
static
char*
test_lookup(
    const char* host,
    GResolverNameLookupFlags flags)
{
    GResolver* resolver = g_resolver_get_default();
    GError* error = NULL;
    char* str = NULL;
    GList* list = g_resolver_lookup_by_name_with_flags(resolver, host, flags,
        NULL, &error);

    g_object_unref(resolver);
    if (list) {
        g_assert(!list->next);
        str = g_inet_address_to_string(list->data);
        g_resolver_free_addresses(list);
    } else {
        GDEBUG("%s", error->message);
        g_assert_error(error, G_RESOLVER_ERROR, G_RESOLVER_ERROR_NOT_FOUND);
        g_error_free(error);
    }
    return str;
}

static
void
test_assert_lookup(
    const char* host,
    const char* expected)
{
    char* found = test_lookup(host, G_RESOLVER_NAME_LOOKUP_FLAGS_DEFAULT);
    g_assert_cmpstr(found, ==, expected);
    g_free(found);
}

static
MMSDnsHost*
test_register(
    const char* host,
    const char* server)
{
    const char* servers[2] = { server, NULL };
    MMSDnsHost* dns = mms_dns_register(host, servers, &test_local_ip);
    g_assert(dns);
    return dns;
}

/*==========================================================================*
 * Tests
 *==========================================================================*/

static
void
test_ignored(
    void)
{
    static const char* ipv6[] = { "2001:db8::1", NULL };
    static const char* none[] = { NULL };

    g_assert(!mms_dns_register("10.0.0.1", ipv6, &test_local_ip));
    g_assert(!mms_dns_register("192.0.2.2", (const char**)none,
        &test_local_ip));
    g_assert(!mms_dns_register(TEST_HOST, ipv6, &test_local_ip));
    g_assert(!mms_dns_register(TEST_HOST, NULL, &test_local_ip));
    g_assert(!mms_dns_register(NULL, ipv6, &test_local_ip));
    g_assert(!mms_dns_register("", ipv6, &test_local_ip));
    mms_dns_unregister(NULL);
}

static
void
test_resolve(
    void)
{
    TestDnsServer* server = test_dns_server_new();
    MMSDnsHost* dns = test_register(TEST_HOST, server->address);
    const gint fallback_lookups = g_atomic_int_get(&test_fallback_lookups);

    test_assert_lookup(TEST_HOST, TEST_SERVER_ADDRESS);
    test_assert_lookup("MMS.Example.Test", TEST_SERVER_ADDRESS);
    g_assert_cmpint(g_atomic_int_get(&server->queries), ==, 2);
    g_assert_cmpint(g_atomic_int_get(&test_fallback_lookups), ==,
        fallback_lookups);

    /* Other hosts go to the fallback resolver */
    test_assert_lookup("other.example.test", TEST_FALLBACK_ADDRESS);
    g_assert_cmpint(g_atomic_int_get(&server->queries), ==, 2);

    mms_dns_unregister(dns);
    test_assert_lookup(TEST_HOST, TEST_FALLBACK_ADDRESS);
    g_assert_cmpint(g_atomic_int_get(&server->queries), ==, 2);
    test_dns_server_free(server);
}

static
void
test_failover(
    void)
{
    TestDnsServer* server = test_dns_server_new();
    char* closed = test_closed_port();
    const char* servers[] = { closed, "2001:db8::1", server->address, NULL };
    MMSDnsHost* dns = mms_dns_register(TEST_HOST, servers, &test_local_ip);

    g_assert(dns);
    test_assert_lookup(TEST_HOST, TEST_SERVER_ADDRESS);
    g_assert_cmpint(g_atomic_int_get(&server->queries), ==, 1);
    mms_dns_unregister(dns);
    test_dns_server_free(server);
    g_free(closed);
}

static
void
test_failure(
    void)
{
    char* closed = test_closed_port();
    MMSDnsHost* dns = test_register(TEST_HOST, closed);
    const gint fallback_lookups = g_atomic_int_get(&test_fallback_lookups);

    test_assert_lookup(TEST_HOST, TEST_FALLBACK_ADDRESS);
    g_assert_cmpint(g_atomic_int_get(&test_fallback_lookups), ==,
        fallback_lookups + 1);
    mms_dns_unregister(dns);
    g_free(closed);
}

static
void
test_ipv6_only(
    void)
{
    TestDnsServer* server = test_dns_server_new();
    MMSDnsHost* dns = test_register(TEST_HOST, server->address);
    const gint fallback_lookups = g_atomic_int_get(&test_fallback_lookups);

    g_assert(!test_lookup(TEST_HOST, G_RESOLVER_NAME_LOOKUP_FLAGS_IPV6_ONLY));
    g_assert_cmpint(g_atomic_int_get(&server->queries), ==, 0);
    g_assert_cmpint(g_atomic_int_get(&test_fallback_lookups), ==,
        fallback_lookups);
    mms_dns_unregister(dns);
    test_dns_server_free(server);
}

static
void
test_latest_wins(
    void)
{
    TestDnsServer* server1 = test_dns_server_new();
    TestDnsServer* server2 = test_dns_server_new();
    MMSDnsHost* dns1 = test_register(TEST_HOST, server1->address);
    MMSDnsHost* dns2 = test_register(TEST_HOST, server2->address);

    test_assert_lookup(TEST_HOST, TEST_SERVER_ADDRESS);
    g_assert_cmpint(g_atomic_int_get(&server1->queries), ==, 0);
    g_assert_cmpint(g_atomic_int_get(&server2->queries), ==, 1);

    mms_dns_unregister(dns2);
    test_assert_lookup(TEST_HOST, TEST_SERVER_ADDRESS);
    g_assert_cmpint(g_atomic_int_get(&server1->queries), ==, 1);
    g_assert_cmpint(g_atomic_int_get(&server2->queries), ==, 1);

    mms_dns_unregister(dns1);
    test_dns_server_free(server1);
    test_dns_server_free(server2);
}

static
void
test_async_done(
    GObject* resolver,
    GAsyncResult* result,
    gpointer loop)
{
    GError* error = NULL;
    GList* list = g_resolver_lookup_by_name_finish(G_RESOLVER(resolver),
        result, &error);
    char* str;

    g_assert_no_error(error);
    g_assert(list && !list->next);
    str = g_inet_address_to_string(list->data);
    g_assert_cmpstr(str, ==, TEST_SERVER_ADDRESS);
    g_free(str);
    g_resolver_free_addresses(list);
    g_main_loop_quit(loop);
}

static
void
test_async(
    void)
{
    TestDnsServer* server = test_dns_server_new();
    MMSDnsHost* dns = test_register(TEST_HOST, server->address);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GResolver* resolver = g_resolver_get_default();

    g_resolver_lookup_by_name_async(resolver, TEST_HOST, NULL,
        test_async_done, loop);
    test_run_loop(&test_opt, loop);
    g_assert_cmpint(g_atomic_int_get(&server->queries), ==, 1);
    g_object_unref(resolver);
    g_main_loop_unref(loop);
    mms_dns_unregister(dns);
    test_dns_server_free(server);
}

#define TEST_(x) "/Dns/" x

int main(int argc, char* argv[])
{
    GResolver* fallback;

    g_test_init(&argc, &argv, NULL);
    test_init(&test_opt, &argc, argv);
    test_local_ip.s_addr = htonl(INADDR_LOOPBACK);

    /* mms_dns picks the default resolver as its fallback */
    fallback = g_object_new(test_resolver_get_type(), NULL);
    g_resolver_set_default(fallback);
    g_object_unref(fallback);

    g_test_add_func(TEST_("Ignored"), test_ignored);
    g_test_add_func(TEST_("Resolve"), test_resolve);
    g_test_add_func(TEST_("Failover"), test_failover);
    g_test_add_func(TEST_("Failure"), test_failure);
    g_test_add_func(TEST_("IPv6Only"), test_ipv6_only);
    g_test_add_func(TEST_("LatestWins"), test_latest_wins);
    g_test_add_func(TEST_("Async"), test_async);
    return g_test_run();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
