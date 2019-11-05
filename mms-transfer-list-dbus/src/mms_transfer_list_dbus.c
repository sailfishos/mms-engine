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

#include "mms_transfer_list_dbus.h"
#include "mms_transfer_list_dbus_log.h"
#include "mms_transfer_dbus.h"

#include <gutil_strv.h>

/* Generated code */
#include "org.nemomobile.MmsEngine.TransferList.h"

/* D-Bus interface */
#define MMS_TRANSFER_LIST_INTERFACE "org.nemomobile.MmsEngine.TransferList"
#define MMS_TRANSFER_LIST_SIGNAL_TRANSFER_STARTED "TransferStarted"
#define MMS_TRANSFER_LIST_SIGNAL_TRANSFER_FINISHED "TransferFinished"

/* Class definition */
typedef MMSTransferListClass MMSTransferListDbusClass;
struct mms_transfer_list_dbus {
    MMSTransferList super;
    GDBusConnection* bus;
    OrgNemomobileMmsEngineTransferList* skeleton;
    gulong list_transfers_id;
    GHashTable* transfers;
    GHashTable* clients;
};

G_DEFINE_TYPE(MMSTransferListDbus, mms_transfer_list_dbus, \
    MMS_TYPE_TRANSFER_LIST)
#define MMS_TYPE_TRANSFER_LIST_DBUS (mms_transfer_list_dbus_get_type())
#define MMS_TRANSFER_LIST_DBUS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
    MMS_TYPE_TRANSFER_LIST_DBUS, MMSTransferListDbus))

#define MMS_TRANSFER_LIST_DBUS_PATH "/"
#define MMS_TRANSFER_LIST_DBUS_BUS  G_BUS_TYPE_SYSTEM

/*
 * Sends signals only to registered clients. That's not much of an overhead
 * because there's usually no more than one client (i.e. Messages app).
 */
static
void
mms_transfer_dbus_list_emit_signal(
    MMSTransferListDbus* self,
    const char* signal,
    const char* path)
{
    if (self->clients) {
        gpointer key;
        GHashTableIter it;
        GVariant* arg = g_variant_ref_sink(g_variant_new("(o)", path));

        g_hash_table_iter_init(&it, self->clients);
        while (g_hash_table_iter_next(&it, &key, NULL)) {
            const char* dest = key;
            GError* error = NULL;
            GDBusMessage* message = g_dbus_message_new_signal(
                g_dbus_interface_skeleton_get_object_path
                (G_DBUS_INTERFACE_SKELETON(self->skeleton)),
                MMS_TRANSFER_LIST_INTERFACE, signal);

            g_dbus_message_set_body(message, arg);
            g_dbus_message_set_destination(message, dest);
            g_dbus_connection_send_message(self->bus, message,
                G_DBUS_SEND_MESSAGE_FLAGS_NONE, NULL, &error);
            g_object_unref(message);
            if (error) {
                GERR("%s => %s failed: %s", signal, dest, error->message);
                g_error_free(error);
            }
        }
        g_variant_unref(arg);
    }
}

static
guint
mms_transfer_key_hash_cb(
    gconstpointer data)
{
    if (data) {
        /* There's shouldn't be more than one transfer per message at
         * any time, it's enough to just hash the message id */
        const MMSTransferKey* key = data;

        return g_str_hash(key->id);
    } else {
        return 0;
    }
}

static
gboolean
mms_transfer_key_equal_cb(
    gconstpointer a,
    gconstpointer b)
{
    if (a == b) {
        return TRUE;
    } else if (!a || !b) {
        return FALSE;
    } else {
        const MMSTransferKey* key1 = a;
        const MMSTransferKey* key2 = a;

        return !g_strcmp0(key1->id, key2->id) &&
               !g_strcmp0(key1->type, key2->type);
    }
}

static
void
mms_transfer_destroy_cb(
    gpointer data)
{
    MMSTransferDbus* transfer = MMS_TRANSFER_DBUS(data);
    MMSTransferListDbus* self = MMS_TRANSFER_LIST_DBUS(transfer->list);

    GDEBUG("Transfer %s finished", transfer->path);
    mms_transfer_dbus_finished(transfer);
    mms_transfer_dbus_list_emit_signal(self,
        MMS_TRANSFER_LIST_SIGNAL_TRANSFER_FINISHED, transfer->path);
    transfer->list = NULL;
    g_object_unref(transfer);
}

static
void
mms_transfer_list_dbus_bus_cb(
    GObject* object,
    GAsyncResult* res,
    gpointer user_data)
{
    MMSTransferListDbus* self = MMS_TRANSFER_LIST_DBUS(user_data);

    self->bus = g_bus_get_finish(res, NULL);
    if (self->bus) {
        GError* error = NULL;
        if (!g_dbus_interface_skeleton_export(
            G_DBUS_INTERFACE_SKELETON(self->skeleton), self->bus,
            MMS_TRANSFER_LIST_DBUS_PATH, &error)) {
            GERR("%s", GERRMSG(error));
            g_error_free(error);
        }
    }
    g_object_unref(self);
}

MMSTransferList*
mms_transfer_list_dbus_new()
{
    MMSTransferListDbus* self = g_object_new(MMS_TYPE_TRANSFER_LIST_DBUS, 0);

    g_bus_get(MMS_TRANSFER_LIST_DBUS_BUS, NULL, mms_transfer_list_dbus_bus_cb,
        g_object_ref(self));
    return &self->super;
}

static
void
mms_transfer_list_dbus_transfer_started(
    MMSTransferList* list,          /* Instance */
    char* id,                       /* Database record ID */
    char* type)                     /* Transfer type */
{
    MMSTransferListDbus* self = MMS_TRANSFER_LIST_DBUS(list);
    MMSTransferDbus* transfer = mms_transfer_dbus_new(self->bus, id, type);

    GDEBUG("Transfer %s started", transfer->path);
    transfer->list = self;
    g_hash_table_replace(self->transfers, &transfer->key, transfer);
    mms_transfer_dbus_list_emit_signal(self,
        MMS_TRANSFER_LIST_SIGNAL_TRANSFER_STARTED, transfer->path);
}

static
void
mms_transfer_list_dbus_transfer_finished(
    MMSTransferList* list,          /* Instance */
    char* id,                       /* Database record ID */
    char* type)                     /* Transfer type */
{
    MMSTransferListDbus* self = MMS_TRANSFER_LIST_DBUS(list);
    MMSTransferKey key;

    key.id = id;
    key.type = type;
    if (!g_hash_table_remove(self->transfers, &key)) {
        GWARN("Transfer %s/%s not found", id, type);
    }
}

static
MMSTransferDbus*
mms_transfer_list_dbus_find(
    MMSTransferListDbus* self,
    char* id,
    char* type)
{
    MMSTransferDbus* transfer;
    MMSTransferKey key;

    key.id = id;
    key.type = type;
    transfer = g_hash_table_lookup(self->transfers, &key);
    if (transfer) {
        return transfer;
    } else {
        GWARN("Transfer %s/%s not found", id, type);
        return NULL;
    }
}

static
void
mms_transfer_list_dbus_send_progress(
    MMSTransferList* list,          /* Instance */
    char* id,                       /* Message ID */
    char* type,                     /* Transfer type */
    guint sent,                     /* Bytes sent so far */
    guint total)                    /* Total bytes to send */
{
    MMSTransferListDbus* self = MMS_TRANSFER_LIST_DBUS(list);
    MMSTransferDbus* transfer = mms_transfer_list_dbus_find(self, id, type);

    if (transfer) {
        mms_transfer_dbus_send_progress(transfer, sent, total);
    }
}

static
void
mms_transfer_list_dbus_receive_progress(
    MMSTransferList* list,          /* Instance */
    char* id,                       /* Message ID */
    char* type,                     /* Transfer type */
    guint received,                 /* Bytes received so far */
    guint total)                    /* Total bytes to receive*/
{
    MMSTransferListDbus* self = MMS_TRANSFER_LIST_DBUS(list);
    MMSTransferDbus* transfer = mms_transfer_list_dbus_find(self, id, type);

    if (transfer) {
        mms_transfer_dbus_receive_progress(transfer, received, total);
    }
}

static
void
mms_transfer_list_dbus_client_vanished(
    GDBusConnection* bus,
    const gchar* name,
    gpointer user_data)
{
    MMSTransferListDbus* self = MMS_TRANSFER_LIST_DBUS(user_data);

    GDEBUG("Client '%s' has disappeared", name);
    g_hash_table_remove(self->clients, name);
    if (!g_hash_table_size(self->clients)) {
        g_hash_table_destroy(self->clients);
        self->clients = NULL;
    }
}

/* org.nemomobile.MmsEngine.TransferList.Get */
static
gboolean
mms_transfer_list_dbus_handle_get(
    OrgNemomobileMmsEngineTransferList* skeleton,
    GDBusMethodInvocation* call,
    MMSTransferListDbus* self)
{
    const char* sender = g_dbus_method_invocation_get_sender(call);
    const char* null = NULL;
    const gchar* const* list = &null;
    const gchar** paths = NULL;
    guint n = g_hash_table_size(self->transfers);

    /* Store the sender's name */
    if (!self->clients) {
        self->clients = /* sender => watch id */
            g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    }
    if (!g_hash_table_contains(self->clients, sender)) {
        /* New client */
        gulong watch_id = g_bus_watch_name_on_connection(self->bus,
            sender, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL,
            mms_transfer_list_dbus_client_vanished, self, NULL);

        g_hash_table_insert(self->clients, g_strdup(sender),
            GUINT_TO_POINTER(watch_id));
    }

    if (n) {
        guint i = 0;
        gpointer value;
        GHashTableIter it;

        g_hash_table_iter_init(&it, self->transfers);
        paths = g_new(const char*, n+1);
        GVERBOSE("%u transfer(s)", n);
        while (i < n && g_hash_table_iter_next(&it, NULL, &value)) {
            MMSTransferDbus* transfer = value;
            GVERBOSE("  %s", transfer->path);
            paths[i++] = transfer->path;
        }
        paths[i++] = NULL;
        list = paths;
    } else {
        GVERBOSE("No transfers");
        list = &null;
    }
    org_nemomobile_mms_engine_transfer_list_complete_get(skeleton, call, list);
    g_free(paths);
    return TRUE;
}

/**
 * Final stage of deinitialization
 */
static
void
mms_transfer_list_dbus_finalize(
    GObject* object)
{
    MMSTransferListDbus* self = MMS_TRANSFER_LIST_DBUS(object);

    g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON(self->skeleton));
    g_signal_handler_disconnect(self->skeleton, self->list_transfers_id);
    g_object_unref(self->skeleton);
    g_hash_table_destroy(self->transfers);
    if (self->clients) {
        g_hash_table_destroy(self->clients);
    }
    if (self->bus) {
        g_object_unref(self->bus);
    }
    G_OBJECT_CLASS(mms_transfer_list_dbus_parent_class)->finalize(object);
}

/**
 * Per instance initializer
 */
static
void
mms_transfer_list_dbus_init(
    MMSTransferListDbus* self)
{
    self->skeleton = org_nemomobile_mms_engine_transfer_list_skeleton_new();
    self->list_transfers_id = g_signal_connect(self->skeleton, "handle-get",
        G_CALLBACK(mms_transfer_list_dbus_handle_get), self);
    self->transfers = g_hash_table_new_full(mms_transfer_key_hash_cb,
        mms_transfer_key_equal_cb, NULL, mms_transfer_destroy_cb);
}

/**
 * Per class initializer
 */
static
void
mms_transfer_list_dbus_class_init(
    MMSTransferListDbusClass* klass)
{
    klass->fn_transfer_started = mms_transfer_list_dbus_transfer_started;
    klass->fn_transfer_finished = mms_transfer_list_dbus_transfer_finished;
    klass->fn_send_progress = mms_transfer_list_dbus_send_progress;
    klass->fn_receive_progress = mms_transfer_list_dbus_receive_progress;
    G_OBJECT_CLASS(klass)->finalize = mms_transfer_list_dbus_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
