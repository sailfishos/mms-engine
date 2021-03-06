/*
 * Copyright (C) 2013-2020 Jolla Ltd.
 * Copyright (C) 2013-2020 Slava Monich <slava.monich@jolla.com>
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
#include "mms_message.h"
#include "mms_file_util.h"
#include "mms_transfer_list.h"

/* Logging */
#define GLOG_MODULE_NAME mms_task_decode_log
#include "mms_lib_log.h"
#include <gutil_log.h>
GLOG_MODULE_DEFINE2("mms-task-decode", MMS_TASK_LOG);

/* Class definition */
typedef MMSTaskClass MMSTaskDecodeClass;
typedef struct mms_task_decode {
    MMSTask task;
    MMSTransferList* transfers;
    GMappedFile* map;
    char* transaction_id;
    char* file;
} MMSTaskDecode;

G_DEFINE_TYPE(MMSTaskDecode, mms_task_decode, MMS_TYPE_TASK)
#define MMS_TYPE_TASK_DECODE (mms_task_decode_get_type())
#define MMS_TASK_DECODE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
   MMS_TYPE_TASK_DECODE, MMSTaskDecode))

static
gboolean
mms_task_decode_array_contains_string(
    const GPtrArray* array,
    const char* str)
{
    guint i;
    for (i=0; i<array->len; i++) {
        if (!strcmp(array->pdata[i], str)) {
            return TRUE;
        }
    }
    return FALSE;
}

static
const char*
mms_task_decode_add_file_name(
    GPtrArray* names,
    const char* proposed)
{
    const char* src;
    char* file = g_new(char, strlen(proposed)+1);
    char* dest = file;
    for (src = proposed; *src; src++) {
        switch (*src) {
        case '<': case '>': case '[': case ']':
            break;
        case '/': case '\\':
            *dest++ = '_';
            break;
        default:
            *dest++ = *src;
            break;
        }
    }
    *dest = 0;
    while (mms_task_decode_array_contains_string(names, file)) {
        char* _file = g_strconcat("_", file, NULL);
        g_free(file);
        file = _file;
    }
    g_ptr_array_add(names, file);
    return file;
}

static
char*
mms_task_decode_make_content_id(
    GPtrArray* ids,
    char* proposed)
{
    char* id = proposed;
    while (mms_task_decode_array_contains_string(ids, id)) {
        char* tmp = g_strconcat("_", id, NULL);
        if (id != proposed) g_free(id);
        id = tmp;
    }
    g_ptr_array_add(ids, id);
    return id;
}

static
void
mms_task_decode_part(
    MMSMessagePart* part,
    GMimeContentEncoding enc,
    const char* dir,
    GPtrArray* part_files)
{
    char* default_name;
    const char* orig_file;
    const char* part_file;
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    part_file = g_basename(part->file);
    G_GNUC_END_IGNORE_DEPRECATIONS;

    default_name = g_strconcat(part_file, ".orig", NULL);
    orig_file = mms_task_decode_add_file_name(part_files, default_name);
    g_free(default_name);

    part->orig = g_build_filename(dir, orig_file, NULL);
    if (rename(part->file, part->orig) == 0) {
        GError* error = NULL;
        if (!mms_file_decode(part->orig, part->file, enc, &error)) {
            unlink(part->file);
            rename(part->orig, part->file);
            g_free(part->orig);
            part->orig = NULL;
            GERR("%s", GERRMSG(error));
            g_error_free(error);
        }
    } else {
        GERR("Failed to rename %s to %s: %s", part->file, part->orig,
            strerror(errno));
    }
}

static
MMSMessage*
mms_task_decode_retrieve_conf(
    MMSTask* task,
    const MMSPdu* pdu,
    const guint8* pdu_data,
    gsize pdu_size)
{
    GSList* entry;
    int i, nparts = g_slist_length(pdu->attachments);
    GPtrArray* part_files = g_ptr_array_new_full(nparts, g_free);
    GPtrArray* part_ids = g_ptr_array_new();
    char* dir = mms_task_dir(task);
    const struct mms_retrieve_conf* rc = &pdu->rc;
    MMSMessage* msg = mms_message_new();

#if GUTIL_LOG_DEBUG
    char date[128];
    strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S%z", localtime(&rc->date));
    date[sizeof(date)-1] = '\0';
#endif /* GUTIL_LOG_DEBUG */

    GASSERT(pdu->type == MMS_MESSAGE_TYPE_RETRIEVE_CONF);
    GINFO("Processing M-Retrieve.conf");
    GINFO("  From: %s", rc->from);

#if GUTIL_LOG_DEBUG
    GDEBUG("  To: %s", rc->to);
    if (rc->cc) GDEBUG("  Cc: %s", rc->cc);
    GDEBUG("  Message-ID: %s", rc->msgid);
    GDEBUG("  Transaction-ID: %s", pdu->transaction_id);
    if (rc->subject) GDEBUG("  Subject: %s", rc->subject);
    GDEBUG("  Date: %s", date);
    GDEBUG("  %u parts", nparts);
#endif /* GUTIL_LOG_DEBUG */

    if (task_config(task)->keep_temp_files) {
        msg->flags |= MMS_MESSAGE_FLAG_KEEP_FILES;
    }

    msg->id = g_strdup(task->id);
    msg->msg_dir = g_strdup(dir);
    msg->message_id = g_strdup(rc->msgid);
    msg->from = mms_strip_address_type(g_strdup(rc->from));
    msg->to = mms_split_address_list(rc->to);
    msg->cc = mms_split_address_list(rc->cc);
    msg->subject = g_strdup(rc->subject);
    msg->cls = g_strdup(rc->cls ? rc->cls : MMS_MESSAGE_CLASS_PERSONAL);
    msg->date = rc->date ? rc->date : time(NULL);
    msg->read_report_req = rc->rr;

    switch (rc->priority) {
    case MMS_MESSAGE_PRIORITY_LOW:
        msg->priority = MMS_PRIORITY_LOW;
        break;
    case MMS_MESSAGE_PRIORITY_NORMAL:
        msg->priority = MMS_PRIORITY_NORMAL;
        break;
    case MMS_MESSAGE_PRIORITY_HIGH:
        msg->priority = MMS_PRIORITY_HIGH;
        break;
    }

    msg->parts_dir = g_build_filename(dir, MMS_PARTS_DIR, NULL);
    for (i=0, entry = pdu->attachments; entry; entry = entry->next, i++) {
        struct mms_attachment* attach = entry->data;
        const char* name =  attach->content_location ?
            attach->content_location : attach->content_id;
        char* path = NULL;
        const char* file;
        if (name && name[0]) {
            file = mms_task_decode_add_file_name(part_files, name);
        } else {
            char* name = g_strdup_printf("part_%d",i);
            file = mms_task_decode_add_file_name(part_files, name);
            g_free(name);
        }
        GDEBUG("Part: %s %s", name, attach->content_type);
        GASSERT(attach->offset < pdu_size);
        if (mms_write_file(msg->parts_dir, file, pdu_data + attach->offset,
            attach->length, &path)) {
            MMSMessagePart* part = g_new0(MMSMessagePart, 1);
            char* tmp = NULL;
            char* id = attach->content_id ? g_strdup(attach->content_id) :
                (tmp = g_strconcat("<", file, ">", NULL));
            part->content_type = g_strdup(attach->content_type);
            part->content_id = mms_task_decode_make_content_id(part_ids, id);
            part->file = path;
            if (attach->transfer_encoding) {
                GMimeContentEncoding enc =
                    g_mime_content_encoding_from_string(
                        attach->transfer_encoding);
                if (enc > GMIME_CONTENT_ENCODING_BINARY) {
                    /* The part actually needs some decoding */
                    GDEBUG("Decoding %s", attach->transfer_encoding);
                    mms_task_decode_part(part,enc,msg->parts_dir,part_files);
                }
            }
            msg->parts = g_slist_append(msg->parts, part);
            if (tmp && tmp != part->content_id) g_free(tmp);
        }
    }

    g_ptr_array_free(part_files, TRUE);
    g_ptr_array_free(part_ids, TRUE);
    g_free(dir);
    return msg;
}

static
void
mms_task_decode_process_pdu(
    MMSTaskDecode* dec,
    MMSPdu* pdu)
{
    MMSTask* task = &dec->task;
    const void* data = g_mapped_file_get_contents(dec->map);
    const gsize len = g_mapped_file_get_length(dec->map);
    if (mms_message_decode(data, len, pdu)) {
        if (pdu->type == MMS_MESSAGE_TYPE_RETRIEVE_CONF) {
            struct mms_retrieve_conf* rc = &pdu->rc;
            /* Message-ID must be present only if the M-Retrieve.conf PDU
             * includes the requested MM */
            if (rc->msgid &&
               (rc->retrieve_status == 0 /* no status at all */ ||
                rc->retrieve_status == MMS_MESSAGE_RETRIEVE_STATUS_OK)) {
                MMSMessage* msg;
                msg = mms_task_decode_retrieve_conf(task, pdu, data, len);
                if (msg) {
                    /* Successfully received and decoded MMS message */
                    mms_task_queue_and_unref(task->delegate,
                        mms_task_ack_new(task, dec->transfers,
                            dec->transaction_id));
                    mms_task_queue_and_unref(task->delegate,
                        mms_task_publish_new(task->settings,
                            task->handler, msg));
                    mms_message_unref(msg);
                    return;
                }
            } else {
                /* MMS server returned an error. Most likely, MMS message
                 * has expired. We need more MMS_RECEIVE_STATE values to
                 * better describe it to the user. */
                GERR("MMSC responded with %u", rc->retrieve_status);
                mms_handler_message_receive_state_changed(task->handler,
                    task->id, MMS_RECEIVE_STATE_DOWNLOAD_ERROR);
                return;
            }
        } else {
            GERR("Unexpected MMS PDU type %u", (guint)pdu->type);
        }
    } else {
        GERR("Failed to decode MMS PDU");
    }

    /* Tell MMS server that we didn't understand this PDU */
    mms_task_queue_and_unref(task->delegate,
        mms_task_notifyresp_new(task, dec->transfers, dec->transaction_id,
            MMS_MESSAGE_NOTIFY_STATUS_UNRECOGNISED));
    mms_handler_message_receive_state_changed(task->handler, task->id,
        MMS_RECEIVE_STATE_DECODING_ERROR);
}

static
void
mms_task_decode_run(
    MMSTask* task)
{
    MMSPdu* pdu = g_new0(MMSPdu, 1);
    mms_task_decode_process_pdu(MMS_TASK_DECODE(task), pdu);
    mms_message_free(pdu);
    mms_task_set_state(task, MMS_TASK_STATE_DONE);
}

static
void
mms_task_decode_finalize(
    GObject* object)
{
    MMSTaskDecode* dec = MMS_TASK_DECODE(object);
    if (!task_config(&dec->task)->keep_temp_files) {
        mms_remove_file_and_dir(dec->file);
    }
    mms_transfer_list_unref(dec->transfers);
    g_mapped_file_unref(dec->map);
    g_free(dec->transaction_id);
    g_free(dec->file);
    G_OBJECT_CLASS(mms_task_decode_parent_class)->finalize(object);
}

static
void
mms_task_decode_class_init(
    MMSTaskDecodeClass* klass)
{
    klass->fn_run = mms_task_decode_run;
    G_OBJECT_CLASS(klass)->finalize = mms_task_decode_finalize;
}

static
void
mms_task_decode_init(
    MMSTaskDecode* decode)
{
}

/* Create MMS decode task */
MMSTask*
mms_task_decode_new(
    MMSTask* parent,
    MMSTransferList* transfers,
    const char* transaction_id,
    const char* file)
{
    if (file) {
        MMSTaskDecode* dec = mms_task_alloc(MMS_TYPE_TASK_DECODE,
            parent->settings, parent->handler, "Decode", parent->id,
            parent->imsi);
        GError* error = NULL;
        dec->map = g_mapped_file_new(file, FALSE, &error);
        if (dec->map) {
            dec->transfers = mms_transfer_list_ref(transfers);
            dec->transaction_id = g_strdup(transaction_id);
            dec->file = g_strdup(file);
            dec->task.priority = MMS_TASK_PRIORITY_POST_PROCESS;
            return &dec->task;
        } else if (error) {
            GERR("%s", GERRMSG(error));
            g_error_free(error);
        }
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
