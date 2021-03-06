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

#include "mms_task.h"
#include "mms_task_http.h"
#include "mms_file_util.h"
#include "mms_handler.h"
#include "mms_codec.h"

/* Logging */
#define GLOG_MODULE_NAME mms_task_retrieve_log
#include "mms_lib_log.h"
#include "mms_error.h"
GLOG_MODULE_DEFINE2("mms-task-retrieve", MMS_TASK_HTTP_LOG);

/* Class definition */
typedef MMSTaskHttpClass MMSTaskRetrieveClass;
typedef struct mms_task_retrieve {
    MMSTaskHttp http;
    char* transaction_id;
} MMSTaskRetrieve;

G_DEFINE_TYPE(MMSTaskRetrieve, mms_task_retrieve, MMS_TYPE_TASK_HTTP)
#define MMS_TYPE_TASK_RETRIEVE (mms_task_retrieve_get_type())
#define MMS_TASK_RETRIEVE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
   MMS_TYPE_TASK_RETRIEVE, MMSTaskRetrieve))

static
void
mms_task_retrieve_started(
    MMSTaskHttp* http)
{
    mms_handler_message_receive_state_changed(http->task.handler,
        http->task.id, MMS_RECEIVE_STATE_RECEIVING);
}

static
void
mms_task_retrieve_paused(
    MMSTaskHttp* http)
{
    mms_handler_message_receive_state_changed(http->task.handler,
        http->task.id, MMS_RECEIVE_STATE_DEFERRED);
}

static
void
mms_task_retrieve_done(
    MMSTaskHttp* http,
    const char* path,
    SoupStatus status)
{
    MMSTask* task = &http->task;
    MMSTaskRetrieve* retrieve = MMS_TASK_RETRIEVE(http);
    MMS_RECEIVE_STATE state =
        (SOUP_STATUS_IS_SUCCESSFUL(status) &&
         mms_task_queue_and_unref(task->delegate,
            mms_task_decode_new(task, http->transfers,
                retrieve->transaction_id, path))) ?
                    MMS_RECEIVE_STATE_DECODING :
                    MMS_RECEIVE_STATE_DOWNLOAD_ERROR;
    mms_handler_message_receive_state_changed(http->task.handler,
        http->task.id, state);
}

/**
 * Final stage of deinitialization
 */
static
void
mms_task_retrieve_finalize(
    GObject* object)
{
    MMSTaskRetrieve* retrieve = MMS_TASK_RETRIEVE(object);
    g_free(retrieve->transaction_id);
    G_OBJECT_CLASS(mms_task_retrieve_parent_class)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
mms_task_retrieve_class_init(
    MMSTaskRetrieveClass* klass)
{
    klass->fn_started = mms_task_retrieve_started;
    klass->fn_paused = mms_task_retrieve_paused;
    klass->fn_done = mms_task_retrieve_done;
    G_OBJECT_CLASS(klass)->finalize = mms_task_retrieve_finalize;
}

/**
 * Per instance initializer
 */
static
void
mms_task_retrieve_init(
    MMSTaskRetrieve* retrieve)
{
}

/* Create MMS retrieve task */
MMSTask*
mms_task_retrieve_new(
    MMSSettings* settings,
    MMSHandler* handler,
    MMSTransferList* transfers,
    const char* id,
    const char* imsi,
    const MMSPdu* pdu,
    MMS_CONNECTION_TYPE ct,
    GError** error)
{
    const time_t now = time(NULL);
    GASSERT(pdu);
    GASSERT(pdu->type == MMS_MESSAGE_TYPE_NOTIFICATION_IND);
    GASSERT(pdu->transaction_id);
    if (pdu->ni.expiry > now) {
        MMSTaskRetrieve* retrieve = mms_task_http_alloc(
            MMS_TYPE_TASK_RETRIEVE, settings, handler, transfers,
            MMS_TRANSFER_TYPE_RETRIEVE, id, imsi, pdu->ni.location,
            MMS_RETRIEVE_CONF_FILE, NULL, ct);
        if (retrieve->http.task.deadline > pdu->ni.expiry) {
            retrieve->http.task.deadline = pdu->ni.expiry;
        }
        retrieve->transaction_id = g_strdup(pdu->transaction_id);
        return &retrieve->http.task;
    } else {
        MMS_ERROR(error, MMS_LIB_ERROR_EXPIRED, "Message already expired");
    }
    return NULL;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
