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

#include "mms_task.h"
#include "mms_task_http.h"
#include "mms_file_util.h"
#include "mms_settings.h"
#include "mms_codec.h"

static
const char*
mms_task_ack_encode(
    const MMSConfig* config,
    const MMSSettingsSimData* settings,
    const char* id,
    const char* transaction_id)
{
    const char* result = NULL;
    const char* file = MMS_ACKNOWLEDGE_IND_FILE;
    char* dir = mms_message_dir(config, id);
    int fd = mms_create_file(dir, file, NULL, NULL);
    if (fd >= 0) {
        MMSPdu* pdu = g_new0(MMSPdu, 1);
        pdu->type = MMS_MESSAGE_TYPE_ACKNOWLEDGE_IND;
        pdu->version = MMS_VERSION;
        pdu->transaction_id = g_strdup(transaction_id);
        pdu->ai.report = settings ? settings->allow_dr :
            MMS_SETTINGS_DEFAULT_ALLOW_DR;
        if (mms_message_encode(pdu, fd)) result = file;
        mms_message_free(pdu);
        close(fd);
    }
    g_free(dir);
    return result;
}

/* Create MMS delivery acknowledgement task */
MMSTask*
mms_task_ack_new(
    MMSTask* parent,
    MMSTransferList* transfers,
    const char* tx_id)
{
    MMSTask* task = NULL;
    const char* file = mms_task_ack_encode(task_config(parent),
        mms_task_sim_settings(parent), parent->id, tx_id);
    if (file) {
        task = mms_task_http_alloc_with_parent(0, parent, transfers,
            MMS_TRANSFER_TYPE_ACK, NULL, NULL, file);
        task->priority = MMS_TASK_PRIORITY_POST_PROCESS;
    }
    return task;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
