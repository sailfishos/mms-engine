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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "test_connman.h"
#include "test_handler.h"
#include "test_util.h"

#include "mms_file_util.h"
#include "mms_lib_log.h"
#include "mms_lib_util.h"
#include "mms_settings.h"
#include "mms_dispatcher.h"

#include <gutil_log.h>
#include <gutil_macros.h>

#define DATA_DIR "data"

static TestOpt test_opt;

typedef struct test_desc {
    const char* name;
    const char* ind_file;
    const char* mmsid;
    MMS_READ_STATUS status;
} TestDesc;

typedef struct test {
    const TestDesc* desc;
    const MMSConfig* config;
    char* id;
    MMSDispatcherDelegate delegate;
    MMSConnMan* cm;
    MMSHandler* handler;
    MMSDispatcher* disp;
    GMappedFile* pdu_file;
    GMainLoop* loop;
} Test;

static const TestDesc read_tests[] = {
    {
        "ReadOK",
        "m-read-orig.ind",
        "BH24CBJJA40W1",
        MMS_READ_STATUS_READ
    },{
        "ReadUnexpected",
        "m-read-orig.ind",
        "UNKNOWN",
        MMS_READ_STATUS_INVALID
    },{
        "ReadDeleted",
        "m-read-orig.ind",
        "BH24CBJJA40W1",
        MMS_READ_STATUS_DELETED
    }
};

static
void
read_ind_finish(
    Test* test)
{
    const TestDesc* desc = test->desc;
    MMS_READ_STATUS rs = mms_handler_test_read_status(test->handler, test->id);

    g_assert_cmpint(rs, == ,desc->status);
    mms_handler_test_reset(test->handler);
    g_main_loop_quit(test->loop);
}

static
void
read_ind_done(
    MMSDispatcherDelegate* delegate,
    MMSDispatcher* dispatcher)
{
    Test* test = G_CAST(delegate,Test,delegate);

    if (!mms_handler_test_receive_pending(test->handler, NULL)) {
        read_ind_finish(test);
    }
}

static
void
run_test(
    gconstpointer data)
{
    const TestDesc* desc = data;
    char* ni = g_build_filename(DATA_DIR, desc->name, desc->ind_file, NULL);
    MMSSettings* settings;
    MMSConfig config;
    Test test;
    TestDirs dirs;
    GBytes* push;
    GError* error = NULL;

    mms_lib_default_config(&config);
    test_dirs_init(&dirs, "test_read_ind");
    config.root_dir = dirs.root;
    config.network_idle_secs = 0;
    config.attic_enabled = TRUE;

    memset(&test, 0, sizeof(test));
    test.config = &config;
    test.pdu_file = g_mapped_file_new(ni, FALSE, &error);
    g_assert(test.pdu_file);
    push = g_bytes_new_static(g_mapped_file_get_contents(test.pdu_file),
        g_mapped_file_get_length(test.pdu_file));

    settings = mms_settings_default_new(&config);
    test.desc = desc;
    test.cm = mms_connman_test_new();
    test.handler = mms_handler_test_new();
    test.disp = mms_dispatcher_new(settings, test.cm, test.handler, NULL);
    test.loop = g_main_loop_new(NULL, FALSE);
    test.delegate.fn_done = read_ind_done;
    mms_dispatcher_set_delegate(test.disp, &test.delegate);
    test.id = g_strdup(mms_handler_test_send_new(test.handler, "IMSI"));
    mms_handler_message_sent(test.handler, test.id, desc->mmsid);
    mms_settings_unref(settings);

    g_assert(mms_dispatcher_handle_push(test.disp, "Connection", push, &error));
    g_assert(mms_dispatcher_start(test.disp));

    test_run_loop(&test_opt, test.loop);

    mms_connman_test_close_connection(test.cm);
    mms_connman_unref(test.cm);
    mms_handler_unref(test.handler);
    mms_dispatcher_unref(test.disp);
    g_main_loop_unref(test.loop);
    g_mapped_file_unref(test.pdu_file);
    g_free(test.id);

    test_dirs_cleanup(&dirs, TRUE);

    g_bytes_unref(push);
    g_free(ni);
}

#define TEST_(x) "/ReadInd/" x

int main(int argc, char* argv[])
{
    int ret;
    guint i;

    mms_lib_init(argv[0]);
    g_test_init(&argc, &argv, NULL);
    test_init(&test_opt, &argc, argv);
    for (i = 0; i < G_N_ELEMENTS(read_tests); i++) {
        const TestDesc* test = read_tests + i;
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
