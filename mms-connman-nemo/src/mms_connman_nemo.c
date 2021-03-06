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
 */

#include "mms_connman_nemo.h"
#include "mms_connection_nemo.h"

#include <gofono_simmgr.h>
#include <gofono_modem.h>

/* Logging */
#define GLOG_MODULE_NAME mms_connman_log
#include "mms_connman_nemo_log.h"
#include <gutil_log.h>
GLOG_MODULE_DEFINE("mms-connman-nemo");

enum mm_event {
    MM_EVENT_VALID,
    MM_EVENT_VOICE_MODEM,
    MM_EVENT_COUNT
};

typedef MMSConnManClass MMSConnManNemoClass;
typedef struct mms_connman_nemo {
    MMSConnMan cm;
    MMSConnection* conn;
    OfonoExtModemManager* mm;
    OfonoSimMgr* default_sim;
    gulong mm_event_id[MM_EVENT_COUNT];
    gulong conn_change_id;
} MMSConnManNemo;

G_DEFINE_TYPE(MMSConnManNemo, mms_connman_nemo, MMS_TYPE_CONNMAN)
#define MMS_TYPE_CONNMAN_NEMO (mms_connman_nemo_get_type())
#define MMS_CONNMAN_NEMO(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
    MMS_TYPE_CONNMAN_NEMO, MMSConnManNemo))

#define MMS_INIT_TIMEOUT_MS (30*1000)

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

static
void
mms_connman_nemo_check_default_sim(
    MMSConnManNemo* self)
{
    const char* path = NULL;
    if (self->mm->valid && self->mm->voice_modem) {
        path = ofono_modem_path(self->mm->voice_modem);
    }
    if (g_strcmp0(path, ofono_simmgr_path(self->default_sim))) {
        ofono_simmgr_unref(self->default_sim);
        if (path) {
            GDEBUG("Default SIM at %s", path);
            self->default_sim = ofono_simmgr_new(path);
        } else {
            GDEBUG("No default SIM");
            self->default_sim = NULL;
        }
    }
}

static
void
mms_connman_nemo_check_default_sim_cb(
    OfonoExtModemManager* mm,
    void* data)
{
    mms_connman_nemo_check_default_sim(MMS_CONNMAN_NEMO(data));
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
        guint timeout_id = g_timeout_add(MMS_INIT_TIMEOUT_MS,
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
    if (mms_connman_nemo_mm_valid(self) && self->default_sim &&
        ofono_simmgr_wait_valid(self->default_sim, MMS_INIT_TIMEOUT_MS, 0) &&
        self->default_sim /* Check it again */ &&
        self->default_sim->imsi) {
        GDEBUG("Default IMSI %s", self->default_sim->imsi);
        return g_strdup(self->default_sim->imsi);
    }
    GDEBUG("No default IMSI");
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
    GASSERT(MMS_CONNECTION(connection) == self->conn);
    GVERBOSE_("%p", connection);
    self->conn_change_id = 0;
    self->conn = NULL;
}

/**
 * Drop current MMSConnection.
 */
static
void
mms_connman_nemo_drop_connection(
    MMSConnManNemo* self)
{
    if (self->conn) {
        GVERBOSE_("%p", self->conn);
        mms_connection_remove_handler(self->conn, self->conn_change_id);
        self->conn_change_id = 0;
        g_object_weak_unref(G_OBJECT(self->conn),
            mms_connman_nemo_connection_weak_ref_notify, self);
        self->conn = NULL;
    }
}

/**
 * MMSConnection state change notification.
 */
static
void
mms_connman_nemo_connection_state_changed(
    MMSConnection* connection,
    void* arg)
{
    MMSConnManNemo* self = MMS_CONNMAN_NEMO(arg);
    GASSERT(connection == self->conn);
    if (self->conn && !mms_connection_is_active(self->conn)) {
        mms_connman_nemo_drop_connection(self);
    }
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
    MMS_CONNECTION_TYPE type)
{
    MMSConnManNemo* self = MMS_CONNMAN_NEMO(cm);
    if (self->conn) {
        if (!g_strcmp0(self->conn->imsi, imsi)) {
            return mms_connection_ref(self->conn);
        } else {
            mms_connman_nemo_drop_connection(self);
        }
    }
    self->conn = mms_connection_nemo_new(cm, imsi, type);
    self->conn_change_id = mms_connection_add_state_change_handler(self->conn,
        mms_connman_nemo_connection_state_changed, self);
    GASSERT(mms_connection_is_active(self->conn));
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
    return g_object_new(MMS_TYPE_CONNMAN_NEMO, NULL);
}

/**
 * First stage of deinitialization (release all references).
 * May be called more than once in the lifetime of the object.
 */
static
void
mms_connman_nemo_dispose(
    GObject* object)
{
    MMSConnManNemo* self = MMS_CONNMAN_NEMO(object);
    if (self->default_sim) {
        ofono_simmgr_unref(self->default_sim);
        self->default_sim = NULL;
    }
    mms_connman_nemo_drop_connection(self);
    ofonoext_mm_remove_handlers(self->mm, self->mm_event_id,
        G_N_ELEMENTS(self->mm_event_id));
    G_OBJECT_CLASS(mms_connman_nemo_parent_class)->dispose(object);
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
    GVERBOSE_("");
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
    object_class->dispose = mms_connman_nemo_dispose;
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
    GVERBOSE_("");
    self->mm = ofonoext_mm_new();
    self->mm_event_id[MM_EVENT_VALID] =
        ofonoext_mm_add_valid_changed_handler(self->mm,
            mms_connman_nemo_check_default_sim_cb, self);
    self->mm_event_id[MM_EVENT_VOICE_MODEM] =
        ofonoext_mm_add_voice_modem_changed_handler(self->mm,
            mms_connman_nemo_check_default_sim_cb, self);
    mms_connman_nemo_check_default_sim(self);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
