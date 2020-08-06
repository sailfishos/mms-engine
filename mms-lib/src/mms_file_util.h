/*
 * Copyright (C) 2013-2020 Jolla Ltd.
 * Copyright (C) 2013-2020 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2020 Open Mobile Platform LLC.
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

#ifndef SAILFISH_MMS_FILE_UTIL_H
#define SAILFISH_MMS_FILE_UTIL_H

#include "mms_lib_types.h"

#include <gmime/gmime-encodings.h>

/* Permissions for MMS files and directories */
#define MMS_DIR_PERM                    (0755)
#define MMS_FILE_PERM                   (0644)

/* Directories and files */
#define MMS_ATTIC_DIR                   "attic"
#define MMS_MESSAGE_DIR                 "msg"
#define MMS_PARTS_DIR                   "parts"
#define MMS_ENCODE_DIR                  "encode"
#define MMS_CONVERT_DIR                 "convert"

#define MMS_NOTIFICATION_IND_FILE       "m-notification.ind"
#define MMS_NOTIFYRESP_IND_FILE         "m-notifyresp.ind"
#define MMS_RETRIEVE_CONF_FILE          "m-retrieve.conf"
#define MMS_ACKNOWLEDGE_IND_FILE        "m-acknowledge.ind"
#define MMS_DELIVERY_IND_FILE           "m-delivery.ind"
#define MMS_READ_REC_IND_FILE           "m-read-rec.ind"
#define MMS_READ_ORIG_IND_FILE          "m-read-orig.ind"
#define MMS_SEND_REQ_FILE               "m-send.req"
#define MMS_SEND_CONF_FILE              "m-send.conf"
#define MMS_UNRECOGNIZED_PUSH_FILE      "push.pdu"

gboolean
mms_file_is_smil(
    const char* file);

void
mms_remove_file_and_dir(
    const char* file);

char*
mms_prepare_filename(
    const char* file,
    const char* subdir)
    G_GNUC_WARN_UNUSED_RESULT;

int
mms_create_file(
    const char* dir,
    const char* fname,
    char** path,
    GError** error)
    G_GNUC_WARN_UNUSED_RESULT;

gboolean
mms_write_file(
    const char* dir,
    const char* file,
    const void* data,
    gsize size,
    char** path);

gboolean
mms_write_bytes(
    const char* dir,
    const char* file,
    GBytes* bytes,
    char** path);

gboolean
mms_file_copy(
    const char* src,
    const char* dest,
    GError** error);

gboolean
mms_file_decode(
    const char* src,
    const char* dest,
    GMimeContentEncoding enc,
    GError** error);

#define mms_message_dir(config,id) \
    (g_build_filename((config)->root_dir, MMS_MESSAGE_DIR, id, NULL))
#define mms_task_dir(task) \
    mms_message_dir(task_config(task),(task)->id)
#define mms_task_file(task,file) \
    (g_build_filename(task_config(task)->root_dir, MMS_MESSAGE_DIR, \
    (task)->id, file, NULL))

#endif /* SAILFISH_MMS_FILE_UTIL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
