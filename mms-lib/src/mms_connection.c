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

#include "mms_connection.h"

/* Logging */
#define GLOG_MODULE_NAME mms_connection_log
#include <gutil_log.h>

G_DEFINE_ABSTRACT_TYPE(MMSConnection, mms_connection, G_TYPE_OBJECT)
#define MMS_CONNECTION_GET_CLASS(obj)  \
    (G_TYPE_INSTANCE_GET_CLASS((obj), MMS_TYPE_CONNECTION, MMSConnectionClass))

enum mms_connection_signal {
    SIGNAL_STATE_CHANGED,
    SIGNAL_COUNT
};

#define SIGNAL_STATE_CHANGED_NAME    "state-changed"

static guint mms_connection_signals[SIGNAL_COUNT] = { 0 };

MMSConnection*
mms_connection_ref(
    MMSConnection* self)
{
    if (self) g_object_ref(MMS_CONNECTION(self));
    return self;
}

void
mms_connection_unref(
    MMSConnection* self)
{
    if (self) g_object_unref(MMS_CONNECTION(self));
}

const char*
mms_connection_state_name(
    MMSConnection* self)
{
    static const char* names[] = {"????","OPENING","FAILED","OPEN","CLOSED"};
    return names[mms_connection_state(self)];
}

MMS_CONNECTION_STATE
mms_connection_state(
    MMSConnection* self)
{
    return self ? self->state : MMS_CONNECTION_STATE_INVALID;
}

gulong
mms_connection_add_state_change_handler(
    MMSConnection* self,
    MMSConnectionStateChangeFunc fn,
    void* data)
{
    return (self && fn) ? g_signal_connect(self,SIGNAL_STATE_CHANGED_NAME,
        G_CALLBACK(fn), data) : 0;
}

void
mms_connection_signal_state_change(
    MMSConnection* self)
{
    if (self) {
        mms_connection_ref(self);
        g_signal_emit(self, mms_connection_signals[SIGNAL_STATE_CHANGED], 0);
        mms_connection_unref(self);
    }
}

void
mms_connection_remove_handler(
    MMSConnection* self,
    gulong id)
{
    if (self && id) {
        g_signal_handler_disconnect(self, id);
    }
}

void
mms_connection_close(
    MMSConnection* conn)
{
    if (conn) MMS_CONNECTION_GET_CLASS(conn)->fn_close(conn);
}

/**
 * Per instance initializer
 */
static
void
mms_connection_init(
    MMSConnection* self)
{
    GVERBOSE_("%p", self);
}

/**
 * Final stage of deinitialization
 */
static
void
mms_connection_finalize(
    GObject* object)
{
    GVERBOSE_("%p", object);
    G_OBJECT_CLASS(mms_connection_parent_class)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
mms_connection_class_init(
    MMSConnectionClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = mms_connection_finalize;
    mms_connection_signals[SIGNAL_STATE_CHANGED] =
        g_signal_new(SIGNAL_STATE_CHANGED_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
