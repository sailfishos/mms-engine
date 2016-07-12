/*
 * Copyright (C) 2013-2016 Jolla Ltd.
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

#include "mms_connman.h"

/* Logging */
#define MMS_LOG_MODULE_NAME mms_connman_log
#include <gutil_log.h>

G_DEFINE_ABSTRACT_TYPE(MMSConnMan, mms_connman, G_TYPE_OBJECT)
#define MMS_CONNMAN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
    MMS_TYPE_CONNMAN, MMSConnMan))
#define MMS_CONNMAN_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), \
    MMS_TYPE_CONNMAN, MMSConnManClass))

enum {
    MMS_CONNMAN_SIGNAL_DONE,
    MMS_CONNMAN_SIGNAL_COUNT
};

#define MMS_CONNMAN_SIGNAL_DONE_NAME "connman-done"

static guint mms_connman_sig[MMS_CONNMAN_SIGNAL_COUNT] = { 0 };

MMSConnMan*
mms_connman_ref(
    MMSConnMan* cm)
{
    if (cm) g_object_ref(MMS_CONNMAN(cm));
    return cm;
}

void
mms_connman_unref(
    MMSConnMan* cm)
{
    if (cm) g_object_unref(MMS_CONNMAN(cm));
}

gulong
mms_connman_add_done_callback(
    MMSConnMan* cm,
    mms_connman_event_fn fn,
    void* param)
{
    if (cm && fn) {
        return g_signal_connect_data(cm, MMS_CONNMAN_SIGNAL_DONE_NAME,
            G_CALLBACK(fn), param, NULL, 0);
    }
    return 0;
}

void
mms_connman_remove_callback(
    MMSConnMan* cm,
    gulong handler_id)
{
    if (cm && handler_id) g_signal_handler_disconnect(cm, handler_id);
}

static
void
mms_connman_finalize(
    GObject* object)
{
    GASSERT(!MMS_CONNMAN(object)->busy);
    G_OBJECT_CLASS(mms_connman_parent_class)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
mms_connman_class_init(
    MMSConnManClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = mms_connman_finalize;
    mms_connman_sig[MMS_CONNMAN_SIGNAL_DONE] =
        g_signal_new(MMS_CONNMAN_SIGNAL_DONE_NAME,
            G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST,
            0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/**
 * Per instance initializer
 */
static
void
mms_connman_init(
    MMSConnMan* cm)
{
}

/**
 * Returns default (first available) IMSI or NULL if SIM is not present
 * or not configured. Caller must g_free() the returned string.
 */
char*
mms_connman_default_imsi(
    MMSConnMan* cm)
{
    if (cm) {
        MMSConnManClass* klass = MMS_CONNMAN_GET_CLASS(cm);
        if (klass->fn_default_imsi) {
            return klass->fn_default_imsi(cm);
        }
    }
    return NULL;
}

/**
 * Creates a new connection or returns the reference to an aready active one.
 * The caller must release the reference.
 */
MMSConnection*
mms_connman_open_connection(
    MMSConnMan* cm,
    const char* imsi,
    MMS_CONNECTION_TYPE type)
{
    if (cm) {
        MMSConnManClass* klass = MMS_CONNMAN_GET_CLASS(cm);
        if (klass->fn_open_connection) {
            return klass->fn_open_connection(cm, imsi, type);
        }
    }
    return NULL;
}

void
mms_connman_busy_update(
    MMSConnMan* cm,
    int change)
{
    GASSERT(change);
    if (cm && change) {
        cm->busy += change;
        GVERBOSE("busy count %d", cm->busy);
        GASSERT(cm->busy >= 0);
        if (cm->busy < 1) {
            g_signal_emit(cm, mms_connman_sig[MMS_CONNMAN_SIGNAL_DONE], 0);
        }
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
