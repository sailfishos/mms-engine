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
#define GLOG_MODULE_NAME mms_task_send_log
#include "mms_lib_log.h"
#include "mms_error.h"
GLOG_MODULE_DEFINE2("mms-task-send", MMS_TASK_HTTP_LOG);

/* Class definition */
typedef MMSTaskHttpClass MMSTaskSendClass;
typedef MMSTaskHttp MMSTaskSend;

G_DEFINE_TYPE(MMSTaskSend, mms_task_send, MMS_TYPE_TASK_HTTP)
#define MMS_TYPE_TASK_SEND (mms_task_send_get_type())
#define MMS_TASK_SEND(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
   MMS_TYPE_TASK_SEND, MMSTaskSend))

static
void
mms_task_send_started(
    MMSTaskHttp* http)
{
    mms_handler_message_send_state_changed(http->task.handler,
        http->task.id, MMS_SEND_STATE_SENDING, NULL);
}

static
void
mms_task_send_paused(
    MMSTaskHttp* http)
{
    mms_handler_message_send_state_changed(http->task.handler,
        http->task.id, MMS_SEND_STATE_DEFERRED, NULL);
}

static
void
mms_task_send_done(
    MMSTaskHttp* http,
    const char* path,
    SoupStatus status)
{
    MMSPdu* pdu = NULL;
    MMS_SEND_STATE state = MMS_SEND_STATE_SEND_ERROR;
    const char* msgid = NULL;
    const char* details = NULL;
    if (SOUP_STATUS_IS_SUCCESSFUL(status)) {
        /* Decode the result */
        GError* error = NULL;
        GMappedFile* map = g_mapped_file_new(path, FALSE, &error);
        if (map) {
            const void* data = g_mapped_file_get_contents(map);
            const gsize len = g_mapped_file_get_length(map);
            pdu = g_new0(MMSPdu, 1);
            if (mms_message_decode(data, len, pdu)) {
                if (pdu &&
                    pdu->type == MMS_MESSAGE_TYPE_SEND_CONF) {
                    if (pdu->sc.rsp_status == MMS_MESSAGE_RSP_STATUS_OK) {
                        if (pdu->sc.msgid && pdu->sc.msgid[0]) {
                            msgid = pdu->sc.msgid;
                            GINFO("Message ID %s", pdu->sc.msgid);
                        } else {
                            GERR("Missing Message-ID");
                        }
                    } else {
                        GERR("MMSC responded with %u", pdu->sc.rsp_status);
                        details = pdu->sc.rsp_text;
                        switch (pdu->sc.rsp_status) {
                        case MMS_MESSAGE_RSP_STATUS_ERR_SERVICE_DENIED:
                        case MMS_MESSAGE_RSP_STATUS_ERR_CONTENT_NOT_ACCEPTED:
                        case MMS_MESSAGE_RSP_STATUS_ERR_UNSUPPORTED_MESSAGE:
                        case MMS_MESSAGE_RSP_STATUS_ERR_PERM_SERVICE_DENIED:
                        case MMS_MESSAGE_RSP_STATUS_ERR_PERM_LACK_OF_PREPAID:
                        case MMS_MESSAGE_RSP_STATUS_ERR_PERM_CONTENT_NOT_ACCEPTED:
                            state = MMS_SEND_STATE_REFUSED;
                            break;
                        default:
                            break;
                        }
                    }
                } else {
                    GERR("Unexpected response from MMSC");
                }
            } else {
                GERR("Failed to parse MMSC response");
            }
            g_mapped_file_unref(map);
        } else {
            GERR("%s", GERRMSG(error));
            g_error_free(error);
        }
    }
    if (msgid) {
        mms_handler_message_sent(http->task.handler, http->task.id, msgid);
    } else {
        mms_handler_message_send_state_changed(http->task.handler,
            http->task.id, state, details);
    }
    if (pdu) mms_message_free(pdu);
}

/**
 * Per class initializer
 */
static
void
mms_task_send_class_init(
    MMSTaskSendClass* klass)
{
    klass->fn_started = mms_task_send_started;
    klass->fn_paused = mms_task_send_paused;
    klass->fn_done = mms_task_send_done;
}

/**
 * Per instance initializer
 */
static
void
mms_task_send_init(
    MMSTaskSend* send)
{
}

/* Create MMS send task */
MMSTask*
mms_task_send_new(
    MMSTask* parent,
    MMSTransferList* transfers)
{
    return mms_task_http_alloc_with_parent(MMS_TYPE_TASK_SEND, parent,
        transfers, MMS_TRANSFER_TYPE_SEND, NULL, MMS_SEND_CONF_FILE,
        MMS_SEND_REQ_FILE);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
