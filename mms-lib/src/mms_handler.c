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

#include "mms_handler.h"

/* Logging */
#define GLOG_MODULE_NAME mms_handler_log
#include <gutil_log.h>

G_DEFINE_ABSTRACT_TYPE(MMSHandler, mms_handler, G_TYPE_OBJECT)

#define MMS_HANDLER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), MMS_TYPE_HANDLER, MMSHandler))
#define MMS_HANDLER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), MMS_TYPE_HANDLER, MMSHandlerClass))
#define MMS_HANDLER_GET_CLASS(obj)  \
    (G_TYPE_INSTANCE_GET_CLASS((obj), MMS_TYPE_HANDLER, MMSHandlerClass))

enum {
    MMS_HANDLER_SIGNAL_DONE,
    MMS_HANDLER_SIGNAL_COUNT
};

#define MMS_HANDLER_SIGNAL_DONE_NAME "mms-handler-done"

static guint mms_handler_signals[MMS_HANDLER_SIGNAL_COUNT] = { 0 };

static
void
mms_handler_finalize(
    GObject* object)
{
    GVERBOSE_("%p", object);
    GASSERT(!MMS_HANDLER(object)->busy);
    G_OBJECT_CLASS(mms_handler_parent_class)->finalize(object);
}

static
void
mms_handler_class_init(
    MMSHandlerClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    mms_handler_signals[MMS_HANDLER_SIGNAL_DONE] =
        g_signal_new(MMS_HANDLER_SIGNAL_DONE_NAME,
            G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST,
            0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    object_class->finalize = mms_handler_finalize;
}

static
void
mms_handler_init(
    MMSHandler* h)
{
    GVERBOSE_("%p", h);
}

MMSHandler*
mms_handler_ref(
    MMSHandler* h)
{
    if (G_LIKELY(h)) {
        GASSERT(MMS_HANDLER(h));
        g_object_ref(h);
    }
    return h;
}

void
mms_handler_unref(
    MMSHandler* h)
{
    if (G_LIKELY(h)) {
        GASSERT(MMS_HANDLER(h));
        g_object_unref(h);
    }
}

gulong
mms_handler_add_done_callback(
    MMSHandler* h,
    mms_handler_event_fn fn,
    void* param)
{
    if (G_LIKELY(h) && G_LIKELY(fn)) {
        return g_signal_connect_data(h, MMS_HANDLER_SIGNAL_DONE_NAME,
            G_CALLBACK(fn), param, NULL, 0);
    }
    return 0;
}

void
mms_handler_remove_callback(
    MMSHandler* h,
    gulong handler_id)
{
    if (G_LIKELY(h) && G_LIKELY(handler_id)) {
        g_signal_handler_disconnect(h, handler_id);
    }
}

MMSHandlerMessageNotifyCall*
mms_handler_message_notify(
    MMSHandler* h,
    const char* imsi,
    const char* from,
    const char* subject,
    time_t expiry,
    GBytes* push,
    mms_handler_message_notify_complete_fn cb,
    void* param)
{
    if (G_LIKELY(h)) {
        MMSHandlerClass* klass = MMS_HANDLER_GET_CLASS(h);
        if (klass->fn_message_notify) {
            if (!from) from = "";
            if (!subject) subject = "";
            return klass->fn_message_notify(h, imsi, from, subject, expiry,
                push, cb, param);
        }
    }
    return NULL;
}

void
mms_handler_message_notify_cancel(
    MMSHandler* h,
    MMSHandlerMessageNotifyCall* call)
{
    if (G_LIKELY(h) && G_LIKELY(call)) {
        MMSHandlerClass* klass = MMS_HANDLER_GET_CLASS(h);
        if (klass->fn_message_notify_cancel) {
            klass->fn_message_notify_cancel(h, call);
        }
    }
}

MMSHandlerMessageReceivedCall*
mms_handler_message_received(
    MMSHandler* h,
    MMSMessage* msg,
    mms_handler_message_received_complete_fn cb,
    void* param)
{
    if (G_LIKELY(h)) {
        MMSHandlerClass* klass = MMS_HANDLER_GET_CLASS(h);
        if (klass->fn_message_received) {
            return klass->fn_message_received(h, msg, cb, param);
        }
    }
    return NULL;
}

void
mms_handler_message_received_cancel(
    MMSHandler* h,
    MMSHandlerMessageReceivedCall* call)
{
    if (G_LIKELY(h) && G_LIKELY(call)) {
        MMSHandlerClass* klass = MMS_HANDLER_GET_CLASS(h);
        if (klass->fn_message_received_cancel) {
            klass->fn_message_received_cancel(h, call);
        }
    }
}

gboolean
mms_handler_message_receive_state_changed(
    MMSHandler* h,
    const char* id,
    MMS_RECEIVE_STATE state)
{
    if (G_LIKELY(h)) {
        MMSHandlerClass* klass = MMS_HANDLER_GET_CLASS(h);
        if (klass->fn_message_receive_state_changed) {
            return klass->fn_message_receive_state_changed(h, id, state);
        }
    }
    return FALSE;
}

gboolean
mms_handler_message_send_state_changed(
    MMSHandler* h,
    const char* id,
    MMS_SEND_STATE state,
    const char* details)
{
    if (G_LIKELY(h)) {
        MMSHandlerClass* klass = MMS_HANDLER_GET_CLASS(h);
        if (klass->fn_message_send_state_changed) {
            return klass->fn_message_send_state_changed(h, id, state, details);
        }
    }
    return FALSE;
}

gboolean
mms_handler_message_sent(
    MMSHandler* h,
    const char* id,
    const char* msgid)
{
    if (G_LIKELY(h)) {
        MMSHandlerClass* klass = MMS_HANDLER_GET_CLASS(h);
        if (klass->fn_message_sent) {
            return klass->fn_message_sent(h, id, msgid);
        }
    }
    return FALSE;
}

gboolean
mms_handler_delivery_report(
    MMSHandler* h,
    const char* imsi,
    const char* msgid,
    const char* recipient,
    MMS_DELIVERY_STATUS ds)
{
    if (G_LIKELY(h)) {
        MMSHandlerClass* klass = MMS_HANDLER_GET_CLASS(h);
        if (klass->fn_delivery_report) {
            return klass->fn_delivery_report(h, imsi, msgid, recipient, ds);
        }
    }
    return FALSE;
}

gboolean
mms_handler_read_report(
    MMSHandler* h,
    const char* imsi,
    const char* msgid,
    const char* recipient,
    MMS_READ_STATUS rs)
{
    if (G_LIKELY(h)) {
        MMSHandlerClass* klass = MMS_HANDLER_GET_CLASS(h);
        if (klass->fn_read_report) {
            return klass->fn_read_report(h, imsi, msgid, recipient, rs);
        }
    }
    return FALSE;
}

gboolean
mms_handler_read_report_send_status(
    MMSHandler* h,
    const char* id,
    MMS_READ_REPORT_STATUS status)
{
    if (G_LIKELY(h)) {
        MMSHandlerClass* klass = MMS_HANDLER_GET_CLASS(h);
        if (klass->fn_read_report_send_status) {
            return klass->fn_read_report_send_status(h, id, status);
        }
    }
    return FALSE;
}

void
mms_handler_busy_update(
    MMSHandler* h,
    int change)
{
    GASSERT(change);
    if (G_LIKELY(h) && G_LIKELY(change)) {
        h->busy += change;
        GASSERT(h->busy >= 0);
        if (h->busy < 1) {
            g_signal_emit(h, mms_handler_signals[MMS_HANDLER_SIGNAL_DONE], 0);
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
