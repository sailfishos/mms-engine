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

#define DATA_DIR "data"

static TestOpt test_opt;

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
test_retrieve_order_finish(
    Test* test)
{
    const TestDesc* desc = test->desc;
    const guint n = test->receive_states->len;
    guint i;

    g_assert_cmpuint(n, == ,desc->num_receive_states);
    for (i = 0; i < n; i++) {
        TestReceiveStateEvent* event = test->receive_states->pdata[i];
        const TestReceiveState* expected = desc->receive_states + i;

        g_assert_cmpstr(event->id, == ,expected->id);
        g_assert_cmpint(event->state, == ,expected->state);
    }

    mms_handler_test_reset(test->handler);
    g_signal_handler_disconnect(test->handler, test->receive_state_event_id);
    g_main_loop_quit(test->loop);
}

static
void
test_retrieve_order_done(
    MMSDispatcherDelegate* delegate,
    MMSDispatcher* dispatcher)
{
    Test* test = G_CAST(delegate,Test,delegate);

    if (!mms_handler_test_receive_pending(test->handler, NULL)) {
        test_retrieve_order_finish(test);
    }
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
void
run_test(
    gconstpointer data)
{
    const TestDesc* desc = data;
    MMSConfig config;
    MMSSettings* settings;
    GError* error = NULL;
    TestDirs dirs;
    Test test;
    guint i;

    test_dirs_init(&dirs, "test_retrieve_order");
    mms_lib_default_config(&config);
    config.root_dir = dirs.root;
    config.keep_temp_files = (test_opt.flags & TEST_FLAG_DEBUG) != 0;
    config.network_idle_secs = 0;
    config.attic_enabled = TRUE;
    settings = mms_settings_default_new(&config);

    memset(&test, 0, sizeof(test));
    test.config = &config;
    test.files = g_ptr_array_new_full(0, test_free_mapped_files);
    test.receive_states = g_ptr_array_new_full(0, test_free_receive_state);
    test.http = test_http_new(NULL, NULL, SOUP_STATUS_NONE);

    /* Open files */
    for (i = 0; i < desc->num_message_files; i++) {
        const char* subdir = desc->name;
        char* ni = g_build_filename(DATA_DIR, subdir,
            desc->message_files[i].notification_ind, NULL);
        char* rc = g_build_filename(DATA_DIR, subdir,
            desc->message_files[i].retrieve_conf, NULL);
        TestMappedFiles* files = g_new0(TestMappedFiles, 1);

        files->notification_ind = g_mapped_file_new(ni, FALSE, &error);
        g_assert(!error);
        files->retrieve_conf = g_mapped_file_new(rc, FALSE, &error);
        g_assert(!error);
        test_http_add_response(test.http, files->retrieve_conf,
            MMS_CONTENT_TYPE, SOUP_STATUS_OK);

        /* Add empty responses for ack */
        test_http_add_response(test.http, NULL, NULL, SOUP_STATUS_OK);
        g_ptr_array_add(test.files, files);
        g_free(ni);
        g_free(rc);
    }

    test.desc = desc;
    test.cm = mms_connman_test_new();
    test.handler = mms_handler_test_new();
    test.disp = mms_dispatcher_new(settings, test.cm, test.handler, NULL);
    test.loop = g_main_loop_new(NULL, FALSE);
    test.delegate.fn_done = test_retrieve_order_done;
    mms_dispatcher_set_delegate(test.disp, &test.delegate);
    mms_connman_test_set_port(test.cm, test_http_get_port(test.http), TRUE);

    test.receive_state_event_id =
        mms_handler_test_add_receive_state_fn(test.handler,
            test_receive_state_changed, &test);

    /* Simulate push and run the event loop */
    for (i = 0; i < test.files->len; i++) {
        TestMappedFiles* files = test.files->pdata[i];
        GBytes* push = g_bytes_new_static(
            g_mapped_file_get_contents(files->notification_ind),
            g_mapped_file_get_length(files->notification_ind));

        g_assert(mms_dispatcher_handle_push(test.disp, "TestConnection", push,
            &error));
        g_bytes_unref(push);
    }
    g_assert(mms_dispatcher_start(test.disp));
    test_run_loop(&test_opt, test.loop);

    /* Done */
    test_http_close(test.http);
    test_http_unref(test.http);
    mms_connman_test_close_connection(test.cm);
    mms_connman_unref(test.cm);
    mms_handler_unref(test.handler);
    mms_dispatcher_unref(test.disp);
    g_main_loop_unref(test.loop);
    g_ptr_array_unref(test.files);
    g_ptr_array_unref(test.receive_states);

    mms_settings_unref(settings);
    test_dirs_cleanup(&dirs, TRUE);
}

#define TEST_(x) "/RetrieveOrder/" x

int main(int argc, char* argv[])
{
    int ret;
    guint i;

    mms_lib_init(argv[0]);
    g_test_init(&argc, &argv, NULL);
    test_init(&test_opt, &argc, argv);
    for (i = 0; i < G_N_ELEMENTS(retrieve_order_tests); i++) {
        const TestDesc* test = retrieve_order_tests + i;
        char* name = g_strdup_printf(TEST_("%s"), test->name);

        g_test_add_data_func(name, test, run_test);
        g_free(name);
    }
    ret = g_test_run();
    mms_lib_deinit();
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
