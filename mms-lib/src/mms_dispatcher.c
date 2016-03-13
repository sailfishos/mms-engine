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

#include "mms_dispatcher.h"
#include "mms_handler.h"
#include "mms_settings.h"
#include "mms_connection.h"
#include "mms_connman.h"
#include "mms_transfer_list.h"
#include "mms_file_util.h"
#include "mms_codec.h"
#include "mms_util.h"
#include "mms_task.h"

#include <errno.h>

/* Logging */
#define MMS_LOG_MODULE_NAME mms_dispatcher_log
#include "mms_lib_log.h"
#include "mms_error.h"
MMS_LOG_MODULE_DEFINE("mms-dispatcher");

struct mms_dispatcher {
    gint ref_count;
    MMSSettings* settings;
    MMSTask* active_task;
    MMSTaskDelegate task_delegate;
    MMSHandler* handler;
    MMSConnMan* cm;
    MMSConnection* connection;
    MMSTransferList* transfers;
    MMSDispatcherDelegate* delegate;
    GQueue* tasks;
    guint next_run_id;
    guint network_idle_id;
    gulong handler_done_id;
    gulong connman_done_id;
    gulong connection_changed_id;
    gboolean started;
};

typedef void (*MMSDispatcherIdleCallbackProc)(MMSDispatcher* disp);
typedef struct mms_dispatcher_idle_callback {
    MMSDispatcher* dispatcher;
    MMSDispatcherIdleCallbackProc proc;
} MMSDispatcherIdleCallback;

inline static MMSDispatcher*
mms_dispatcher_from_task_delegate(MMSTaskDelegate* delegate)
    { return MMS_CAST(delegate,MMSDispatcher,task_delegate); }

static
void
mms_dispatcher_run(
    MMSDispatcher* disp);

/**
 * Checks if we are done and notifies the delegate if necessary
 */
static
void
mms_dispatcher_check_if_done(
    MMSDispatcher* disp)
{
    if (!mms_dispatcher_is_active(disp)) {
        /* Cancel pending runs */
        if (disp->next_run_id) {
            g_source_remove(disp->next_run_id);
            disp->next_run_id = 0;
        }
        /* Notify the delegate that we are done */
        if (disp->delegate && disp->delegate->fn_done && disp->started) {
            disp->started = FALSE;
            disp->delegate->fn_done(disp->delegate, disp);
        }
    }
}

/**
 * Drops the reference to the network connection
 */
static
void
mms_dispatcher_drop_connection(
    MMSDispatcher* disp)
{
    if (disp->connection) {
        MMS_ASSERT(!mms_connection_is_active(disp->connection));
        mms_connection_remove_handler(disp->connection,
            disp->connection_changed_id);
        disp->connection_changed_id = 0;
        mms_connection_unref(disp->connection);
        disp->connection = NULL;
        if (disp->network_idle_id) {
            g_source_remove(disp->network_idle_id);
            disp->network_idle_id = 0;
        }
    }
}

/**
 * Close the network connection
 */
static
void
mms_dispatcher_close_connection(
    MMSDispatcher* disp)
{
    if (disp->connection) {
        mms_connection_close(disp->connection);
        /* Assert that connection state changes are asynchronous */
        MMS_ASSERT(disp->connection);
        if (!mms_connection_is_active(disp->connection)) {
            mms_dispatcher_drop_connection(disp);
        }
        mms_dispatcher_check_if_done(disp);
    }
}

/**
 * Run loop callbacks
 */
static
void
mms_dispatcher_callback_free(
    gpointer data)
{
    MMSDispatcherIdleCallback* call = data;
    mms_dispatcher_unref(call->dispatcher);
    g_free(call);
}

static
gboolean
mms_dispatcher_idle_callback_cb(
    gpointer data)
{
    MMSDispatcherIdleCallback* call = data;
    call->proc(call->dispatcher);
    return FALSE;
}

static
guint
mms_dispatcher_callback_schedule(
    MMSDispatcher* disp,
    MMSDispatcherIdleCallbackProc proc)
{
    MMSDispatcherIdleCallback* call = g_new0(MMSDispatcherIdleCallback,1);
    call->dispatcher = mms_dispatcher_ref(disp);
    call->proc = proc;
    return g_idle_add_full(G_PRIORITY_HIGH, mms_dispatcher_idle_callback_cb,
        call, mms_dispatcher_callback_free);
}

static
guint
mms_dispatcher_timeout_callback_schedule(
    MMSDispatcher* disp,
    guint interval,
    MMSDispatcherIdleCallbackProc proc)
{
    MMSDispatcherIdleCallback* call = g_new0(MMSDispatcherIdleCallback,1);
    call->dispatcher = mms_dispatcher_ref(disp);
    call->proc = proc;
    return g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, interval,
        mms_dispatcher_idle_callback_cb, call, mms_dispatcher_callback_free);
}

/**
 * Network idle timeout
 */

static
void
mms_dispatcher_network_idle_run(
    MMSDispatcher* disp)
{
    MMS_ASSERT(disp->network_idle_id);
    disp->network_idle_id = 0;
    mms_dispatcher_close_connection(disp);
}

static
void
mms_dispatcher_network_idle_check(
    MMSDispatcher* disp)
{
    if (disp->connection && !disp->network_idle_id) {
        /* Schedule idle inactivity timeout callback */
        MMS_VERBOSE("Network connection is inactive");
        disp->network_idle_id = mms_dispatcher_timeout_callback_schedule(disp,
            disp->settings->config->idle_secs, mms_dispatcher_network_idle_run);
    }
}

static
void
mms_dispatcher_network_idle_cancel(
    MMSDispatcher* disp)
{
    if (disp->network_idle_id) {
        MMS_VERBOSE("Cancel network inactivity timeout");
        g_source_remove(disp->network_idle_id);
        disp->network_idle_id = 0;
    }
}

/**
 * Dispatcher run on a fresh stack
 */
static
void
mms_dispatcher_next_run(
    MMSDispatcher* disp)
{
    MMS_ASSERT(disp->next_run_id);
    MMS_ASSERT(!disp->active_task);
    disp->next_run_id = 0;
    if (!disp->active_task) {
        mms_dispatcher_run(disp);
    }
}

static
void
mms_dispatcher_next_run_schedule(
    MMSDispatcher* disp)
{
    if (disp->next_run_id) g_source_remove(disp->next_run_id);
    disp->next_run_id = mms_dispatcher_callback_schedule(disp,
        mms_dispatcher_next_run);
}

/**
 * Connection state callback
 */
static
void
mms_dispatcher_connection_state_changed(
    MMSConnection* conn,
    void* data)
{
    MMSDispatcher* disp = data;
    MMS_CONNECTION_STATE state = mms_connection_state(conn);
    MMS_DEBUG("%s %s", conn->imsi, mms_connection_state_name(conn));
    MMS_ASSERT(conn == disp->connection);
    if (state == MMS_CONNECTION_STATE_FAILED ||
        state == MMS_CONNECTION_STATE_CLOSED) {
        GList* entry;
        mms_dispatcher_close_connection(disp);
        for (entry = disp->tasks->head; entry; entry = entry->next) {
            MMSTask* task = entry->data;
            switch (task->state) {
            case MMS_TASK_STATE_NEED_CONNECTION:
            case MMS_TASK_STATE_NEED_USER_CONNECTION:
            case MMS_TASK_STATE_TRANSMITTING:
                if (!strcmp(conn->imsi, task->imsi)) {
                    mms_task_network_unavailable(task, TRUE);
                }
            default:
                break;
            }
        }
        mms_dispatcher_drop_connection(disp);
        mms_dispatcher_check_if_done(disp);
    }
    if (!disp->active_task) {
        mms_dispatcher_next_run_schedule(disp);
    }
}

/**
 * Set the delegate that receives dispatcher notifications.
 * One delegate per dispatcher.
 *
 * TODO: Replace this delegate stuff wuth glib signals
 */
void
mms_dispatcher_set_delegate(
    MMSDispatcher* disp,
    MMSDispatcherDelegate* delegate)
{
    MMS_ASSERT(!disp->delegate || !delegate);
    disp->delegate = delegate;
}

/**
 * Checks if dispatcher has something to do.
 */
gboolean
mms_dispatcher_is_active(
    MMSDispatcher* disp)
{
    return disp && (mms_connection_is_active(disp->connection) ||
        mms_handler_busy(disp->handler) || mms_connman_busy(disp->cm) ||
        disp->active_task || !g_queue_is_empty(disp->tasks));
}

/**
 * Task queue sort callback. Defines the order in which tasks are executed.
 * Returns 0 if the tasks are equal, a negative value if the first task
 * comes before the second, and a positive value if the second task comes
 * before the first.
 */
static
gint
mms_dispatcher_sort_cb(
    gconstpointer v1,
    gconstpointer v2,
    gpointer user_data)
{
    const MMSTask* task1 = v1;
    const MMSTask* task2 = v2;
    MMSDispatcher* disp = user_data;
    gboolean connection_is_open =
        mms_connection_state(disp->connection) == MMS_CONNECTION_STATE_OPEN;

    MMS_ASSERT(task1);
    MMS_ASSERT(task2);
    if (task1 == task2) {
        return 0;
    }
    /* Don't interfere with the task transmiting the data */
    if (task1->state != task2->state && connection_is_open) {
        if (task1->state == MMS_TASK_STATE_TRANSMITTING) return -1;
        if (task2->state == MMS_TASK_STATE_TRANSMITTING) return 1;

    }
    /* Compare priorities */
    if (task1->priority != task2->priority) {
        return task2->priority - task1->priority;
    }
    /* Prefer to reuse the existing connection */
    if (task1->state != task2->state && connection_is_open) {
        gboolean task1_wants_this_connection =
           (task1->state == MMS_TASK_STATE_NEED_CONNECTION ||
            task1->state == MMS_TASK_STATE_NEED_USER_CONNECTION) &&
            !strcmp(task1->imsi, disp->connection->imsi);
        gboolean task2_wants_this_connection =
           (task2->state == MMS_TASK_STATE_NEED_CONNECTION ||
            task2->state == MMS_TASK_STATE_NEED_USER_CONNECTION) &&
            !strcmp(task2->imsi, disp->connection->imsi);
        if (task1_wants_this_connection != task2_wants_this_connection) {
            if (task1_wants_this_connection) return -1;
            if (task2_wants_this_connection) return 1;
        }
    }
    /* Immediately runnable tasks first */
    if (task1->state != task2->state) {
        gboolean runnable1 =
            task1->state == MMS_TASK_STATE_READY ||
            task1->state == MMS_TASK_STATE_DONE;
        gboolean runnable2 =
            task2->state == MMS_TASK_STATE_READY ||
            task2->state == MMS_TASK_STATE_DONE;
        if (runnable1 != runnable2) {
            if (runnable1) return -1;
            if (runnable2) return 1;
        }
    }
    /* Followed by the tasks that want network connection */
    if (task1->state != task2->state) {
        gboolean task1_wants_connection =
            task1->state == MMS_TASK_STATE_NEED_CONNECTION ||
            task1->state == MMS_TASK_STATE_NEED_USER_CONNECTION;
        gboolean task2_wants_connection =
            task2->state == MMS_TASK_STATE_NEED_CONNECTION ||
            task2->state == MMS_TASK_STATE_NEED_USER_CONNECTION;
        if (task1_wants_connection != task2_wants_connection) {
            if (task1_wants_connection) return -1;
            if (task2_wants_connection) return 1;
        }
    }
    /* Otherwise follow the creation order */
    return task1->order - task2->order;
}

/**
 * Picks the next task for processing. Reference is passed to the caller.
 * Caller must eventually dereference the task or place it back to the queue.
 */
static
MMSTask*
mms_dispatcher_pick_next_task(
    MMSDispatcher* disp)
{
    g_queue_sort(disp->tasks, mms_dispatcher_sort_cb, disp);
    if (disp->tasks->head) {
        MMSTask* task = disp->tasks->head->data;
        switch (task->state) {
        case MMS_TASK_STATE_READY:
        case MMS_TASK_STATE_DONE:
            g_queue_delete_link(disp->tasks, disp->tasks->head);
            return task;
        case MMS_TASK_STATE_NEED_CONNECTION:
        case MMS_TASK_STATE_NEED_USER_CONNECTION:
            if (disp->connection &&
                strcmp(task->imsi, disp->connection->imsi)) {
                /* Wrong connection, close it */
                mms_dispatcher_close_connection(disp);
            }
            if (!disp->connection) {
                /* No connection, request it */
                disp->connection =
                    mms_connman_open_connection(disp->cm, task->imsi,
                        (task->state == MMS_TASK_STATE_NEED_USER_CONNECTION) ?
                        MMS_CONNECTION_TYPE_USER : MMS_CONNECTION_TYPE_AUTO);
                 if (disp->connection) {
                     MMS_ASSERT(!disp->connection_changed_id);
                     disp->connection_changed_id =
                         mms_connection_add_state_change_handler(
                             disp->connection,
                             mms_dispatcher_connection_state_changed,
                             disp);
                 }
            }
            if (disp->connection) {
                if (!strcmp(task->imsi, disp->connection->imsi) &&
                    mms_connection_is_open(disp->connection)) {
                    /* Connection can be used by this task */
                    g_queue_delete_link(disp->tasks, disp->tasks->head);
                    return task;
                }
            } else {
                /* Most likely, mms_connman_open_connection hasn't found
                 * the requested SIM card */
                mms_task_network_unavailable(task, FALSE);
            }
            break;
        default:
            break;
        }
    }

    /* Nothing to do at this point */
    return NULL;
}

/**
 * Task dispatch loop.
 */
static
void
mms_dispatcher_run(
    MMSDispatcher* disp)
{
    MMSTask* task;
    MMS_ASSERT(!disp->active_task);
    while ((task = mms_dispatcher_pick_next_task(disp)) != NULL) {
        MMS_DEBUG("%s %s", task->name, mms_task_state_name(task->state));
        disp->active_task = task;
        switch (task->state) {
        case MMS_TASK_STATE_READY:
            mms_task_run(task);
            break;

        case MMS_TASK_STATE_NEED_CONNECTION:
        case MMS_TASK_STATE_NEED_USER_CONNECTION:
            /* mms_dispatcher_pick_next_task() has checked that the right
             * connection is active, we can send/receive the data */
            MMS_ASSERT(mms_connection_is_open(disp->connection));
            MMS_ASSERT(!strcmp(task->imsi, disp->connection->imsi));
            mms_task_transmit(task, disp->connection);
            break;

        default:
            break;
        }

        if (task->state == MMS_TASK_STATE_DONE) {
            task->delegate = NULL;
            mms_task_unref(task);
        } else {
            g_queue_push_tail(disp->tasks, task);
        }
        disp->active_task = NULL;
    }

    if (disp->connection) {
        /* Check if network connection is being used */
        GList* entry;
        gboolean connection_in_use = FALSE;
        for (entry = disp->tasks->head; entry; entry = entry->next) {
            MMSTask* task = entry->data;
            if (task->state == MMS_TASK_STATE_NEED_CONNECTION ||
                task->state == MMS_TASK_STATE_NEED_USER_CONNECTION ||
                task->state == MMS_TASK_STATE_TRANSMITTING) {
                connection_in_use = TRUE;
                break;
            }
        }
        if (connection_in_use) {
            /* It's in use, disable idle inactivity callback */
            mms_dispatcher_network_idle_cancel(disp);
        } else {
            /* Make sure that network inactivity timer is ticking */
            mms_dispatcher_network_idle_check(disp);
        }
    }

    mms_dispatcher_check_if_done(disp);
}

/**
 * Starts task processing.
 */
gboolean
mms_dispatcher_start(
    MMSDispatcher* disp)
{
    const char* root_dir = disp->settings->config->root_dir;
    int err = g_mkdir_with_parents(root_dir, MMS_DIR_PERM);
    if (!err || errno == EEXIST) {
        if (!g_queue_is_empty(disp->tasks)) {
            disp->started = TRUE;
            mms_dispatcher_next_run_schedule(disp);
            return TRUE;
        }
    } else {
        MMS_ERR("Failed to create %s: %s", root_dir, strerror(errno));
    }
    return FALSE;
}

static
void
mms_dispatcher_queue_task(
    MMSDispatcher* disp,
    MMSTask* task)
{
    task->delegate = &disp->task_delegate;
    g_queue_push_tail(disp->tasks, mms_task_ref(task));
}

static
gboolean
mms_dispatcher_queue_and_unref_task(
    MMSDispatcher* disp,
    MMSTask* task)
{
    if (task) {
        mms_dispatcher_queue_task(disp, task);
        mms_task_unref(task);
        return TRUE;
    } else {
        return FALSE;
    }
}

/**
 * Creates a WAP push receive task and adds it to the queue.
 */
gboolean
mms_dispatcher_handle_push(
    MMSDispatcher* disp,
    const char* imsi,
    GBytes* push,
    GError** error)
{
    return mms_dispatcher_queue_and_unref_task(disp,
        mms_task_notification_new(disp->settings, disp->handler,
            disp->transfers, imsi, push, error));
}

/**
 * Creates download task and adds it to the queue.
 */
gboolean
mms_dispatcher_receive_message(
    MMSDispatcher* disp,
    const char* id,
    const char* imsi,
    gboolean automatic,
    GBytes* bytes,
    GError** error)
{
    gboolean ok = FALSE;
    MMSPdu* pdu = mms_decode_bytes(bytes);
    if (pdu) {
        MMS_ASSERT(pdu->type == MMS_MESSAGE_TYPE_NOTIFICATION_IND);
        if (pdu->type == MMS_MESSAGE_TYPE_NOTIFICATION_IND) {
            const MMSConfig* config = disp->settings->config;
            ok = mms_dispatcher_queue_and_unref_task(disp,
                mms_task_retrieve_new(disp->settings, disp->handler,
                    disp->transfers, id, imsi, pdu, automatic ?
                    MMS_CONNECTION_TYPE_AUTO : MMS_CONNECTION_TYPE_USER,
                    error));
            if (config->keep_temp_files) {
                char* dir = mms_message_dir(config, id);
                mms_write_bytes(dir, MMS_NOTIFICATION_IND_FILE, bytes, NULL);
                g_free(dir);
            }
        } else {
            MMS_ERROR(error, MMS_LIB_ERROR_DECODE, "Inexpected MMS PDU type");
        }
        mms_message_free(pdu);
    } else {
        MMS_ERROR(error, MMS_LIB_ERROR_DECODE, "Failed to decode MMS PDU");
    }
    return ok;
}

/**
 * Sends read report
 */
gboolean
mms_dispatcher_send_read_report(
    MMSDispatcher* disp,
    const char* id,
    const char* imsi,
    const char* message_id,
    const char* to,
    MMSReadStatus status,
    GError** error)
{
    return mms_dispatcher_queue_and_unref_task(disp,
        mms_task_read_new(disp->settings, disp->handler, disp->transfers,
            id, imsi, message_id, to, status, error));
}

/**
 * Sends MMS message
 */
char*
mms_dispatcher_send_message(
    MMSDispatcher* disp,
    const char* id,
    const char* imsi,
    const char* to,
    const char* cc,
    const char* bcc,
    const char* subject,
    unsigned int flags,
    const MMSAttachmentInfo* parts,
    unsigned int nparts,
    GError** error)
{
    char* default_imsi = NULL;
    if (!imsi || !imsi[0]) {
        /* No IMSI specified - try the default one */
        imsi = default_imsi = mms_connman_default_imsi(disp->cm);
    }
    if (imsi) {
        if (mms_dispatcher_queue_and_unref_task(disp,
            mms_task_encode_new(disp->settings, disp->handler, disp->transfers,
            id, imsi, to, cc, bcc, subject, flags, parts, nparts, error))) {
            return default_imsi ? default_imsi : g_strdup(imsi);
        }
    } else {
        MMS_ERROR(error, MMS_LIB_ERROR_NOSIM,
            "No IMSI is provided and none is available");
    }
    return NULL;
}

/**
 * Cancels al the activity associated with the specified message
 */
void
mms_dispatcher_cancel(
    MMSDispatcher* disp,
    const char* id)
{
    GList* entry;
    for (entry = disp->tasks->head; entry; entry = entry->next) {
        MMSTask* task = entry->data;
        if (mms_task_match_id(task, id)) {
            mms_task_cancel(task);
        }
    }

    if (mms_task_match_id(disp->active_task, id)) {
        mms_task_cancel(disp->active_task);
    }

    /* If we have cancelling all tasks, close the network connection
     * immediately to finish up as soon as possible. */
    if (!id && disp->connection) {
        mms_dispatcher_close_connection(disp);
    }
}

/**
 * Task delegate callbacks
 */
static
void
mms_dispatcher_delegate_task_queue(
    MMSTaskDelegate* delegate,
    MMSTask* task)
{
    MMSDispatcher* disp = mms_dispatcher_from_task_delegate(delegate);
    mms_dispatcher_queue_task(disp, task);
    if (!disp->active_task) {
        mms_dispatcher_next_run_schedule(disp);
    }
}

static
void
mms_dispatcher_delegate_task_state_changed(
    MMSTaskDelegate* delegate,
    MMSTask* task)
{
    MMSDispatcher* disp = mms_dispatcher_from_task_delegate(delegate);
    if (!disp->active_task) {
        mms_dispatcher_next_run_schedule(disp);
    }
}

/**
 * Handler state callback
 */
static
void
mms_dispatcher_handler_done(
    MMSHandler* handler,
    void* param)
{
    MMSDispatcher* disp = param;
    MMS_VERBOSE("Handler is inactive");
    mms_dispatcher_next_run_schedule(disp);
}

/**
 * Connman state callback
 */
static
void
mms_dispatcher_connman_done(
    MMSConnMan* cm,
    void* param)
{
    MMSDispatcher* disp = param;
    MMS_VERBOSE("Connman is inactive");
    mms_dispatcher_check_if_done(disp);
}

/**
 * Creates the dispatcher object. Caller must call mms_dispatcher_unref
 * when it no longer needs it.
 */
MMSDispatcher*
mms_dispatcher_new(
    MMSSettings* settings,
    MMSConnMan* cm,
    MMSHandler* handler,
    MMSTransferList* transfers)
{
    MMSDispatcher* disp = g_new0(MMSDispatcher, 1);
    disp->ref_count = 1;
    disp->settings = mms_settings_ref(settings);
    disp->tasks = g_queue_new();
    disp->handler = mms_handler_ref(handler);
    disp->cm = mms_connman_ref(cm);
    disp->transfers = mms_transfer_list_ref(transfers);
    disp->task_delegate.fn_task_queue =
        mms_dispatcher_delegate_task_queue;
    disp->task_delegate.fn_task_state_changed =
        mms_dispatcher_delegate_task_state_changed;
    disp->handler_done_id = mms_handler_add_done_callback(handler,
        mms_dispatcher_handler_done, disp);
    disp->connman_done_id = mms_connman_add_done_callback(cm,
        mms_dispatcher_connman_done, disp);
    return disp;
}

/**
 * Deinitializer
 */
static
void
mms_dispatcher_finalize(
    MMSDispatcher* disp)
{
    MMSTask* task;
    const char* root_dir = disp->settings->config->root_dir;
    char* msg_dir = g_strconcat(root_dir, "/" MMS_MESSAGE_DIR "/", NULL);
    MMS_VERBOSE_("");
    mms_handler_remove_callback(disp->handler, disp->handler_done_id);
    mms_connman_remove_callback(disp->cm, disp->connman_done_id);
    mms_dispatcher_drop_connection(disp);
    while ((task = g_queue_pop_head(disp->tasks)) != NULL) {
        task->delegate = NULL;
        mms_task_cancel(task);
        mms_task_unref(task);
    }
    g_queue_free(disp->tasks);
    mms_transfer_list_unref(disp->transfers);
    mms_settings_unref(disp->settings);
    mms_handler_unref(disp->handler);
    mms_connman_unref(disp->cm);

    /* Try to remove the message directory */
    remove(msg_dir);
    g_free(msg_dir);
}

/**
 * Reference counting. NULL argument is safely ignored.
 */
MMSDispatcher*
mms_dispatcher_ref(
    MMSDispatcher* disp)
{
    if (disp) {
        MMS_ASSERT(disp->ref_count > 0);
        g_atomic_int_inc(&disp->ref_count);
    }
    return disp;
}

void
mms_dispatcher_unref(
    MMSDispatcher* disp)
{
    if (disp) {
        MMS_ASSERT(disp->ref_count > 0);
        if (g_atomic_int_dec_and_test(&disp->ref_count)) {
            mms_dispatcher_finalize(disp);
            g_free(disp);
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
