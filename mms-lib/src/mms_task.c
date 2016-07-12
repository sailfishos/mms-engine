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
#include "mms_file_util.h"

#ifdef _WIN32
#  define snprintf _snprintf
#endif

/* Logging */
#define GLOG_MODULE_NAME MMS_TASK_LOG
#include <gutil_log.h>
GLOG_MODULE_DEFINE("mms-task");

#define MMS_TASK_DEFAULT_LIFETIME (600)

struct mms_task_priv {
    guint wakeup_id;                     /* ID of the wakeup source */
    time_t wakeup_time;                  /* Wake up time (if sleeping) */
};

G_DEFINE_ABSTRACT_TYPE(MMSTask, mms_task, G_TYPE_OBJECT)

#define MMS_TASK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), MMS_TYPE_TASK, MMSTask))
#define MMS_TASK_GET_CLASS(obj)  \
    (G_TYPE_INSTANCE_GET_CLASS((obj), MMS_TYPE_TASK, MMSTaskClass))

/*
 * mms_task_order is copied to MMSTask->order and incremented every
 * time a new task is created. It's reset to zero after all tasks have
 * finished, in order to avoid overflow.
 */
static int mms_task_order;
static int mms_task_count;

static
void
mms_task_wakeup_free(
    gpointer data)
{
    mms_task_unref(data);
}

static
gboolean
mms_task_wakeup_callback(
    gpointer data)
{
    MMSTask* task = MMS_TASK(data);
    MMSTaskPriv* priv = task->priv;
    priv->wakeup_id = 0;
    GASSERT(task->state == MMS_TASK_STATE_SLEEP);
    mms_task_set_state(task, MMS_TASK_STATE_READY);
    return FALSE;
}

gboolean
mms_task_schedule_wakeup(
    MMSTask* task,
    unsigned int secs)
{
    MMSTaskPriv* priv = task->priv;
    const time_t now = time(NULL);
    if (!secs) secs = task->settings->config->retry_secs;

    /* Cancel the previous sleep */
    if (priv->wakeup_id) {
        GASSERT(task->state == MMS_TASK_STATE_SLEEP);
        g_source_remove(priv->wakeup_id);
        priv->wakeup_id = 0;
    }

    if (now < task->deadline) {
        /* Don't sleep past deadline */
        const unsigned int max_secs = task->deadline - now;
        if (secs > max_secs) secs = max_secs;
        /* Schedule wakeup */
        priv->wakeup_time = now + secs;
        priv->wakeup_id = g_timeout_add_seconds_full(G_PRIORITY_DEFAULT,
            secs, mms_task_wakeup_callback, mms_task_ref(task),
            mms_task_wakeup_free);
        GASSERT(priv->wakeup_id);
        GVERBOSE("%s sleeping for %u sec", task->name, secs);
    }

    return (priv->wakeup_id > 0);
}

gboolean
mms_task_sleep(
    MMSTask* task,
    unsigned int secs)
{
    gboolean ok = mms_task_schedule_wakeup(task, secs);
    mms_task_set_state(task, ok ? MMS_TASK_STATE_SLEEP : MMS_TASK_STATE_DONE);
    return ok;
}

static
void
mms_task_cancel_cb(
    MMSTask* task)
{
    MMSTaskPriv* priv = task->priv;
    if (priv->wakeup_id) {
        GASSERT(task->state == MMS_TASK_STATE_SLEEP);
        g_source_remove(priv->wakeup_id);
        priv->wakeup_id = 0;
    }
    task->flags |= MMS_TASK_FLAG_CANCELLED;
    mms_task_set_state(task, MMS_TASK_STATE_DONE);
}

static
void
mms_task_finalize(
    GObject* object)
{
    MMSTask* task = MMS_TASK(object);
    GVERBOSE_("%p", task);
    GASSERT(!task->delegate);
    GASSERT(!task->priv->wakeup_id);
    GASSERT(mms_task_count > 0);
    if (!(--mms_task_count)) {
        GVERBOSE("Last task is gone");
        mms_task_order = 0;
    }
    if (task->id) {
        if (!task_config(task)->keep_temp_files) {
            char* dir = mms_task_dir(task);
            if (rmdir(dir) == 0) {
                GVERBOSE("Deleted %s", dir);
            }
            g_free(dir);
        }
        g_free(task->id);
    }
    g_free(task->name);
    g_free(task->imsi);
    mms_settings_unref(task->settings);
    mms_handler_unref(task->handler);
    G_OBJECT_CLASS(mms_task_parent_class)->finalize(object);
}

static
void
mms_task_class_init(
    MMSTaskClass* klass)
{
    klass->fn_cancel = mms_task_cancel_cb;
    g_type_class_add_private(klass, sizeof(MMSTaskPriv));
    G_OBJECT_CLASS(klass)->finalize = mms_task_finalize;
}

static
void
mms_task_init(
    MMSTask* task)
{
    GVERBOSE_("%p", task);
    mms_task_count++;
    task->order = mms_task_order++;
    task->priv = G_TYPE_INSTANCE_GET_PRIVATE(task, MMS_TYPE_TASK, MMSTaskPriv);
}

void*
mms_task_alloc(
    GType type,
    MMSSettings* settings,
    MMSHandler* handler,
    const char* name,
    const char* id,
    const char* imsi)
{
    MMSTask* task = g_object_new(type, NULL);
    const time_t now = time(NULL);
    time_t max_lifetime = MMS_TASK_GET_CLASS(task)->max_lifetime;
    if (!max_lifetime) max_lifetime = MMS_TASK_DEFAULT_LIFETIME;
    task->settings = mms_settings_ref(settings);
    task->handler = mms_handler_ref(handler);
    if (name) {
        task->name = id ?
            g_strdup_printf("%s[%.08s]", name, id) :
            g_strdup(name);
    }
    task->id = g_strdup(id);
    task->imsi = g_strdup(imsi);
    task->deadline = now + max_lifetime;
    return task;
}

MMSTask*
mms_task_ref(
    MMSTask* task)
{
    if (task) g_object_ref(MMS_TASK(task));
    return task;
}

void
mms_task_unref(
    MMSTask* task)
{
    if (task) g_object_unref(MMS_TASK(task));
}

void
mms_task_run(
    MMSTask* task)
{
    GASSERT(task->state == MMS_TASK_STATE_READY);
    MMS_TASK_GET_CLASS(task)->fn_run(task);
    GASSERT(task->state != MMS_TASK_STATE_READY);
}

void
mms_task_transmit(
    MMSTask* task,
    MMSConnection* connection)
{
    GASSERT(task->state == MMS_TASK_STATE_NEED_CONNECTION ||
               task->state == MMS_TASK_STATE_NEED_USER_CONNECTION);
    MMS_TASK_GET_CLASS(task)->fn_transmit(task, connection);
    GASSERT(task->state != MMS_TASK_STATE_NEED_CONNECTION &&
               task->state != MMS_TASK_STATE_NEED_USER_CONNECTION);
}

void
mms_task_network_unavailable(
    MMSTask* task,
    gboolean can_retry)
{
    if (task->state != MMS_TASK_STATE_DONE) {
        GASSERT(task->state == MMS_TASK_STATE_NEED_CONNECTION ||
                   task->state == MMS_TASK_STATE_NEED_USER_CONNECTION ||
                   task->state == MMS_TASK_STATE_TRANSMITTING);
        MMS_TASK_GET_CLASS(task)->fn_network_unavailable(task, can_retry);
        GASSERT(task->state != MMS_TASK_STATE_NEED_CONNECTION &&
                   task->state != MMS_TASK_STATE_NEED_USER_CONNECTION &&
                   task->state != MMS_TASK_STATE_TRANSMITTING);
    }
}

void
mms_task_cancel(
    MMSTask* task)
{
    GDEBUG_("%s", task->name);
    MMS_TASK_GET_CLASS(task)->fn_cancel(task);
}

void
mms_task_set_state(
    MMSTask* task,
    MMS_TASK_STATE state)
{
    MMSTaskPriv* priv = task->priv;
    if (task->state != state) {
        GDEBUG("%s %s -> %s", task->name,
            mms_task_state_name(task->state),
            mms_task_state_name(state));
        if (state == MMS_TASK_STATE_SLEEP && !priv->wakeup_id) {
            const unsigned int secs = task_config(task)->retry_secs;
            if (!mms_task_schedule_wakeup(task, secs)) {
                GDEBUG("%s SLEEP -> DONE (no time left)", task->name);
                MMS_TASK_GET_CLASS(task)->fn_cancel(task);
                state = MMS_TASK_STATE_DONE;
            }
        }
        /* Canceling the task may change the state so check it again */
        if (task->state != state) {
            task->state = state;
            if (task->delegate && task->delegate->fn_task_state_changed) {
                task->delegate->fn_task_state_changed(task->delegate, task);
            }
        }
    }
}

/* Utilities */

static const char* mms_task_names[] = {"READY", "NEED_CONNECTION",
    "NEED_USER_CONNECTION", "TRANSMITTING", "WORKING", "PENDING",
    "SLEEP", "DONE"
};
G_STATIC_ASSERT(G_N_ELEMENTS(mms_task_names) == MMS_TASK_STATE_COUNT);

const char*
mms_task_state_name(
    MMS_TASK_STATE state)
{
    if (state >= 0 && state < G_N_ELEMENTS(mms_task_names)) {
        return mms_task_names[state];
    } else {
        /* This shouldn't happen */
        static char unknown[32];
        snprintf(unknown, sizeof(unknown), "%d ????", state);
        return unknown;
    }
}

gboolean
mms_task_queue_and_unref(
    MMSTaskDelegate* delegate,
    MMSTask* task)
{
    gboolean ok = FALSE;
    if (task) {
        if (delegate && delegate->fn_task_queue) {
            delegate->fn_task_queue(delegate, task);
            ok = TRUE;
        }
        mms_task_unref(task);
    }
    return ok;
}

gboolean
mms_task_match_id(
    MMSTask* task,
    const char* id)
{
    if (!task) {
        /* No task - no match */
        return FALSE;
    } else if (!id || !id[0]) {
        /* A wildcard matching any task */
        return TRUE;
    } else if (!task->id || !task->id[0]) {
        /* Only wildcard will match that */
        return FALSE;
    } else {
        return !strcmp(task->id, id);
    }
}

/**
 * Generates dummy task id if necessary.
 */
const char*
mms_task_make_id(
    MMSTask* task)
{
    if (!task->id || !task->id[0]) {
        const char* root_dir = task_config(task)->root_dir;
        char* msgdir = g_strconcat(root_dir, "/", MMS_MESSAGE_DIR, NULL);
        int err = g_mkdir_with_parents(msgdir, MMS_DIR_PERM);
        if (!err || errno == EEXIST) {
            char* tmpl = g_strconcat(msgdir, "/XXXXXX" , NULL);
            char* taskdir = g_mkdtemp_full(tmpl, MMS_DIR_PERM);
            if (taskdir) {
                g_free(task->id);
                task->id = g_path_get_basename(taskdir);
            }
            g_free(tmpl);
        } else {
            GERR("Failed to create %s: %s", root_dir, strerror(errno));
        }
        g_free(msgdir);
    }
    return task->id;
}

const MMSSettingsSimData*
mms_task_sim_settings(
    MMSTask* task)
{
    if (task && task->settings) {
        return mms_settings_get_sim_data(task->settings, task->imsi);
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
