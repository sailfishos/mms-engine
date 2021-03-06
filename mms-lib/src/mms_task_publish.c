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
#include "mms_handler.h"

/* Logging */
#define GLOG_MODULE_NAME mms_task_publish_log
#include "mms_lib_log.h"
#include <gutil_log.h>
GLOG_MODULE_DEFINE2("mms-task-publish", MMS_TASK_LOG);

/* Class definition */
typedef MMSTaskClass MMSTaskPublishClass;
typedef struct mms_task_publish {
    MMSTask task;
    MMSMessage* msg;
    MMSHandlerMessageReceivedCall* call;
} MMSTaskPublish;

G_DEFINE_TYPE(MMSTaskPublish, mms_task_publish, MMS_TYPE_TASK);
#define MMS_TYPE_TASK_PUBLISH (mms_task_publish_get_type())
#define MMS_TASK_PUBLISH(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
   MMS_TYPE_TASK_PUBLISH, MMSTaskPublish))

static
void
mms_task_publish_done(
    MMSHandlerMessageReceivedCall* call,
    MMSMessage* msg,
    gboolean ok,
    void* param)
{
    MMSTaskPublish* pub = MMS_TASK_PUBLISH(param);
    if (ok) {
        GDEBUG("Done");
        mms_task_set_state(&pub->task, MMS_TASK_STATE_DONE);
    } else if (mms_task_retry(&pub->task)) {
        GERR("Failed to publish the message, will retry later...");
    } else {
        GERR("Failed to publish the message");
    }
    mms_task_unref(&pub->task);
}

static
void
mms_task_publish_run(
    MMSTask* task)
{
    MMSTaskPublish* pub = MMS_TASK_PUBLISH(task);
    GASSERT(!pub->call);
    mms_task_ref(task);
    pub->call = mms_handler_message_received(task->handler, pub->msg,
        mms_task_publish_done, pub);
    if (pub->call) {
        mms_task_set_state(task, MMS_TASK_STATE_PENDING);
    } else {
        mms_task_unref(task);
        mms_task_retry(task);
    }
}

static
void
mms_task_publish_cancel(
    MMSTask* task)
{
    MMSTaskPublish* pub = MMS_TASK_PUBLISH(task);
    if (pub->call) {
        mms_handler_message_received_cancel(task->handler, pub->call);
        pub->call = NULL;
        mms_task_unref(task);
    }
    MMS_TASK_CLASS(mms_task_publish_parent_class)->fn_cancel(task);
}

static
void
mms_task_publish_finalize(
    GObject* object)
{
    MMSTaskPublish* pub = MMS_TASK_PUBLISH(object);
    mms_message_unref(pub->msg);
    G_OBJECT_CLASS(mms_task_publish_parent_class)->finalize(object);
}

static
void
mms_task_publish_class_init(
    MMSTaskPublishClass* klass)
{
    klass->fn_run = mms_task_publish_run;
    klass->fn_cancel = mms_task_publish_cancel;
    G_OBJECT_CLASS(klass)->finalize = mms_task_publish_finalize;
}

static
void
mms_task_publish_init(
    MMSTaskPublish* publish)
{
}

/* Create MMS publish task */
MMSTask*
mms_task_publish_new(
    MMSSettings* settings,
    MMSHandler* handler,
    MMSMessage* msg)
{
    GASSERT(msg && msg->id);
    if (msg && msg->id) {
        MMSTaskPublish* pub = mms_task_alloc(MMS_TYPE_TASK_PUBLISH,
            settings, handler, "Publish", msg->id, NULL);
        pub->msg = mms_message_ref(msg);
        pub->task.priority = MMS_TASK_PRIORITY_POST_PROCESS;
        return &pub->task;
    } else {
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
