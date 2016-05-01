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

#include "mms_transfer_list.h"

/* Logging */
#define MMS_LOG_MODULE_NAME mms_transfer_list_log
#include "mms_lib_log.h"

G_DEFINE_ABSTRACT_TYPE(MMSTransferList, mms_transfer_list, G_TYPE_OBJECT)

#define MMS_TRANSFER_LIST(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
    MMS_TYPE_TRANSFER_LIST, MMSTransferList))
#define MMS_TRANSFER_LIST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), \
    MMS_TYPE_TRANSFER_LIST, MMSTransferListClass))

static
void
mms_transfer_list_class_init(
    MMSTransferListClass* klass)
{
}

static
void
mms_transfer_list_init(
    MMSTransferList* self)
{
}

MMSTransferList*
mms_transfer_list_ref(
    MMSTransferList* self)
{
    if (self) {
        MMS_ASSERT(MMS_TRANSFER_LIST(self));
        g_object_ref(self);
    }
    return self;
}

void
mms_transfer_list_unref(
    MMSTransferList* self)
{
    if (self) {
        MMS_ASSERT(MMS_TRANSFER_LIST(self));
        g_object_unref(self);
    }
}

void
mms_transfer_list_transfer_started(
    MMSTransferList* self,          /* Instance */
    char* id,                       /* Database record ID */
    char* type)                     /* Transfer type */
{
    if (self && id && type) {
        MMSTransferListClass* klass = MMS_TRANSFER_LIST_GET_CLASS(self);
        if (klass->fn_transfer_started) {
            klass->fn_transfer_started(self, id, type);
        }
    }
}

void
mms_transfer_list_transfer_finished(
    MMSTransferList* self,          /* Instance */
    char* id,                       /* Database record ID */
    char* type)                     /* Transfer type */
{
    if (self && id) {
        MMSTransferListClass* klass = MMS_TRANSFER_LIST_GET_CLASS(self);
        if (klass->fn_transfer_finished) {
            klass->fn_transfer_finished(self, id, type);
        }
    }
}

void
mms_transfer_list_transfer_send_progress(
    MMSTransferList* self,          /* Instance */
    char* id,                       /* Message ID */
    char* type,                     /* Transfer type */
    guint sent,                     /* Bytes sent so far */
    guint total)                    /* Total bytes to send */
{
    if (self && id) {
        MMSTransferListClass* klass = MMS_TRANSFER_LIST_GET_CLASS(self);
        if (klass->fn_send_progress) {
            klass->fn_send_progress(self, id, type, sent, total);
        }
    }
}

void
mms_transfer_list_transfer_receive_progress(
    MMSTransferList* self,          /* Instance */
    char* id,                       /* Message ID */
    char* type,                     /* Transfer type */
    guint received,                 /* Bytes received so far */
    guint total)                    /* Total bytes to receive*/
{
    if (self && id) {
        MMSTransferListClass* klass = MMS_TRANSFER_LIST_GET_CLASS(self);
        if (klass->fn_receive_progress) {
            klass->fn_receive_progress(self, id, type, received, total);
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
