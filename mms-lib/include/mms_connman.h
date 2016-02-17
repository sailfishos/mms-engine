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
 *
 */

#ifndef JOLLA_MMS_CONNMAN_H
#define JOLLA_MMS_CONNMAN_H

#include "mms_lib_types.h"

/* Instance */
struct mms_connman {
    GObject object;
    int busy;
};

/* Class */
typedef struct mms_connman_class {
    GObjectClass parent;
    char* (*fn_default_imsi)(MMSConnMan* cm);
    MMSConnection* (*fn_open_connection)(MMSConnMan* cm, const char* imsi,
        MMS_CONNECTION_TYPE type);
} MMSConnManClass;

GType mms_connman_get_type(void);
#define MMS_TYPE_CONNMAN (mms_connman_get_type())

/* Connman event callback */
typedef void
(*mms_connman_event_fn)(
    MMSConnMan* cm,
    void* param);

/* Reference counting */
MMSConnMan*
mms_connman_ref(
    MMSConnMan* cm);

void
mms_connman_unref(
    MMSConnMan* cm);

/**
 * Returns default (first available) IMSI or NULL if SIM is not present
 * or not configured. Caller must g_free() the returned string.
 */
char*
mms_connman_default_imsi(
    MMSConnMan* cm);

/**
 * Creates a new connection or returns the reference to an aready active one.
 * The caller must release the reference.
 */
MMSConnection*
mms_connman_open_connection(
    MMSConnMan* cm,
    const char* imsi,
    MMS_CONNECTION_TYPE type);

/**
 * While busy flags is on, mms-engine shouldn't exit
 */
void
mms_connman_busy_update(
    MMSConnMan* cm,
    int change);

gulong
mms_connman_add_done_callback(
    MMSConnMan* cm,
    mms_connman_event_fn fn,
    void* param);

void
mms_connman_remove_callback(
    MMSConnMan* cm,
    gulong handler_id);

#define mms_connman_busy(cm) ((cm) && ((cm)->busy > 0))
#define mms_connman_busy_inc(cm) mms_connman_busy_update(cm,1)
#define mms_connman_busy_dec(cm) mms_connman_busy_update(cm,-1)

#endif /* JOLLA_MMS_CONNMAN_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
