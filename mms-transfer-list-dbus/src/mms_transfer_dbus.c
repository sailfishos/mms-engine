/*
 * Copyright (C) 2016-2019 Jolla Ltd.
 * Copyright (C) 2016-2019 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2019 Open Mobile Platform LLC.
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

#include "mms_transfer_dbus.h"
#include "mms_transfer_list_dbus_log.h"

#include <gutil_misc.h>

/* Logging */
#define GLOG_MODULE_NAME mms_transfer_list_log
#include <gutil_log.h>

/* Generated code */
#include "org.nemomobile.MmsEngine.TransferList.h"

/* D-Bus interface */
#define MMS_TRANSFER_INTERFACE "org.nemomobile.MmsEngine.Transfer"
#define MMS_TRANSFER_SIGNAL_SEND_PROGRESS_CHANGED "SendProgressChanged"
#define MMS_TRANSFER_SIGNAL_RECEIVE_PROGRESS_CHANGED "ReceiveProgressChanged"
#define MMS_TRANSFER_SIGNAL_FINISHED "Finished"

/* Class definition */

enum mms_transfer_dbus_method {
    MMS_TRANSFER_DBUS_METHOD_GET_ALL,
    MMS_TRANSFER_DBUS_METHOD_ENABLE_UPDATES,
    MMS_TRANSFER_DBUS_METHOD_DISABLE_UPDATES,
    MMS_TRANSFER_DBUS_METHOD_GET_INTERFACE_VERSION,
    MMS_TRANSFER_DBUS_METHOD_GET_SEND_PROGRESS,
    MMS_TRANSFER_DBUS_METHOD_GET_RECEIVE_PROGRESS,
    MMS_TRANSFER_DBUS_METHOD_COUNT
};

typedef enum mms_transfer_flags {
    MMS_TRANSFER_FLAG_NONE = 0x00,
    MMS_TRANSFER_FLAG_SEND_UPDATES_ENABLED = 0x01,
    MMS_TRANSFER_FLAG_RECEIVE_UPDATES_ENABLED = 0x02,
    MMS_TRANSFER_FLAG_FINISHED = 0x10 /* Internal flag */
} MMS_TRANSFER_FLAGS;

typedef GObjectClass MMSTransferDbusClass;
struct mms_transfer_dbus_priv {
    char* id;
    char* type;
    char* path;
    GDBusConnection* bus;
    OrgNemomobileMmsEngineTransfer* skeleton;
    gulong proxy_signal_id[MMS_TRANSFER_DBUS_METHOD_COUNT];
    GHashTable* clients;
    guint last_update_cookie;
    MMS_TRANSFER_FLAGS flags;
    guint bytes_sent;
    guint bytes_to_send;
    guint bytes_received;
    guint bytes_to_receive;
};

typedef struct mms_transfer_dbus_client {
    gulong watch_id;
    GHashTable* requests;
    MMS_TRANSFER_FLAGS flags;
} MMSTransferDbusClient;

G_DEFINE_TYPE(MMSTransferDbus, mms_transfer_dbus, G_TYPE_OBJECT)

#define MMS_TRANSFER_DBUS_INTERFACE_VERSION (1)

/*
 * Sends signals only to registered clients. That's not much of an overhead
 * because there's usually no more than one client (i.e. Messages app).
 */
static
void
mms_transfer_dbus_emit_signal(
    MMSTransferDbus* self,
    MMS_TRANSFER_FLAGS flags,
    const char* signal,
    GVariant* args)  /* floating */
{
    MMSTransferDbusPriv* priv = self->priv;

    g_variant_ref_sink(args);
    if (priv->clients && g_hash_table_size(priv->clients)) {
        gpointer key, value;
        GHashTableIter it;

        g_hash_table_iter_init(&it, priv->clients);
        while (g_hash_table_iter_next(&it, &key, &value)) {
            const char* dest = key;
            MMSTransferDbusClient* client = value;

            if (client->flags & flags) {
                GError* error = NULL;
                GDBusMessage* message = g_dbus_message_new_signal(
                    g_dbus_interface_skeleton_get_object_path
                    (G_DBUS_INTERFACE_SKELETON(priv->skeleton)),
                    MMS_TRANSFER_INTERFACE, signal);

                g_dbus_message_set_body(message, args);
                g_dbus_message_set_destination(message, dest);
                g_dbus_connection_send_message(priv->bus, message,
                    G_DBUS_SEND_MESSAGE_FLAGS_NONE, NULL, &error);
                g_object_unref(message);
                if (error) {
                    GERR("%s => %s failed: %s", signal, dest, error->message);
                    g_error_free(error);
                }
            }
        }
    }
    g_variant_unref(args);
}

void
mms_transfer_dbus_send_progress(
    MMSTransferDbus* self,
    guint sent,
    guint total)
{
    MMSTransferDbusPriv* priv = self->priv;

    if (priv->bytes_sent != sent ||
        priv->bytes_to_send != total) {
        priv->bytes_sent = sent;
        priv->bytes_to_send = total;
        if (priv->flags & MMS_TRANSFER_FLAG_SEND_UPDATES_ENABLED) {
            mms_transfer_dbus_emit_signal(self,
                MMS_TRANSFER_FLAG_SEND_UPDATES_ENABLED,
                MMS_TRANSFER_SIGNAL_SEND_PROGRESS_CHANGED,
                g_variant_new("(uu)", sent, total));
        }
    }
}

void
mms_transfer_dbus_receive_progress(
    MMSTransferDbus* self,
    guint received,
    guint total)
{
    MMSTransferDbusPriv* priv = self->priv;

    if (priv->bytes_received != received ||
        priv->bytes_to_receive != total) {
        priv->bytes_received = received;
        priv->bytes_to_receive = total;
        if (priv->flags & MMS_TRANSFER_FLAG_RECEIVE_UPDATES_ENABLED) {
            mms_transfer_dbus_emit_signal(self,
                MMS_TRANSFER_FLAG_RECEIVE_UPDATES_ENABLED,
                MMS_TRANSFER_SIGNAL_RECEIVE_PROGRESS_CHANGED,
                g_variant_new("(uu)", received, total));
        }
    }
}

void
mms_transfer_dbus_finished(
    MMSTransferDbus* self)
{
    MMSTransferDbusPriv* priv = self->priv;

    if (priv->flags & MMS_TRANSFER_FLAG_FINISHED) {
        mms_transfer_dbus_emit_signal(self,
            MMS_TRANSFER_FLAG_FINISHED,
            MMS_TRANSFER_SIGNAL_FINISHED,
            g_variant_new("()"));
    }
}

static
void
mms_transfer_dbus_client_destroy(
    gpointer data)
{
    MMSTransferDbusClient* client = data;

    g_bus_unwatch_name(client->watch_id);
    g_hash_table_destroy(client->requests);
    g_slice_free1(sizeof(*client), client);
}

static
void
mms_transfer_dbus_update_flags(
    MMSTransferDbus* self)
{
    MMSTransferDbusPriv* priv = self->priv;
    MMS_TRANSFER_FLAGS flags = 0;

    if (g_hash_table_size(priv->clients)) {
        gpointer value;
        GHashTableIter it;

        g_hash_table_iter_init(&it, priv->clients);
        while (g_hash_table_iter_next(&it, NULL, &value)) {
            flags |= ((MMSTransferDbusClient*)value)->flags;
        }
    }
    if (priv->flags != flags) {
        priv->flags = flags;
        GDEBUG("Update flags => 0x%02x", priv->flags);
    }
}

static
void
mms_transfer_dbus_client_vanished(
    GDBusConnection* bus,
    const gchar* name,
    gpointer user_data)
{
    MMSTransferDbus* self = MMS_TRANSFER_DBUS(user_data);
    MMSTransferDbusPriv* priv = self->priv;

    GDEBUG("Name '%s' has disappeared", name);
    g_hash_table_remove(priv->clients, name);
    mms_transfer_dbus_update_flags(self);
}

/* org.nemomobile.MmsEngine.Transfer.GetAll */
static
gboolean
mms_transfer_dbus_handle_get_all(
    OrgNemomobileMmsEngineTransfer* proxy,
    GDBusMethodInvocation* call,
    MMSTransferDbus* self)
{
    MMSTransferDbusPriv* priv = self->priv;

    org_nemomobile_mms_engine_transfer_complete_get_all(proxy, call,
        MMS_TRANSFER_DBUS_INTERFACE_VERSION,
        priv->bytes_sent,
        priv->bytes_to_send,
        priv->bytes_received,
        priv->bytes_to_receive);
    return TRUE;
}

/* org.nemomobile.MmsEngine.Transfer.EnableUpdates */
static
gboolean
mms_transfer_dbus_handle_enable_updates(
    OrgNemomobileMmsEngineTransfer* proxy,
    GDBusMethodInvocation* call,
    guint flags,
    MMSTransferDbus* self)
{
    MMSTransferDbusPriv* priv = self->priv;
    guint cookie = 0;

    if (flags) {
        MMSTransferDbusClient* client = NULL;
        const char* sender = g_dbus_method_invocation_get_sender(call);

        /* All registered clients get "Finished" signal */
        flags |= MMS_TRANSFER_FLAG_FINISHED;
        cookie = ++(priv->last_update_cookie);
        GVERBOSE_("%s %u -> 0x%02x", sender, cookie, flags);

        /* Create client context if necessary */
        if (priv->clients) {
            client = g_hash_table_lookup(priv->clients, sender);
        } else {
            priv->clients = g_hash_table_new_full(g_str_hash, g_str_equal,
                g_free, mms_transfer_dbus_client_destroy);
        }
        if (!client) {
            client = g_slice_new0(MMSTransferDbusClient);
            client->requests = g_hash_table_new(g_direct_hash, g_direct_equal);
            client->watch_id = g_bus_watch_name_on_connection(priv->bus,
                sender, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL,
                mms_transfer_dbus_client_vanished, self, NULL);
            g_hash_table_insert(priv->clients, g_strdup(sender), client);
        }

        client->flags |= flags;
        g_hash_table_insert(client->requests, GINT_TO_POINTER(cookie),
            GINT_TO_POINTER(flags));
        if ((priv->flags & flags) != flags) {
            priv->flags |= flags;
            GDEBUG("Update flags => 0x%02x", priv->flags);
        }
    } else {
        GWARN("Client provided no update flags!");
    }

    org_nemomobile_mms_engine_transfer_complete_enable_updates(proxy,
        call, cookie);
    return TRUE;
}

/* org.nemomobile.MmsEngine.Transfer.DisableUpdates */
static
gboolean
mms_transfer_dbus_handle_disable_updates(
    OrgNemomobileMmsEngineTransfer* proxy,
    GDBusMethodInvocation* call,
    guint cookie,
    MMSTransferDbus* self)
{
    MMSTransferDbusPriv* priv = self->priv;
    MMSTransferDbusClient* client = NULL;
    const char* sender = g_dbus_method_invocation_get_sender(call);

    GVERBOSE_("%s %u", sender, cookie);
    org_nemomobile_mms_engine_transfer_complete_disable_updates(proxy, call);

    if (priv->clients) {
        client = g_hash_table_lookup(priv->clients, sender);
    }
    if (client) {
        gpointer value;
        GHashTableIter it;

        /* Update client flags */
        client->flags = 0;
        g_hash_table_remove(client->requests, GINT_TO_POINTER(cookie));
        g_hash_table_iter_init(&it, client->requests);
        while (g_hash_table_iter_next(&it, NULL, &value)) {
            client->flags |= GPOINTER_TO_INT(value);
        }
        mms_transfer_dbus_update_flags(self);
    }

    return TRUE;
}

/* org.nemomobile.MmsEngine.Transfer.GetInterfaceVersion */
static
gboolean
mms_transfer_dbus_handle_get_interface_version(
    OrgNemomobileMmsEngineTransfer* proxy,
    GDBusMethodInvocation* call,
    MMSTransferDbus* self)
{
    org_nemomobile_mms_engine_transfer_complete_get_interface_version(proxy,
        call, MMS_TRANSFER_DBUS_INTERFACE_VERSION);
    return TRUE;
}

/* org.nemomobile.MmsEngine.Transfer.GetSendProgress */
static
gboolean
mms_transfer_dbus_handle_get_send_progress(
    OrgNemomobileMmsEngineTransfer* proxy,
    GDBusMethodInvocation* call,
    MMSTransferDbus* self)
{
    MMSTransferDbusPriv* priv = self->priv;

    org_nemomobile_mms_engine_transfer_complete_get_send_progress(proxy,
        call, priv->bytes_sent, priv->bytes_to_send);
    return TRUE;
}

/* org.nemomobile.MmsEngine.Transfer.GetReceiveProgress */
static
gboolean
mms_transfer_dbus_handle_get_receive_progress(
    OrgNemomobileMmsEngineTransfer* proxy,
    GDBusMethodInvocation* call,
    MMSTransferDbus* self)
{
    MMSTransferDbusPriv* priv = self->priv;

    org_nemomobile_mms_engine_transfer_complete_get_receive_progress(proxy,
        call, priv->bytes_received, priv->bytes_to_receive);
    return TRUE;
}

MMSTransferDbus*
mms_transfer_dbus_new(
    GDBusConnection* bus,
    const char* id,
    const char* type)
{
    MMSTransferDbus* self = g_object_new(MMS_TYPE_TRANSFER_DBUS, NULL);
    MMSTransferDbusPriv* priv = self->priv;
    GError* error = NULL;

    self->key.id = priv->id = g_strdup(id);
    self->key.type = priv->type = g_strdup(type);
    self->path = priv->path = g_strconcat("/msg/", id, "/", type, NULL);
    priv->bus = g_object_ref(bus);

	priv->skeleton = org_nemomobile_mms_engine_transfer_skeleton_new();
	priv->proxy_signal_id[MMS_TRANSFER_DBUS_METHOD_GET_ALL] =
	    g_signal_connect(priv->skeleton, "handle-get-all",
	    G_CALLBACK(mms_transfer_dbus_handle_get_all), self);
	priv->proxy_signal_id[MMS_TRANSFER_DBUS_METHOD_ENABLE_UPDATES] =
	    g_signal_connect(priv->skeleton, "handle-enable-updates",
	    G_CALLBACK(mms_transfer_dbus_handle_enable_updates), self);
	priv->proxy_signal_id[MMS_TRANSFER_DBUS_METHOD_DISABLE_UPDATES] =
	    g_signal_connect(priv->skeleton, "handle-disable-updates",
	    G_CALLBACK(mms_transfer_dbus_handle_disable_updates), self);
	priv->proxy_signal_id[MMS_TRANSFER_DBUS_METHOD_GET_INTERFACE_VERSION] =
	    g_signal_connect(priv->skeleton, "handle-get-interface-version",
	    G_CALLBACK(mms_transfer_dbus_handle_get_interface_version), self);
	priv->proxy_signal_id[MMS_TRANSFER_DBUS_METHOD_GET_SEND_PROGRESS] =
	    g_signal_connect(priv->skeleton, "handle-get-send-progress",
	    G_CALLBACK(mms_transfer_dbus_handle_get_send_progress), self);
	priv->proxy_signal_id[MMS_TRANSFER_DBUS_METHOD_GET_RECEIVE_PROGRESS] =
	    g_signal_connect(priv->skeleton, "handle-get-receive-progress",
	    G_CALLBACK(mms_transfer_dbus_handle_get_receive_progress), self);
    if (!g_dbus_interface_skeleton_export(
        G_DBUS_INTERFACE_SKELETON(priv->skeleton),
        priv->bus, priv->path, &error)) {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }
    return self;
}

/**
 * Final stage of deinitialization
 */
static
void
mms_transfer_dbus_finalize(
    GObject* object)
{
    MMSTransferDbus* self = MMS_TRANSFER_DBUS(object);
    MMSTransferDbusPriv* priv = self->priv;

    if (priv->clients) {
        g_hash_table_destroy(priv->clients);
    }
    if (priv->skeleton) {
        g_dbus_interface_skeleton_unexport(
            G_DBUS_INTERFACE_SKELETON(priv->skeleton));
        gutil_disconnect_handlers(priv->skeleton, priv->proxy_signal_id,
            G_N_ELEMENTS(priv->proxy_signal_id));
        g_object_unref(priv->skeleton);
    }
    if (priv->bus) {
        g_object_unref(priv->bus);
    }
    g_free(priv->id);
    g_free(priv->type);
    g_free(priv->path);
    G_OBJECT_CLASS(mms_transfer_dbus_parent_class)->finalize(object);
}

/**
 * Per instance initializer
 */
static
void
mms_transfer_dbus_init(
    MMSTransferDbus* self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        MMS_TYPE_TRANSFER_DBUS, MMSTransferDbusPriv);
}

/**
 * Per class initializer
 */
static
void
mms_transfer_dbus_class_init(
    MMSTransferDbusClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(MMSTransferDbusPriv));
    object_class->finalize = mms_transfer_dbus_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
