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

#ifndef JOLLA_MMS_TASK_H
#define JOLLA_MMS_TASK_H

#include "mms_settings.h"

/* Claim MMS 1.1 support */
#define MMS_VERSION MMS_MESSAGE_VERSION_1_1

/* mms_codec.h */
typedef enum mms_message_notify_status MMSNotifyStatus;

/* Task state */
typedef enum _MMS_TASK_STATE {
    MMS_TASK_STATE_READY,                /* Ready to run */
    MMS_TASK_STATE_NEED_CONNECTION,      /* Network connection us needed */
    MMS_TASK_STATE_NEED_USER_CONNECTION, /* Connection requested by user */
    MMS_TASK_STATE_TRANSMITTING,         /* Sending or receiving the data */
    MMS_TASK_STATE_WORKING,              /* Active but not using network */
    MMS_TASK_STATE_PENDING,              /* Waiting for something */
    MMS_TASK_STATE_SLEEP,                /* Sleeping, will wake up */
    MMS_TASK_STATE_DONE,                 /* Nothing left to do */
    MMS_TASK_STATE_COUNT                 /* Number of valid states */
} MMS_TASK_STATE;

/*
 * Transfer types which double as task names. These are part of the D-Bus API,
 * don't change them just because you don't like them
 */
#define MMS_TRANSFER_TYPE_ACK           "Ack"
#define MMS_TRANSFER_TYPE_NOTIFY_RESP   "NotifyResp"
#define MMS_TRANSFER_TYPE_READ_REPORT   "ReadReport"
#define MMS_TRANSFER_TYPE_RETRIEVE      "Retrieve"
#define MMS_TRANSFER_TYPE_SEND          "Send"

/* Task priority */
typedef enum _MMS_TASK_PRIORITY {
    MMS_TASK_PRIORITY_NORMAL,            /* Default priority */
    MMS_TASK_PRIORITY_POST_PROCESS       /* Post-processing priority */
} MMS_TASK_PRIORITY;

/* Delegate (one per task) */
typedef struct mms_task MMSTask;
typedef struct mms_task_priv MMSTaskPriv;
typedef struct mms_task_delegate MMSTaskDelegate;
struct mms_task_delegate {
    /* Submits new task to the queue */
    void (*fn_task_queue)(
        MMSTaskDelegate* delegate,
        MMSTask* task);
    /* Task has changed its state */
    void (*fn_task_state_changed)(
        MMSTaskDelegate* delegate,
        MMSTask* task);
};

/* Task object */
struct mms_task {
    GObject parent;                      /* Parent object */
    MMSTaskPriv* priv;                   /* Private data */
    MMS_TASK_PRIORITY priority;          /* Task priority */
    int order;                           /* Task creation order */
    char* name;                          /* Task name for debug purposes */
    char* id;                            /* Database record ID */
    char* imsi;                          /* Associated subscriber identity */
    MMSSettings* settings;               /* Settings */
    MMSHandler* handler;                 /* Message database interface */
    MMSTaskDelegate* delegate;           /* Observer */
    MMS_TASK_STATE state;                /* Task state */
    time_t deadline;                     /* Task deadline */
    int flags;                           /* Flags: */

#define MMS_TASK_FLAG_CANCELLED (0x01)   /* Task has been cancelled */

};

typedef struct mms_task_class {
    GObjectClass parent;
    time_t max_lifetime;                 /* Maximum lifetime, in seconds */
    /* Invoked in IDLE/RETRY state to get the task going */
    void (*fn_run)(MMSTask* task);
    /* Invoked in NEED_[USER_]CONNECTION state */
    void (*fn_transmit)(MMSTask* task, MMSConnection* conn);
    /* Invoked in NEED_[USER_]CONNECTION or TRANSMITTING state */
    void (*fn_network_unavailable)(MMSTask* task, gboolean can_retry);
    /* May be invoked in any state */
    void (*fn_cancel)(MMSTask* task);
} MMSTaskClass;

GType mms_task_get_type(void);
#define MMS_TASK_LOG mms_task_log
#define MMS_TYPE_TASK (mms_task_get_type())
#define MMS_TASK_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), \
        MMS_TYPE_TASK, MMSTaskClass))

void*
mms_task_alloc(
    GType type,
    MMSSettings* settings,
    MMSHandler* handler,
    const char* name,
    const char* id,
    const char* imsi);

MMSTask*
mms_task_ref(
    MMSTask* task);

void
mms_task_unref(
    MMSTask* task);

void
mms_task_run(
    MMSTask* task);

void
mms_task_transmit(
    MMSTask* task,
    MMSConnection* connection);

void
mms_task_network_unavailable(
    MMSTask* task,
    gboolean can_retry);

void
mms_task_cancel(
    MMSTask* task);

void
mms_task_set_state(
    MMSTask* task,
    MMS_TASK_STATE state);

gboolean
mms_task_sleep(
    MMSTask* task,
    unsigned int secs);

#define mms_task_retry(task) \
    mms_task_sleep(task, 0)

/* Utilities */
const char*
mms_task_state_name(
    MMS_TASK_STATE state);

gboolean
mms_task_queue_and_unref(
    MMSTaskDelegate* delegate,
    MMSTask* task);

gboolean
mms_task_match_id(
    MMSTask* task,
    const char* id);

const char*
mms_task_make_id(
    MMSTask* task);

const MMSSettingsSimData*
mms_task_sim_settings(
    MMSTask* task);

#define task_config(task) \
    ((task)->settings->config)

/* Create particular types of tasks */
MMSTask*
mms_task_notification_new(
    MMSSettings* settings,
    MMSHandler* handler,
    MMSTransferList* transfers,
    const char* imsi,
    GBytes* bytes,
    GError** error);

MMSTask*
mms_task_retrieve_new(
    MMSSettings* settings,
    MMSHandler* handler,
    MMSTransferList* transfers,
    const char* id,
    const char* imsi,
    const MMSPdu* pdu,
    MMS_CONNECTION_TYPE ct,
    GError** error);

MMSTask*
mms_task_decode_new(
    MMSTask* parent,
    MMSTransferList* transfers,
    const char* transaction_id,
    const char* file);

MMSTask*
mms_task_notifyresp_new(
    MMSTask* parent,
    MMSTransferList* transfers,
    const char* transaction_id,
    MMSNotifyStatus status);

MMSTask*
mms_task_ack_new(
    MMSTask* parent,
    MMSTransferList* transfers,
    const char* transaction_id);

MMSTask*
mms_task_read_new(
    MMSSettings* settings,
    MMSHandler* handler,
    MMSTransferList* transfers,
    const char* id,
    const char* imsi,
    const char* message_id,
    const char* to,
    MMSReadStatus status,
    GError** error);

MMSTask*
mms_task_publish_new(
    MMSSettings* settings,
    MMSHandler* handler,
    MMSMessage* msg);

MMSTask*
mms_task_encode_new(
    MMSSettings* settings,
    MMSHandler* handler,
    MMSTransferList* transfers,
    const char* id,
    const char* imsi,
    const char* to,
    const char* cc,
    const char* bcc,
    const char* subject,
    int flags,
    const MMSAttachmentInfo* parts,
    int nparts,
    GError** error);

MMSTask*
mms_task_send_new(
    MMSTask* parent,
    MMSTransferList* transfers);

#endif /* JOLLA_MMS_TASK_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
