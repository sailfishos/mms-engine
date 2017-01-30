/*
 * Copyright (C) 2013-2017 Jolla Ltd.
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

#include "mms_task.h"
#include "mms_util.h"
#include "mms_codec.h"
#include "mms_handler.h"
#include "mms_file_util.h"
#include "mms_transfer_list.h"

/* Logging */
#define GLOG_MODULE_NAME mms_task_notification_log
#include "mms_lib_log.h"
#include "mms_error.h"
GLOG_MODULE_DEFINE2("mms-task-notification", MMS_TASK_LOG);

/* Class definition */
typedef MMSTaskClass MMSTaskNotificationClass;
typedef struct mms_task_notification {
    MMSTask task;
    MMSPdu* pdu;
    GBytes* push;
    MMSHandlerMessageNotifyCall* notify;
    MMSTransferList* transfers;
} MMSTaskNotification;

G_DEFINE_TYPE(MMSTaskNotification, mms_task_notification, MMS_TYPE_TASK)
#define MMS_TYPE_TASK_NOTIFICATION (mms_task_notification_get_type())
#define MMS_TASK_NOTIFICATION(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
   MMS_TYPE_TASK_NOTIFICATION, MMSTaskNotification))

/**
 * Writes the datagram to a file in the message directory.
 */
static
gboolean
mms_task_notification_write_file(
    MMSTaskNotification* ind,
    const char* file)
{
    char* dir;
    gboolean ok;
    mms_task_make_id(&ind->task);
    dir = mms_task_dir(&ind->task);
    ok = mms_write_bytes(dir, file, ind->push, NULL);
    g_free(dir);
    return ok;
}

/**
 * Rejects the notification
 */
static
void
mms_task_notification_reject(
    MMSTaskNotification* ind)
{
    mms_task_make_id(&ind->task);
    mms_task_queue_and_unref(ind->task.delegate,
        mms_task_notifyresp_new(&ind->task, ind->transfers,
            ind->pdu->transaction_id, MMS_MESSAGE_NOTIFY_STATUS_REJECTED));
}

/**
 * Handles response from the MMS handler. Non-empty message id means
 * that we start download the message immediately, empty string means that
 * download is postponed, NULL id means that an error has occured.
 */
static
void
mms_task_notification_done(
    MMSHandlerMessageNotifyCall* notify,
    const char* id,
    void* param)
{
    MMSTaskNotification* ind = MMS_TASK_NOTIFICATION(param);
    MMSTask* task = &ind->task;
    GASSERT(ind->notify == notify);
    ind->notify = NULL;
    if (id) {
        if (id[0]) {
            GDEBUG("  Database id: %s", id);
            if (task->id) {
                char* olddir = mms_task_dir(task);
                char* file = g_strconcat(olddir, "/"
                    MMS_NOTIFICATION_IND_FILE, NULL);
                /* Replace fake id with the real one */
                g_free(task->id);
                task->id = g_strdup(id);
                if (task_config(task)->keep_temp_files) {
                    /* Move file to the new place */
                    char* newdir = mms_task_dir(task);
                    if (rename(olddir, newdir) == 0) {
                        GVERBOSE("Moved %s to %s", file, newdir);
                    } else {
                        GERR("Failed to rename %s to %s: %s", olddir,
                            newdir, strerror(errno));
                    }
                    g_free(newdir);
                } else {
                    /* Remove temporary directory and file */
                    remove(file);
                    remove(olddir);
                }
                g_free(file);
                g_free(olddir);
            } else {
                task->id = g_strdup(id);
            }

            /* Schedule the download task */
            if (!mms_task_queue_and_unref(task->delegate,
                mms_task_retrieve_new(task->settings, task->handler,
                ind->transfers, task->id, task->imsi, ind->pdu,
                FALSE, NULL))) {
                mms_handler_message_receive_state_changed(task->handler, id,
                    MMS_RECEIVE_STATE_DOWNLOAD_ERROR);
            }
        }
        mms_task_set_state(task, MMS_TASK_STATE_DONE);
    } else if (!mms_task_retry(task)) {
        mms_task_notification_reject(ind);
    }
    mms_task_unref(task);
}

/**
 * Handles M-Notification.ind PDU
 */
static
void
mms_task_notification_ind(
    MMSTaskNotification* ind)
{
    MMSTask* task = &ind->task;
    const struct mms_notification_ind* ni = &ind->pdu->ni;

#if GUTIL_LOG_DEBUG
    char expiry[128];
    strftime(expiry, sizeof(expiry), "%Y-%m-%dT%H:%M:%S%z",
        localtime(&ni->expiry));
    expiry[sizeof(expiry)-1] = '\0';

    GDEBUG("Processing M-Notification.ind");
    GDEBUG("  From: %s", ni->from);
    if (ni->subject) GDEBUG("  Subject: %s", ni->subject);
    GDEBUG("  Size: %d bytes", ni->size);
    GDEBUG("  Location: %s", ni->location);
    GDEBUG("  Expiry: %s", expiry);
#endif /* GUTIL_LOG_DEBUG */

    if (task->deadline > ni->expiry) {
        task->deadline = ni->expiry;
    }

    GASSERT(!ind->notify);
    mms_task_ref(task);
    ind->notify = mms_handler_message_notify(task->handler, task->imsi,
        mms_strip_address_type(ni->from), ni->subject, ni->expiry,
        ind->push, ni->location, mms_task_notification_done, ind);

    if (ind->notify) {
        mms_task_set_state(task, MMS_TASK_STATE_PENDING);
    } else {
        mms_task_unref(task);
        if (!mms_task_retry(task)) mms_task_notification_reject(ind);
    }

    if (task_config(task)->keep_temp_files) {
        mms_task_notification_write_file(ind, MMS_NOTIFICATION_IND_FILE);
    }
}

/**
 * Handles M-Delivery.ind PDU
 */
static
void
mms_task_delivery_ind(
    MMSTaskNotification* ind)
{
    MMS_DELIVERY_STATUS ds;
    MMSTask* task = &ind->task;
    const struct mms_delivery_ind* di = &ind->pdu->di;
    const char* to = mms_strip_address_type(di->to);
    GDEBUG("Processing M-Delivery.ind");
    GDEBUG("  MMS message id: %s", di->msgid);
    GDEBUG("  Recipient: %s", to);
    switch (di->dr_status) {
    case MMS_MESSAGE_DELIVERY_STATUS_EXPIRED:
        ds = MMS_DELIVERY_STATUS_EXPIRED;
        break;
    case MMS_MESSAGE_DELIVERY_STATUS_RETRIEVED:
        ds = MMS_DELIVERY_STATUS_RETRIEVED;
        break;
    case MMS_MESSAGE_DELIVERY_STATUS_REJECTED:
        ds = MMS_DELIVERY_STATUS_REJECTED;
        break;
    case MMS_MESSAGE_DELIVERY_STATUS_DEFERRED:
        ds = MMS_DELIVERY_STATUS_DEFERRED;
        break;
    case MMS_MESSAGE_DELIVERY_STATUS_UNRECOGNISED:
        ds = MMS_DELIVERY_STATUS_UNRECOGNISED;
        break;
    case MMS_MESSAGE_DELIVERY_STATUS_FORWARDED:
        ds = MMS_DELIVERY_STATUS_FORWARDED;
        break;
    case MMS_MESSAGE_DELIVERY_STATUS_UNREACHABLE:
        ds = MMS_DELIVERY_STATUS_UNREACHABLE;
        break;
    case MMS_MESSAGE_DELIVERY_STATUS_INDETERMINATE:
    default:
        ds = MMS_DELIVERY_STATUS_UNKNOWN;
        break;
    }
    mms_handler_delivery_report(task->handler, task->imsi, di->msgid, to, ds);
    if (task_config(task)->keep_temp_files) {
        mms_task_notification_write_file(ind, MMS_DELIVERY_IND_FILE);
    }
}

/**
 * Handles M-Read-Orig.ind PDU
 */
static
void
mms_task_read_orig_ind(
    MMSTaskNotification* ind)
{
    MMS_READ_STATUS rs;
    MMSTask* task = &ind->task;
    const struct mms_read_ind* ri = &ind->pdu->ri;
    const char* to = mms_strip_address_type(ri->to);
    GDEBUG("Processing M-Read-Orig.ind");
    GDEBUG("  MMS message id: %s", ri->msgid);
    GDEBUG("  Recipient: %s", to);
    switch (ri->rr_status) {
    case MMS_MESSAGE_READ_STATUS_READ:
        rs = MMS_READ_STATUS_READ;
        break;
    case MMS_MESSAGE_READ_STATUS_DELETED:
        rs = MMS_READ_STATUS_DELETED;
        break;
    default:
        rs = MMS_READ_STATUS_INVALID;
        break;
    }
    mms_handler_read_report(task->handler, task->imsi, ri->msgid, to, rs);
    if (task_config(task)->keep_temp_files) {
        mms_task_notification_write_file(ind,  MMS_READ_ORIG_IND_FILE);
    }
}

/**
 * Handles unrecognized PDU
 */
static
void
mms_task_notification_unrecornized(
    const MMSConfig* config,
    GBytes* push)
{
    if (config->attic_enabled) {
        char* attic_dir = NULL;
        int i;
        for (i=0; i<100; i++) {
            g_free(attic_dir);
            attic_dir = g_strdup_printf("%s/" MMS_ATTIC_DIR "/%03d",
                config->root_dir, i);
            if (!g_file_test(attic_dir, G_FILE_TEST_IS_DIR)) break;
        }
        mms_write_bytes(attic_dir, MMS_UNRECOGNIZED_PUSH_FILE, push, NULL);
        g_free(attic_dir);
    }
}

/**
 * Runs the task
 */
static
void
mms_task_notification_run(
    MMSTask* task)
{
    MMSTaskNotification* ind = MMS_TASK_NOTIFICATION(task);
    switch (ind->pdu->type) {
    case MMS_MESSAGE_TYPE_NOTIFICATION_IND:
        mms_task_notification_ind(ind);
        break;
    case MMS_MESSAGE_TYPE_DELIVERY_IND:
        mms_task_delivery_ind(ind);
        break;
    case MMS_MESSAGE_TYPE_READ_ORIG_IND:
        mms_task_read_orig_ind(ind);
        break;
    default:
        GINFO("Ignoring MMS push PDU of type %u", ind->pdu->type);
        mms_task_notification_unrecornized(task_config(task), ind->push);
        break;
    }
    if (task->state == MMS_TASK_STATE_READY) {
        mms_task_set_state(task, MMS_TASK_STATE_DONE);
    }
}

/**
 * Cancels the task
 */
static
void
mms_task_notification_cancel(
    MMSTask* task)
{
    MMSTaskNotification* ind = MMS_TASK_NOTIFICATION(task);
    if (ind->notify) {
        mms_handler_message_notify_cancel(task->handler, ind->notify);
        ind->notify = NULL;
        mms_task_unref(task);
    }
    MMS_TASK_CLASS(mms_task_notification_parent_class)->fn_cancel(task);
}

/**
 * Final stage of deinitialization
 */
static
void
mms_task_notification_finalize(
    GObject* object)
{
    MMSTaskNotification* ind = MMS_TASK_NOTIFICATION(object);
    GASSERT(!ind->notify);
    g_bytes_unref(ind->push);
    mms_message_free(ind->pdu);
    mms_transfer_list_unref(ind->transfers);
    G_OBJECT_CLASS(mms_task_notification_parent_class)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
mms_task_notification_class_init(
    MMSTaskNotificationClass* klass)
{
    klass->fn_run = mms_task_notification_run;
    klass->fn_cancel = mms_task_notification_cancel;
    G_OBJECT_CLASS(klass)->finalize = mms_task_notification_finalize;
}

/**
 * Per instance initializer
 */
static
void
mms_task_notification_init(
    MMSTaskNotification* notification)
{
}

/* Create MMS notification task */
MMSTask*
mms_task_notification_new(
    MMSSettings* settings,
    MMSHandler* handler,
    MMSTransferList* transfers,
    const char* imsi,
    GBytes* bytes,
    GError** error)
{
    MMSPdu* pdu = mms_decode_bytes(bytes);
    GASSERT(!error || !(*error));
    if (pdu) {
        MMSTaskNotification* ind;

        /* Looks like a legitimate MMS Push PDU */
#if GUTIL_LOG_DEBUG
        GDEBUG("  MMS version: %u.%u", (pdu->version & 0x70) >> 4,
            pdu->version & 0x0f);
        if (pdu->transaction_id) {
            GDEBUG("  MMS transaction id: %s", pdu->transaction_id);
        }
#endif /* GUTIL_LOG_DEBUG */

        ind = mms_task_alloc(MMS_TYPE_TASK_NOTIFICATION,
            settings, handler, "Notification", NULL, imsi);
        ind->transfers = mms_transfer_list_ref(transfers);
        ind->push = g_bytes_ref(bytes);
        ind->pdu = pdu;
        return &ind->task;
    } else {
        MMS_ERROR(error, MMS_LIB_ERROR_DECODE, "Failed to decode MMS PDU");
        mms_task_notification_unrecornized(settings->config, bytes);
        return NULL;
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
