/*
 * Copyright (C) 2016 Jolla Ltd.
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

#include "test_transfer_list.h"

/* Logging */
#define MMS_LOG_MODULE_NAME mms_transfer_list_log
#include "mms_lib_log.h"
MMS_LOG_MODULE_DEFINE("mms-transfer-list-test");

/* Class definition */
typedef MMSTransferListClass MMSTransferListTestClass;
typedef struct mms_transfer_list_test {
    MMSTransferList super;
} MMSTransferListTest;

G_DEFINE_TYPE(MMSTransferListTest, mms_transfer_list_test, \
    MMS_TYPE_TRANSFER_LIST)
#define MMS_TYPE_TRANSFER_LIST_TEST (mms_transfer_list_test_get_type())
#define MMS_TRANSFER_LIST_TEST(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
    MMS_TYPE_TRANSFER_LIST_TEST, MMSTransferListTest))

MMSTransferList*
mms_transfer_list_test_new()
{
    return g_object_new(MMS_TYPE_TRANSFER_LIST_TEST, 0);
}

static
void
mms_transfer_list_test_transfer_started(
    MMSTransferList* self,          /* Instance */
    char* id,                       /* Database record ID */
    char* type)                     /* Transfer type */
{
}

static
void
mms_transfer_list_test_transfer_finished(
    MMSTransferList* self,          /* Instance */
    char* id,                       /* Database record ID */
    char* type)                     /* Transfer type */
{
}

static
void
mms_transfer_list_test_send_progress(
    MMSTransferList* self,          /* Instance */
    char* id,                       /* Message ID */
    char* type,                     /* Transfer type */
    guint sent,                     /* Bytes sent so far */
    guint total)                    /* Total bytes to send */
{
}

static
void
mms_transfer_list_test_receive_progress(
    MMSTransferList* self,          /* Instance */
    char* id,                       /* Message ID */
    char* type,                     /* Transfer type */
    guint received,                 /* Bytes received so far */
    guint total)                    /* Total bytes to receive*/
{
}

static
void
mms_transfer_list_test_init(
    MMSTransferListTest* self)
{
}

static
void
mms_transfer_list_test_class_init(
    MMSTransferListTestClass* klass)
{
    klass->fn_transfer_started = mms_transfer_list_test_transfer_started;
    klass->fn_transfer_finished = mms_transfer_list_test_transfer_finished;
    klass->fn_send_progress = mms_transfer_list_test_send_progress;
    klass->fn_receive_progress = mms_transfer_list_test_receive_progress;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
