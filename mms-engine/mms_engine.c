/*
 * Copyright (C) 2013-2020 Jolla Ltd.
 * Copyright (C) 2013-2020 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2019-2020 Open Mobile Platform LLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "mms_engine.h"
#include "mms_log.h"

#include "mms_attachment_info.h"
#include "mms_dispatcher.h"
#include "mms_settings.h"
#include "mms_lib_util.h"
#include "mms_handler_dbus.h"
#include "mms_settings_dconf.h"
#include "mms_transfer_list_dbus.h"

#ifdef SAILFISH
#  include "mms_connman_nemo.h"
#  define mms_connman_new() mms_connman_nemo_new()
#else
#  include "mms_connman_ofono.h"
#  define mms_connman_new() mms_connman_ofono_new()
#endif

/* Generated code */
#include "org.nemomobile.MmsEngine.h"

#include <dbuslog_util.h>

#include <dbusaccess_peer.h>
#include <dbusaccess_policy.h>

#include <gutil_macros.h>
#include <gutil_misc.h>
#include <gutil_log.h>

#include <gio/gunixfdlist.h>

/* D-Bus proxy Signals */
enum mms_engine_dbus_methods {
    #define MMS_ENGINE_METHOD_(ID,id,name) MMS_ENGINE_METHOD_##ID,
    MMS_ENGINE_DBUS_METHODS(MMS_ENGINE_METHOD_)
    #undef MMS_ENGINE_METHOD_
    MMS_ENGINE_METHOD_COUNT
};

struct mms_engine {
    GObject parent;
    const MMSConfig* config;
    MMSConnMan* cm;
    MMSSettings* settings;
    MMSDispatcher* dispatcher;
    MMSDispatcherDelegate dispatcher_delegate;
    MMSLog* log;
    DAPolicy* dbus_access;
    DA_BUS da_bus;
    GDBusConnection* engine_bus;
    OrgNemomobileMmsEngine* proxy;
    GMainLoop* loop;
    gboolean stopped;
    gboolean stop_requested;
    gboolean keep_running;
    gboolean disable_dbus_log;
    guint idle_timer_id;
    gulong proxy_signal_id[MMS_ENGINE_METHOD_COUNT];
};

typedef GObjectClass MMSEngineClass;
G_DEFINE_TYPE(MMSEngine, mms_engine, G_TYPE_OBJECT)
#define MMS_TYPE_ENGINE (mms_engine_get_type())
#define MMS_ENGINE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
        MMS_TYPE_ENGINE, MMSEngine))

inline static MMSEngine*
mms_engine_from_dispatcher_delegate(MMSDispatcherDelegate* delegate)
    { return G_CAST(delegate,MMSEngine,dispatcher_delegate); }

static
gboolean
mms_engine_stop_callback(
    gpointer data)
{
    MMSEngine* engine = data;
    engine->stopped = TRUE;
    if (engine->loop) g_main_loop_quit(engine->loop);
    mms_engine_unref(engine);
    return G_SOURCE_REMOVE;
}

static
void
mms_engine_stop_schedule(
    MMSEngine* engine)
{
    g_idle_add(mms_engine_stop_callback, mms_engine_ref(engine));
}

static
gboolean
mms_engine_idle_timer_expired(
    gpointer data)
{
    MMSEngine* engine = data;
    GASSERT(engine->idle_timer_id);
    GINFO("Shutting down due to inactivity...");
    engine->idle_timer_id = 0;
    mms_engine_stop_schedule(engine);
    return G_SOURCE_REMOVE;
}

static
void
mms_engine_idle_timer_stop(
    MMSEngine* engine)
{
    if (engine->idle_timer_id) {
        g_source_remove(engine->idle_timer_id);
        engine->idle_timer_id = 0;
    }
}

static
void
mms_engine_idle_timer_check(
    MMSEngine* engine)
{
    mms_engine_idle_timer_stop(engine);
    if (!mms_dispatcher_is_started(engine->dispatcher) && !engine->keep_running) {
       engine->idle_timer_id = g_timeout_add_seconds(engine->config->idle_secs,
           mms_engine_idle_timer_expired, engine);
    }
}

static
GArray*
mms_engine_new_attachment_array()
{
    GArray* ais = g_array_sized_new(FALSE, TRUE,
        sizeof(MMSAttachmentInfo), 0);

    g_array_set_clear_func(ais, (GDestroyNotify)
        mms_attachment_info_cleanup);
    return ais;
}

static
gboolean
mms_engine_dbus_access_allowed(
    MMSEngine* engine,
    GDBusMethodInvocation* call,
    MMS_ENGINE_ACTION action)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);
    DAPeer* peer = da_peer_get(engine->da_bus, sender);

    if (peer && da_policy_check(engine->dbus_access, &peer->cred, action, 0,
        DA_ACCESS_ALLOW) == DA_ACCESS_ALLOW) {
        return TRUE;
    } else {
        const char* iface = g_dbus_method_invocation_get_interface_name(call);
        const char* method = g_dbus_method_invocation_get_method_name(call);

        GWARN("Client %s is not allowed to call %s.%s", sender, iface, method);
        g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
            G_DBUS_ERROR_FAILED, "Client %s is not allowed to call %s.%s",
            sender, iface, method);
        return FALSE;
    }
}

/* org.nemomobile.MmsEngine.sendMessage */
static
gboolean
mms_engine_handle_send_message(
    OrgNemomobileMmsEngine* proxy,
    GDBusMethodInvocation* call,
    int database_id,
    const char* imsi_to,
    const char* const* to,
    const char* const* cc,
    const char* const* bcc,
    const char* subject,
    guint flags,
    GVariant* attachments,
    MMSEngine* engine)
{
    if (!mms_engine_dbus_access_allowed(engine, call,
        MMS_ENGINE_ACTION_SEND_MESSAGE)) {
        /* mms_engine_dbus_access_allowed has completed the call */
    } else if (to && *to) {
        GPtrArray* strings = g_ptr_array_new_full(3, g_free);
        GArray* ais = mms_engine_new_attachment_array();
        GError* error = NULL;

        /* Extract attachment info */
        char* fn = NULL;
        char* ct = NULL;
        char* cid = NULL;
        GVariantIter* it = NULL;

        g_variant_get(attachments, "a(sss)", &it);
        while (g_variant_iter_loop(it, "(sss)", &fn, &ct, &cid)) {
            MMSAttachmentInfo ai;

            g_ptr_array_add(strings, fn);
            g_ptr_array_add(strings, ct);
            g_ptr_array_add(strings, cid);
            if (mms_attachment_info_path(&ai, fn, ct, cid, &error)) {
                /*
                 * Need to zero these pointers to stop the next
                 * g_variant_iter_loop() call from deallocating
                 * the data allocated by the previous loop.
                 */
                fn = ct = cid = NULL;
                g_array_append_vals(ais, &ai, 1);
            } else {
                GERR("%s", GERRMSG(error));
                break;
            }
        }

        if (error) {
            g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
                G_DBUS_ERROR_FAILED, "%s", GERRMSG(error));
            g_error_free(error);
        } else {
            MMSAttachmentInfo* parts = (MMSAttachmentInfo*)ais->data;
            char* to_list = g_strjoinv(",", (char**)to);
            char* cc_list = NULL;
            char* bcc_list = NULL;
            char* id = NULL;
            char* imsi;

            /* Convert address lists into comma-separated strings
             * expected by mms_dispatcher_send_message and mms_codec */
            if (cc && *cc) cc_list = g_strjoinv(",", (char**)cc);
            if (bcc && *bcc) bcc_list = g_strjoinv(",", (char**)bcc);
            if (database_id > 0) id = g_strdup_printf("%u", database_id);

            /* Queue the message */
            imsi = mms_dispatcher_send_message(engine->dispatcher, id,
                imsi_to, to_list, cc_list, bcc_list, subject, flags, parts,
                ais->len, &error);
            if (imsi) {
                mms_dispatcher_start(engine->dispatcher);
                org_nemomobile_mms_engine_complete_send_message(proxy, call,
                    imsi);
                g_free(imsi);
            } else {
                g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
                    G_DBUS_ERROR_FAILED, "%s", GERRMSG(error));
                g_error_free(error);
            }

            g_free(to_list);
            g_free(cc_list);
            g_free(bcc_list);
            g_free(id);
        }

        g_variant_iter_free(it);
        g_array_unref(ais);
        g_ptr_array_free(strings, TRUE);
    } else {
        g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
            G_DBUS_ERROR_FAILED, "Missing recipient");
    }
    mms_engine_idle_timer_check(engine);
    return TRUE;
}

/* org.nemomobile.MmsEngine.sendMessageFd */
static
gboolean
mms_engine_handle_send_message_fd(
    OrgNemomobileMmsEngine* proxy,
    GDBusMethodInvocation* call,
    GUnixFDList* fdl,
    int database_id,
    const char* imsi_to,
    const char* const* to,
    const char* const* cc,
    const char* const* bcc,
    const char* subject,
    guint flags,
    GVariant* attachments,
    MMSEngine* engine)
{
    if (!mms_engine_dbus_access_allowed(engine, call,
        MMS_ENGINE_ACTION_SEND_MESSAGE)) {
        /* mms_engine_dbus_access_allowed has completed the call */
    } else if (to && *to) {
        gint nfds = 0;
        const gint* fds = g_unix_fd_list_peek_fds(fdl, &nfds);
        GPtrArray* strings = g_ptr_array_new_full(3, g_free);
        GArray* ais = mms_engine_new_attachment_array();
        GError* error = NULL;

        /* Extract attachment info */
        gint32 fdi;
        char* fn = NULL;
        char* ct = NULL;
        char* cid = NULL;
        GVariantIter* it = NULL;

        g_variant_get(attachments, "a(hsss)", &it);
        while (g_variant_iter_loop(it, "(hsss)", &fdi, &fn, &ct, &cid)) {
            MMSAttachmentInfo ai;

            g_ptr_array_add(strings, fn);
            g_ptr_array_add(strings, ct);
            g_ptr_array_add(strings, cid);
            if (fdi >= 0 && fdi < nfds /* validate the index */ &&
                mms_attachment_info_fd(&ai, fds[fdi], fn, ct, cid, &error)) {
                /*
                 * Need to zero these pointers to stop the next
                 * g_variant_iter_loop() call from deallocating
                 * the data allocated by the previous loop.
                 */
                fn = ct = cid = NULL;
                g_array_append_vals(ais, &ai, 1);
            } else {
                GERR("%s", GERRMSG(error));
                break;
            }
        }

        if (error) {
            g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
                G_DBUS_ERROR_FAILED, "%s", GERRMSG(error));
            g_error_free(error);
        } else {
            MMSAttachmentInfo* parts = (MMSAttachmentInfo*)ais->data;
            char* to_list = g_strjoinv(",", (char**)to);
            char* cc_list = NULL;
            char* bcc_list = NULL;
            char* id = NULL;
            char* imsi;

            /* Convert address lists into comma-separated strings
             * expected by mms_dispatcher_send_message and mms_codec */
            if (cc && *cc) cc_list = g_strjoinv(",", (char**)cc);
            if (bcc && *bcc) bcc_list = g_strjoinv(",", (char**)bcc);
            if (database_id > 0) id = g_strdup_printf("%u", database_id);

            /* Queue the message */
            imsi = mms_dispatcher_send_message(engine->dispatcher, id,
                imsi_to, to_list, cc_list, bcc_list, subject, flags, parts,
                ais->len, &error);
            if (imsi) {
                mms_dispatcher_start(engine->dispatcher);
                org_nemomobile_mms_engine_complete_send_message_fd(proxy,
                    call, fdl, imsi);
                g_free(imsi);
            } else {
                g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
                    G_DBUS_ERROR_FAILED, "%s", GERRMSG(error));
                g_error_free(error);
            }

            g_free(to_list);
            g_free(cc_list);
            g_free(bcc_list);
            g_free(id);
        }

        g_variant_iter_free(it);
        g_array_unref(ais);
        g_ptr_array_free(strings, TRUE);
    } else {
        g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
            G_DBUS_ERROR_FAILED, "Missing recipient");
    }
    mms_engine_idle_timer_check(engine);
    return TRUE;
}

/* org.nemomobile.MmsEngine.receiveMessage */
static
gboolean
mms_engine_handle_receive_message(
    OrgNemomobileMmsEngine* proxy,
    GDBusMethodInvocation* call,
    int database_id,
    const char* imsi,
    gboolean automatic,
    GVariant* data,
    MMSEngine* engine)
{
    /* mms_engine_dbus_access_allowed completes the call if access is denied */
    if (mms_engine_dbus_access_allowed(engine, call,
        MMS_ENGINE_ACTION_RECEIVE_MESSAGE)) {
        gsize len = 0;
        const guint8* bytes = g_variant_get_fixed_array(data, &len, 1);
        GDEBUG("Processing push %u bytes from %s", (guint)len, imsi);
        if (imsi && bytes && len) {
            char* id = g_strdup_printf("%d", database_id);
            GBytes* push = g_bytes_new(bytes, len);
            GError* error = NULL;
            if (mms_dispatcher_receive_message(engine->dispatcher, id, imsi,
                automatic, push, &error)) {
                mms_dispatcher_start(engine->dispatcher);
                org_nemomobile_mms_engine_complete_receive_message(proxy, call);
            } else {
                g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
                    G_DBUS_ERROR_FAILED, "%s", GERRMSG(error));
                g_error_free(error);
            }
            g_bytes_unref(push);
            g_free(id);
        } else {
            g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
                G_DBUS_ERROR_FAILED, "Invalid parameters");
        }
    }
    mms_engine_idle_timer_check(engine);
    return TRUE;
}

/* org.nemomobile.MmsEngine.sendReadReport */
static
gboolean
mms_engine_handle_send_read_report(
    OrgNemomobileMmsEngine* proxy,
    GDBusMethodInvocation* call,
    int database_id,
    const char* imsi,
    const char* message_id,
    const char* to,
    int read_status, /*  0: Read  1: Deleted without reading */
    MMSEngine* engine)
{
    /* mms_engine_dbus_access_allowed completes the call if access is denied */
    if (mms_engine_dbus_access_allowed(engine, call,
        MMS_ENGINE_ACTION_SEND_READ_REPORT)) {
        GError* error = NULL;
        char* id = g_strdup_printf("%d", database_id);
        GDEBUG_("%s %s %s %s %d", id, imsi, message_id, to, read_status);
        if (mms_dispatcher_send_read_report(engine->dispatcher, id, imsi,
            message_id, to, (read_status == 1) ? MMS_READ_STATUS_DELETED :
            MMS_READ_STATUS_READ, &error)) {
            mms_dispatcher_start(engine->dispatcher);
            org_nemomobile_mms_engine_complete_send_read_report(proxy, call);
        } else {
            g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
                G_DBUS_ERROR_FAILED, "%s", GERRMSG(error));
            g_error_free(error);
        }
        g_free(id);
    }
    mms_engine_idle_timer_check(engine);
    return TRUE;
}

/* org.nemomobile.MmsEngine.cancel */
static
gboolean
mms_engine_handle_cancel(
    OrgNemomobileMmsEngine* proxy,
    GDBusMethodInvocation* call,
    int database_id,
    MMSEngine* engine)
{
    /* mms_engine_dbus_access_allowed completes the call if access is denied */
    if (mms_engine_dbus_access_allowed(engine, call,
        MMS_ENGINE_ACTION_CANCEL)) {
        char* id = NULL;
        if (database_id > 0) id = g_strdup_printf("%u", database_id);
        GDEBUG_("%s", id);
        mms_dispatcher_cancel(engine->dispatcher, id);
        org_nemomobile_mms_engine_complete_cancel(proxy, call);
        g_free(id);
    }
    mms_engine_idle_timer_check(engine);
    return TRUE;
}

/* org.nemomobile.MmsEngine.pushNotify */

static
void
mms_engine_push_handler(
    MMSEngine* engine,
    GDBusMethodInvocation* call,
    const char* imsi,
    const char* type,
    GVariant* data,
    void (*complete)(
        OrgNemomobileMmsEngine* proxy,
        GDBusMethodInvocation* call))
{
    if (!type || g_ascii_strcasecmp(type, MMS_CONTENT_TYPE)) {
        GERR_("Unsupported content type %s", type);
        g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
            G_DBUS_ERROR_FAILED, "Unsupported content type");
    } else if (!imsi || !imsi[0]) {
        GERR_("IMSI is missing");
        g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
            G_DBUS_ERROR_FAILED, "IMSI is missing");
    } else {
        gsize len = 0;
        const guint8* bytes = g_variant_get_fixed_array(data, &len, 1);

        if (!bytes || !len) {
            GERR_("No data provided");
            g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
                G_DBUS_ERROR_FAILED, "No data provided");
        } else {
            GError* err = NULL;
            MMSDispatcher* dispatcher = engine->dispatcher;
            GBytes* msg = g_bytes_new_with_free_func(bytes, len,
                (GDestroyNotify) g_variant_unref, g_variant_ref(data));

            GDEBUG("Received %u bytes from %s", (guint)len, imsi);
            if (mms_dispatcher_handle_push(dispatcher, imsi, msg, &err)) {
                mms_dispatcher_start(dispatcher);
                complete(engine->proxy, call);
            } else {
                g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
                    G_DBUS_ERROR_FAILED, "%s", GERRMSG(err));
                g_error_free(err);
            }
            g_bytes_unref(msg);
        }
    }
}

static
gboolean
mms_engine_handle_push_notify(
    OrgNemomobileMmsEngine* proxy,
    GDBusMethodInvocation* call,
    const char* imsi,
    const char* type,
    GVariant* data,
    MMSEngine* engine)
{
    /* mms_engine_dbus_access_allowed completes the call if access is denied */
    if (mms_engine_dbus_access_allowed(engine, call,
        MMS_ENGINE_ACTION_PUSH_NOTIFY)) {
        mms_engine_push_handler(engine, call, imsi, type, data,
            org_nemomobile_mms_engine_complete_push_notify);
    }
    mms_engine_idle_timer_check(engine);
    return TRUE;
}

/* org.nemomobile.MmsEngine.push */
static
gboolean
mms_engine_handle_push(
    OrgNemomobileMmsEngine* proxy,
    GDBusMethodInvocation* call,
    const char* imsi,
    const char* from,
    guint32 remote_time,
    guint32 local_time,
    int dst_port,
    int src_port,
    const char* type,
    GVariant* data,
    MMSEngine* engine)
{
    /* mms_engine_dbus_access_allowed completes the call if access is denied */
    if (mms_engine_dbus_access_allowed(engine, call,
        MMS_ENGINE_ACTION_PUSH)) {
        mms_engine_push_handler(engine, call, imsi, type, data,
            org_nemomobile_mms_engine_complete_push);
    }
    mms_engine_idle_timer_check(engine);
    return TRUE;
}

/* org.nemomobile.MmsEngine.setLogLevel */
static
gboolean
mms_engine_handle_set_log_level(
    OrgNemomobileMmsEngine* proxy,
    GDBusMethodInvocation* call,
    const char* module,
    gint level,
    MMSEngine* engine)
{
    /* mms_engine_dbus_access_allowed completes the call if access is denied */
    if (mms_engine_dbus_access_allowed(engine, call,
        MMS_ENGINE_ACTION_SET_LOG_LEVEL)) {
        GDEBUG_("%s:%d", module, level);
        dbus_log_server_set_category_level(engine->log->server,
            module[0] ? module : gutil_log_default.name,
            dbus_log_level_from_gutil(level));
        org_nemomobile_mms_engine_complete_set_log_level(proxy, call);
    }
    mms_engine_idle_timer_check(engine);
    return TRUE;
}

/* org.nemomobile.MmsEngine.setLogType */
static
gboolean
mms_engine_handle_set_log_type(
    OrgNemomobileMmsEngine* proxy,
    GDBusMethodInvocation* call,
    const char* type,
    MMSEngine* engine)
{
    /* mms_engine_dbus_access_allowed completes the call if access is denied */
    if (mms_engine_dbus_access_allowed(engine, call,
        MMS_ENGINE_ACTION_SET_LOG_TYPE)) {
        GDEBUG_("%s", type);
        gutil_log_set_type(type, MMS_APP_LOG_PREFIX);
        org_nemomobile_mms_engine_complete_set_log_type(proxy, call);
    }
    mms_engine_idle_timer_check(engine);
    return TRUE;
}

/* org.nemomobile.MmsEngine.getVersion */
static
gboolean
mms_engine_handle_get_version(
    OrgNemomobileMmsEngine* proxy,
    GDBusMethodInvocation* call,
    MMSEngine* engine)
{
    /* mms_engine_dbus_access_allowed completes the call if access is denied */
    if (mms_engine_dbus_access_allowed(engine, call,
        MMS_ENGINE_ACTION_GET_VERSION)) {
#ifdef MMS_VERSION_STRING
        int v1 = 0, v2 = 0, v3 = 0;
        char* s = g_malloc(strlen(MMS_VERSION_STRING)+1);
        s[0] = 0;
        if (sscanf(MMS_VERSION_STRING, "%d.%d.%d%s", &v1, &v2, &v3, s) < 3) {
            GWARN_("unable to parse version %s", MMS_VERSION_STRING);
        } else {
            GDEBUG_("version %d.%d.%d%s", v1, v2, v3, s);
        }
        org_nemomobile_mms_engine_complete_get_version(proxy, call, v1, v2, v3, s);
        g_free(s);
#else
        GDEBUG_("oops");
        org_nemomobile_mms_engine_complete_get_version(proxy, call, 0, 0, 0, "");
#endif
    }
    mms_engine_idle_timer_check(engine);
    return TRUE;
}

/* org.nemomobile.MmsEngine.migrateSettings */
static
gboolean
mms_engine_handle_migrate_settings(
    OrgNemomobileMmsEngine* proxy,
    GDBusMethodInvocation* call,
    const char* imsi,
    MMSEngine* engine)
{
    /* mms_engine_dbus_access_allowed completes the call if access is denied */
    if (mms_engine_dbus_access_allowed(engine, call,
        MMS_ENGINE_ACTION_MIGRATE_SETTINGS)) {
        char* tmp = NULL;
        /* Querying settings will migrate per-SIM settings after upgrading
         * from 1.0.21 or older version of mms-engine */
        GDEBUG_("%s", imsi);
        if (!imsi || !imsi[0]) {
            imsi = tmp = mms_connman_default_imsi(engine->cm);
        }
        if (imsi) {
            mms_settings_get_sim_data(engine->settings, imsi);
        }
        org_nemomobile_mms_engine_complete_migrate_settings(proxy, call);
        g_free(tmp);
    }
    mms_engine_idle_timer_check(engine);
    return TRUE;
}

/* org.nemomobile.MmsEngine.exit */
static
gboolean
mms_engine_handle_exit(
    OrgNemomobileMmsEngine* proxy,
    GDBusMethodInvocation* call,
    MMSEngine* engine)
{
    /* mms_engine_dbus_access_allowed completes the call if access is denied */
    if (mms_engine_dbus_access_allowed(engine, call, MMS_ENGINE_ACTION_EXIT)) {
        GDEBUG("Exit requested over D-Bus");
        mms_engine_stop(engine);
        org_nemomobile_mms_engine_complete_exit(proxy, call);
    }
    return TRUE;
}

MMSEngine*
mms_engine_new(
    const MMSConfig* config,
    const MMSSettingsSimData* defaults,
    const MMSEngineDbusConfig* dbus,
    MMSLogModule* log_modules[], /* NULL terminated */
    unsigned int flags)
{
    MMSConnMan* cm = mms_connman_new();
    if (cm) {
        MMSEngine* mms = g_object_new(MMS_TYPE_ENGINE, NULL);
        MMSHandler* handler = mms_handler_dbus_new();
        MMSSettings* settings = mms_settings_dconf_new(config, defaults);
        MMSTransferList* txlist = mms_transfer_list_dbus_new
            (dbus->tx_list_access, dbus->tx_access);

        static const struct _mms_engine_settings_flags_map {
#define MAP_(x) \
    MMS_ENGINE_FLAG_OVERRIDE_##x, \
    MMS_SETTINGS_FLAG_OVERRIDE_##x
            int engine_flag;
            int settings_flag;
        } flags_map [] = {
            { MAP_(USER_AGENT)},
            { MAP_(UAPROF) },
            { MAP_(SIZE_LIMIT) },
            { MAP_(MAX_PIXELS) }
        };

        unsigned int i;
        for (i=0; i<G_N_ELEMENTS(flags_map); i++) {
            if (flags & flags_map[i].engine_flag) {
                settings->flags |= flags_map[i].settings_flag;
            }
        }

        mms->dispatcher = mms_dispatcher_new(settings, cm, handler, txlist);
        mms_handler_unref(handler);
        mms_transfer_list_unref(txlist);
        mms_dispatcher_set_delegate(mms->dispatcher,
            &mms->dispatcher_delegate);

        if (flags & MMS_ENGINE_FLAG_KEEP_RUNNING) {
            mms->keep_running = TRUE;
        }

        if (flags & MMS_ENGINE_FLAG_DISABLE_DBUS_LOG) {
            mms->disable_dbus_log = TRUE;
        }

        mms->cm = cm;
        mms->config = config;
        mms->settings = settings;
        mms->log = mms_log_new(dbus->type, log_modules);
        mms->dbus_access = da_policy_ref(dbus->engine_access);
        mms->da_bus = (dbus->type == G_BUS_TYPE_SESSION) ?
            DA_BUS_SESSION : DA_BUS_SYSTEM;

        mms->proxy = org_nemomobile_mms_engine_skeleton_new();
#define MMS_ENGINE_HANDLER_(ID,id,name)  \
        mms->proxy_signal_id[MMS_ENGINE_METHOD_##ID] = \
            g_signal_connect(mms->proxy, "handle-" name, \
            G_CALLBACK(mms_engine_handle_##id), mms);
        MMS_ENGINE_DBUS_METHODS(MMS_ENGINE_HANDLER_)
#undef  MMS_ENGINE_HANDLER_

        return mms;
    }

    return NULL;
}

MMSEngine*
mms_engine_ref(
    MMSEngine* engine)
{
    return g_object_ref(MMS_ENGINE(engine));
}

void
mms_engine_unref(
    MMSEngine* engine)
{
    if (engine) g_object_unref(MMS_ENGINE(engine));
}

void
mms_engine_run(
    MMSEngine* engine,
    GMainLoop* loop)
{
    DBusLogServer* logger = engine->disable_dbus_log ? NULL :
        engine->log->server;

    GASSERT(!engine->loop);
    engine->loop = loop;
    engine->stopped = FALSE;
    engine->stop_requested = FALSE;
    mms_dispatcher_start(engine->dispatcher);
    dbus_log_server_start(logger);
    mms_engine_idle_timer_check(engine);
    g_main_loop_run(loop);
    mms_engine_idle_timer_stop(engine);
    dbus_log_server_stop(logger);
    engine->loop = NULL;
}

void
mms_engine_stop(
    MMSEngine* engine)
{
    if (mms_dispatcher_is_active(engine->dispatcher)) {
        engine->stop_requested = TRUE;
        mms_dispatcher_cancel(engine->dispatcher, NULL);
    } else {
        mms_engine_stop_schedule(engine);
    }
}

void
mms_engine_unregister(
    MMSEngine* engine)
{
    if (engine->engine_bus) {
        g_dbus_interface_skeleton_unexport(
            G_DBUS_INTERFACE_SKELETON(engine->proxy));
        g_object_unref(engine->engine_bus);
        engine->engine_bus = NULL;
    }
}

gboolean
mms_engine_register(
    MMSEngine* engine,
    GDBusConnection* bus,
    GError** error)
{
    mms_engine_unregister(engine);
    if (g_dbus_interface_skeleton_export(
        G_DBUS_INTERFACE_SKELETON(engine->proxy), bus,
        MMS_ENGINE_PATH, error)) {
        g_object_ref(engine->engine_bus = bus);
        return TRUE;
    } else {
        return FALSE;
    }
}

static
void
mms_engine_delegate_dispatcher_done(
    MMSDispatcherDelegate* delegate,
    MMSDispatcher* dispatcher)
{
    MMSEngine* engine = mms_engine_from_dispatcher_delegate(delegate);
    GDEBUG("All done");
    if (engine->stop_requested) {
        mms_engine_stop_schedule(engine);
    } else {
        mms_engine_idle_timer_check(engine);
    }
}

/**
 * Per object initializer
 *
 * Only sets up internal state (all values set to zero)
 */
static
void
mms_engine_init(
    MMSEngine* engine)
{
    engine->dispatcher_delegate.fn_done = mms_engine_delegate_dispatcher_done;
}

/**
 * First stage of deinitialization (release all references).
 * May be called more than once in the lifetime of the object.
 */
static
void
mms_engine_dispose(
    GObject* object)
{
    MMSEngine* mms = MMS_ENGINE(object);
    GVERBOSE_("%p", mms);
    GASSERT(!mms->loop);
    mms_engine_unregister(mms);
    mms_engine_idle_timer_stop(mms);
    if (mms->proxy) {
        gutil_disconnect_handlers(mms->proxy, mms->proxy_signal_id,
            G_N_ELEMENTS(mms->proxy_signal_id));
        g_object_unref(mms->proxy);
        mms->proxy = NULL;
    }
    if (mms->dispatcher) {
        mms_dispatcher_set_delegate(mms->dispatcher, NULL);
        mms_dispatcher_unref(mms->dispatcher);
        mms->dispatcher = NULL;
    }
    if (mms->settings) {
        mms_settings_unref(mms->settings);
        mms->settings = NULL;
    }
    if (mms->cm) {
        mms_connman_unref(mms->cm);
        mms->cm = NULL;
    }
    G_OBJECT_CLASS(mms_engine_parent_class)->dispose(object);
}

/**
 * Final stage of deinitialization
 */
static
void
mms_engine_finalize(
    GObject* object)
{
    MMSEngine* engine = MMS_ENGINE(object);
    GVERBOSE_("%p", engine);
    da_policy_unref(engine->dbus_access);
    mms_log_free(engine->log);
    G_OBJECT_CLASS(mms_engine_parent_class)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
mms_engine_class_init(
    MMSEngineClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    GASSERT(object_class);
    object_class->dispose = mms_engine_dispose;
    object_class->finalize = mms_engine_finalize;
    GVERBOSE_("done");
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
