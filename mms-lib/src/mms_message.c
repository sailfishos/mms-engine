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

#include "mms_message.h"

/* Logging */
#define GLOG_MODULE_NAME mms_message_log
#include <gutil_log.h>
GLOG_MODULE_DEFINE("mms-message");

static
void
mms_message_part_free(
    gpointer data,
    gpointer user_data)
{
    MMSMessagePart* part = data;
    MMSMessage* msg = user_data;
    g_free(part->content_type);
    g_free(part->content_id);
    if (part->file) {
        if (!(msg->flags & MMS_MESSAGE_FLAG_KEEP_FILES)) remove(part->file);
        g_free(part->file);
    }
    if (part->orig) {
        if (!(msg->flags & MMS_MESSAGE_FLAG_KEEP_FILES)) remove(part->orig);
        g_free(part->orig);
    }
    g_free(part);
}

static
void
mms_message_finalize(
    MMSMessage* msg)
{
    GVERBOSE_("%p", msg);
    g_free(msg->id);
    g_free(msg->message_id);
    g_free(msg->from);
    g_strfreev(msg->to);
    g_strfreev(msg->cc);
    g_free(msg->subject);
    g_free(msg->cls);
    g_slist_foreach(msg->parts, mms_message_part_free, msg);
    g_slist_free(msg->parts);
    if (msg->parts_dir) {
        if (!(msg->flags & MMS_MESSAGE_FLAG_KEEP_FILES)) {
            if (rmdir(msg->parts_dir) == 0) {
                GVERBOSE("Deleted %s", msg->parts_dir);
            }
        }
        g_free(msg->parts_dir);
    }
    if (msg->msg_dir) {
        if (!(msg->flags & MMS_MESSAGE_FLAG_KEEP_FILES)) {
            if (rmdir(msg->msg_dir) == 0) {
                GVERBOSE("Deleted %s", msg->msg_dir);
            }
        }
        g_free(msg->msg_dir);
    }
}

MMSMessage*
mms_message_new()
{
    MMSMessage* msg = g_new0(MMSMessage, 1);
    GVERBOSE_("%p", msg);
    msg->ref_count = 1;
    msg->priority = MMS_PRIORITY_NORMAL;
    return msg;
}

MMSMessage*
mms_message_ref(
    MMSMessage* msg)
{
    if (msg) {
        GASSERT(msg->ref_count > 0);
        g_atomic_int_inc(&msg->ref_count);
    }
    return msg;
}

void
mms_message_unref(
    MMSMessage* msg)
{
    if (msg) {
        GASSERT(msg->ref_count > 0);
        if (g_atomic_int_dec_and_test(&msg->ref_count)) {
            mms_message_finalize(msg);
            g_free(msg);
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
