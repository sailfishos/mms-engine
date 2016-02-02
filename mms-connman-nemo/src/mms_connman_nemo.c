/*
 * Copyright (C) 2016 Jolla Ltd.
 * Contact: Slava Monich <slava.monich@jolla.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "mms_connman_nemo.h"
#include "mms_connection_nemo.h"

/* Logging */
#define MMS_LOG_MODULE_NAME mms_connman_log
#include "mms_connman_nemo_log.h"
MMS_LOG_MODULE_DEFINE("mms-connman-nemo");

typedef MMSConnManClass MMSConnManNemoClass;
typedef struct mms_connman_nemo {
    MMSConnMan cm;
    OfonoExtModemManager* mm;
    MMSConnection* conn;
    gulong voice_sim_changed_id;
} MMSConnManNemo;

G_DEFINE_TYPE(MMSConnManNemo, mms_connman_nemo, MMS_TYPE_CONNMAN)
#define MMS_TYPE_CONNMAN_NEMO (mms_connman_nemo_get_type())
#define MMS_CONNMAN_NEMO(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
    MMS_TYPE_CONNMAN_NEMO, MMSConnManNemo))

#define MMS_INIT_TIMEOUT_SEC (30)

static
gboolean
mms_connman_nemo_wait_timeout(
    gpointer data)
{
    g_main_loop_quit(data);
    return G_SOURCE_REMOVE;
}

static
void
mms_connman_nemo_wait_valid_changed(
    OfonoExtModemManager* mm,
    void* data)
{
    if (mm->valid) {
        g_main_loop_quit(data);
    }
}

/**
 * Checks if OfonoExtModemManager is initialized and waits up to
 * MMS_INIT_TIMEOUT_SEC if necessary.
 */
static
gboolean
mms_connman_nemo_mm_valid(
    MMSConnManNemo* self)
{
    if (self->mm->valid) {
        return TRUE;
    } else {
        /* That shouldn't take long */
        GMainLoop* loop = g_main_loop_new(NULL, TRUE);
        guint timeout_id = g_timeout_add(MMS_INIT_TIMEOUT_SEC*1000,
            mms_connman_nemo_wait_timeout, loop);
        gulong valid_id = ofonoext_mm_add_valid_changed_handler(self->mm,
            mms_connman_nemo_wait_valid_changed, loop);
        g_main_loop_run(loop);
        g_main_loop_unref(loop);
        g_source_remove(timeout_id);
        ofonoext_mm_remove_handler(self->mm, valid_id);
        return self->mm->valid;
    }
}

/**
 * Returns IMSI of the default (voice) SIM
 */
static
char*
mms_connman_nemo_default_imsi(
    MMSConnMan* cm)
{
    MMSConnManNemo* self = MMS_CONNMAN_NEMO(cm);
    if (mms_connman_nemo_mm_valid(self)) {
        return g_strdup(self->mm->voice_imsi);
    }
    return NULL;
}

/**
 * MMSConnection destroy notification.
 */
static
void
mms_connman_nemo_connection_weak_ref_notify(
    gpointer arg,
    GObject* connection)
{
    MMSConnManNemo* self = MMS_CONNMAN_NEMO(arg);
    MMS_ASSERT(MMS_CONNECTION(connection) == self->conn);
    self->conn = NULL;
}

/**
 * Creates a new connection or returns the reference to an aready active one.
 * The caller must release the reference. Returns NULL if the modem is offline
 * and the network task should fail immediately.
 */
static
MMSConnection*
mms_connman_nemo_open_connection(
    MMSConnMan* cm,
    const char* imsi,
    gboolean user_request)
{
    MMSConnManNemo* self = MMS_CONNMAN_NEMO(cm);
    if (self->conn) {
        if (!g_strcmp0(self->conn->imsi, imsi)) {
            return mms_connection_ref(self->conn);
        } else {
            g_object_weak_unref(G_OBJECT(self->conn),
                mms_connman_nemo_connection_weak_ref_notify, self);
            self->conn = NULL;
        }
    }
    self->conn = mms_connection_nemo_new(cm, imsi, user_request);
    g_object_weak_ref(G_OBJECT(self->conn),
        mms_connman_nemo_connection_weak_ref_notify, self);
    return self->conn;
}

/**
 * Creates connection manager
 */
MMSConnMan*
mms_connman_nemo_new()
{
    MMSConnManNemo* self = g_object_new(MMS_TYPE_CONNMAN_NEMO, NULL);
    self->mm = ofonoext_mm_new();
    return &self->cm;
}

/**
 * Final stage of deinitialization
 */
static
void
mms_connman_nemo_finalize(
    GObject* object)
{
    MMSConnManNemo* self = MMS_CONNMAN_NEMO(object);
    MMS_VERBOSE_("");
    ofonoext_mm_unref(self->mm);
    G_OBJECT_CLASS(mms_connman_nemo_parent_class)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
mms_connman_nemo_class_init(
    MMSConnManNemoClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    klass->fn_default_imsi = mms_connman_nemo_default_imsi;
    klass->fn_open_connection = mms_connman_nemo_open_connection;
    object_class->finalize = mms_connman_nemo_finalize;
}

/**
 * Per instance initializer
 */
static
void
mms_connman_nemo_init(
    MMSConnManNemo* self)
{
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
