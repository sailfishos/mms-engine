/*
 * Copyright (C) 2013-2018 Jolla Ltd.
 * Copyright (C) 2013-2018 Slava Monich <slava.monich@jolla.com>
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

#include "test_connman.h"
#include "test_handler.h"
#include "test_http.h"
#include "test_util.h"

#include "mms_codec.h"
#include "mms_file_util.h"
#include "mms_lib_log.h"
#include "mms_lib_util.h"
#include "mms_settings.h"
#include "mms_dispatcher.h"

#include <gutil_macros.h>
#include <gutil_log.h>
#include <libsoup/soup-status.h>

#define RET_OK      (0)
#define RET_ERR     (1)
#define RET_TIMEOUT (2)

#define DATA_DIR "data/"

#define TEST_TIMEOUT (10) /* seconds */

#define TEST_ARRAY_AND_COUNT(a) a, G_N_ELEMENTS(a)

typedef struct test_receive_state {
    const char* id;
    MMS_RECEIVE_STATE state;
} TestReceiveState;

typedef struct test_receive_state_event {
    char* id;
    MMS_RECEIVE_STATE state;
} TestReceiveStateEvent;

typedef struct test_message_files {
    const char* notification_ind;
    const char* retrieve_conf;
} TestMessageFiles;

typedef struct test_mapped_files {
    GMappedFile* notification_ind;
    GMappedFile* retrieve_conf;
} TestMappedFiles;

typedef struct test_desc {
    const char* name;
    const TestMessageFiles* message_files;
    unsigned int num_message_files;
    const TestReceiveState* receive_states;
    unsigned int num_receive_states;
    int flags;
} TestDesc;

typedef struct test {
    const TestDesc* desc;
    const MMSConfig* config;
    MMSDispatcherDelegate delegate;
    MMSConnMan* cm;
    MMSHandler* handler;
    MMSDispatcher* disp;
    GMainLoop* loop;
    GPtrArray* files;
    GPtrArray* receive_states;
    guint timeout_id;
    gulong receive_state_event_id;
    TestHttp* http;
    int ret;
} Test;

static const TestMessageFiles test1_files[] = {
    { "m-notification1.ind", "m-retrieve1.conf" },
    { "m-notification2.ind", "m-retrieve2.conf" }
};

static const TestReceiveState test1_receive_states[] = {
    { "1", MMS_RECEIVE_STATE_RECEIVING },
    { "1", MMS_RECEIVE_STATE_DECODING },
    { "2", MMS_RECEIVE_STATE_RECEIVING },
    { "2", MMS_RECEIVE_STATE_DECODING }
};

static const TestDesc retrieve_order_tests[] = {
    {
        "Order",
        TEST_ARRAY_AND_COUNT(test1_files),
        TEST_ARRAY_AND_COUNT(test1_receive_states)
    }
};

static
void
test_free_receive_state(
    gpointer data)
{
    TestReceiveStateEvent* event = data;
    g_free(event->id);
    g_free(event);
}

static
void
test_finish(
    Test* test)
{
    const TestDesc* desc = test->desc;
    const char* name = desc->name;
    if (test->ret == RET_OK) {
        guint n = test->receive_states->len;
        if (n != desc->num_receive_states) {
            GERR("Unexpected number of state changes %u", n);
            test->ret = RET_ERR;
        } else {
            guint i;
            for (i=0; i<n; i++) {
                TestReceiveStateEvent* event = test->receive_states->pdata[i];
                const TestReceiveState* expected = desc->receive_states + i;
                if (g_strcmp0(event->id, expected->id)) {
                    GERR("Unexpected task id %s at %u", event->id, i);
                    test->ret = RET_ERR;
                }
                if (event->state != expected->state) {
                    GERR("Unexpected state %d at %u", event->state, i);
                    test->ret = RET_ERR;
                }
            }
        }
    }
    GINFO("%s: %s", (test->ret == RET_OK) ? "OK" : "FAILED", name);
    mms_handler_test_reset(test->handler);
    g_signal_handler_disconnect(test->handler, test->receive_state_event_id);
    g_main_loop_quit(test->loop);
}

static
void
test_done(
    MMSDispatcherDelegate* delegate,
    MMSDispatcher* dispatcher)
{
    Test* test = G_CAST(delegate,Test,delegate);
    if (!mms_handler_test_receive_pending(test->handler, NULL)) {
        test_finish(test);
    }
}

static
gboolean
test_timeout(
    gpointer data)
{
    Test* test = data;
    test->timeout_id = 0;
    test->ret = RET_TIMEOUT;
    GINFO("%s TIMEOUT", test->desc->name);
    if (test->http) test_http_close(test->http);
    mms_connman_test_close_connection(test->cm);
    mms_dispatcher_cancel(test->disp, NULL);
    return FALSE;
}

static
void
test_receive_state_changed(
    MMSHandler* handler,
    const char* id,
    MMS_RECEIVE_STATE state,
    void* user_data)
{
    Test* test = user_data;
    TestReceiveStateEvent* event = g_new0(TestReceiveStateEvent, 1);
    event->id = g_strdup(id);
    event->state = state;
    g_ptr_array_add(test->receive_states, event);
}

static
void
test_free_mapped_files(
    gpointer data)
{
    TestMappedFiles* files = data;
    g_mapped_file_unref(files->notification_ind);
    g_mapped_file_unref(files->retrieve_conf);
    g_free(files);
}

static
gboolean
test_init(
    Test* test,
    const MMSConfig* config,
    const TestDesc* desc,
    gboolean debug)
{
    guint i;
    MMSSettings* settings = mms_settings_default_new(config);

    GDEBUG(">>>>>>>>>> %s <<<<<<<<<<", desc->name);
    memset(test, 0, sizeof(*test));
    test->config = config;
    test->files = g_ptr_array_new_full(0, test_free_mapped_files);
    test->receive_states = g_ptr_array_new_full(0, test_free_receive_state);
    test->http = test_http_new(NULL, NULL, SOUP_STATUS_NONE);

    /* Open files */
    for (i=0; i<desc->num_message_files; i++) {
        GError* err = NULL;
        const char* subdir = desc->name;
        char* ni = g_strconcat(DATA_DIR, subdir, "/",
            desc->message_files[i].notification_ind, NULL);
        char* rc = g_strconcat(DATA_DIR, subdir, "/",
            desc->message_files[i].retrieve_conf, NULL);
        TestMappedFiles* files = g_new0(TestMappedFiles, 1);
        files->notification_ind = g_mapped_file_new(ni, FALSE, &err);
        if (err) {
            GERR("%s", GERRMSG(err));
            g_error_free(err);
            err = NULL;
        }
        files->retrieve_conf = g_mapped_file_new(rc, FALSE, &err);
        test_http_add_response(test->http, files->retrieve_conf,
            MMS_CONTENT_TYPE, SOUP_STATUS_OK);
        /* Add empty responses for ack */
        test_http_add_response(test->http, NULL, NULL, SOUP_STATUS_OK);
        if (err) {
            GERR("%s", GERRMSG(err));
            g_error_free(err);
            err = NULL;
        }
        g_ptr_array_add(test->files, files);
        g_free(ni);
        g_free(rc);
    }

    test->desc = desc;
    test->cm = mms_connman_test_new();
    test->handler = mms_handler_test_new();
    test->disp = mms_dispatcher_new(settings, test->cm, test->handler, NULL);
    test->loop = g_main_loop_new(NULL, FALSE);
    if (!debug) {
        test->timeout_id = g_timeout_add_seconds(TEST_TIMEOUT,
            test_timeout, test);
    }
    test->delegate.fn_done = test_done;
    mms_dispatcher_set_delegate(test->disp, &test->delegate);
    mms_connman_test_set_port(test->cm, test_http_get_port(test->http), TRUE);

    test->receive_state_event_id =
        mms_handler_test_add_receive_state_fn(test->handler,
            test_receive_state_changed, test);

    test->ret = RET_OK;
    return TRUE;
}

static
void
test_finalize(
    Test* test)
{
    if (test->timeout_id) {
        g_source_remove(test->timeout_id);
        test->timeout_id = 0;
    }
    if (test->http) {
        test_http_close(test->http);
        test_http_unref(test->http);
    }
    mms_connman_test_close_connection(test->cm);
    mms_connman_unref(test->cm);
    mms_handler_unref(test->handler);
    mms_dispatcher_unref(test->disp);
    g_main_loop_unref(test->loop);
    g_ptr_array_unref(test->files);
    g_ptr_array_unref(test->receive_states);
}

static
int
test_order_once(
    const MMSConfig* config,
    const TestDesc* desc,
    gboolean debug)
{
    Test test;
    if (test_init(&test, config, desc, debug)) {
        guint i;
        for (i=0; i<test.files->len && test.ret == RET_OK; i++) {
            TestMappedFiles* files = test.files->pdata[i];
            GError* error = NULL;
            GBytes* push = g_bytes_new_static(
                g_mapped_file_get_contents(files->notification_ind),
                g_mapped_file_get_length(files->notification_ind));
            if (!mms_dispatcher_handle_push(test.disp, "TestConnection",
                push, &error)) {
                g_error_free(error);
                test.ret = RET_ERR;
            }
            g_bytes_unref(push);
        }
        if (test.ret == RET_OK) {
            if (mms_dispatcher_start(test.disp)) {
                g_main_loop_run(test.loop);
            } else {
                test.ret = RET_ERR;
            }
        }
        if (test.ret != RET_OK) {
            GINFO("FAILED: %s", desc->name);
        }
        test_finalize(&test);
        return test.ret;
    } else {
        return RET_ERR;
    }
}

static
int
test_order(
    const MMSConfig* config,
    const char* name,
    gboolean debug)
{
    int i, ret;
    if (name) {
        const TestDesc* found = NULL;
        for (i=0, ret = RET_ERR; i<G_N_ELEMENTS(retrieve_order_tests); i++) {
            const TestDesc* test = retrieve_order_tests + i;
            if (!strcmp(test->name, name)) {
                ret = test_order_once(config, test, debug);
                found = test;
                break;
            }
        }
        if (!found) GERR("No such test: %s", name);
    } else {
        for (i=0, ret = RET_OK; i<G_N_ELEMENTS(retrieve_order_tests); i++) {
            int ret2 = test_order_once(config, retrieve_order_tests + i, debug);
            if (ret == RET_OK && ret2 != RET_OK) ret = ret2;
        }
    }
    return ret;
}

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    gboolean keep_temp = FALSE;
    gboolean verbose = FALSE;
    gboolean debug = FALSE;
    GError* error = NULL;
    GOptionContext* options;
    GOptionEntry entries[] = {
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { "keep", 'k', 0, G_OPTION_ARG_NONE, &keep_temp,
          "Keep temporary files", NULL },
        { "debug", 'd', 0, G_OPTION_ARG_NONE, &debug,
          "Disable timeout for debugging", NULL },
        { NULL }
    };

    options = g_option_context_new("[TEST] - MMS task order test");
    g_option_context_add_main_entries(options, entries, NULL);
    if (g_option_context_parse(options, &argc, &argv, &error)) {
        const char* test = "test_retrieve_order";
        MMSConfig config;
        TestDirs dirs;
 
        mms_lib_init(argv[0]);
        gutil_log_default.name = test;
        if (verbose) {
            gutil_log_default.level = GLOG_LEVEL_VERBOSE;
        } else {
            gutil_log_timestamp = FALSE;
            gutil_log_default.level = GLOG_LEVEL_INFO;
            mms_task_http_log.level =
            mms_task_decode_log.level =
            mms_task_retrieve_log.level =
            mms_task_notification_log.level = GLOG_LEVEL_NONE;
        }

        test_dirs_init(&dirs, test);
        mms_lib_default_config(&config);
        config.root_dir = dirs.root;
        config.keep_temp_files = keep_temp;
        config.network_idle_secs = 0;
        if (argc < 2) {
            ret = test_order(&config, NULL, debug);
        } else {
            int i;
            for (i=1, ret = RET_OK; i<argc; i++) {
                int test_status =  test_order(&config, argv[i], debug);
                if (ret == RET_OK && test_status != RET_OK) ret = test_status;
            }
        }

        test_dirs_cleanup(&dirs, TRUE);
        mms_lib_deinit();
    } else {
        fprintf(stderr, "%s\n", GERRMSG(error));
        g_error_free(error);
        ret = RET_ERR;
    }
    g_option_context_free(options);
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
