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
#include "test_http.h"
#include "test_util.h"

#include "mms_codec.h"
#include "mms_lib_log.h"
#include "mms_lib_util.h"
#include "mms_settings.h"
#include "mms_dispatcher.h"

#include <gutil_macros.h>
#include <gutil_log.h>

#include <gio/gio.h>
#include <libsoup/soup-status.h>

#define TEST_IMSI    "IMSI"

static TestOpt test_opt;

typedef struct test_desc {
    const char* name;
    MMSReadStatus status;
    const char* phone;
    enum mms_message_read_status rr_status;
    const char* to;
} TestDesc;

typedef struct test {
    const TestDesc* desc;
    MMSDispatcherDelegate delegate;
    MMSConnMan* cm;
    MMSHandler* handler;
    MMSDispatcher* disp;
    GMainLoop* loop;
    const char* imsi;
    char* id;
    TestHttp* http;
} Test;

static const TestDesc tests[] = {
    {
        "Read",
        MMS_READ_STATUS_READ,
        "+358501111111",
        MMS_MESSAGE_READ_STATUS_READ,
        "+358501111111/TYPE=PLMN"
    },{
        "Deleted",
        MMS_READ_STATUS_DELETED,
        "+358501111111/TYPE=PLMN",
        MMS_MESSAGE_READ_STATUS_DELETED,
        "+358501111111/TYPE=PLMN"
    }
};

static
void
read_report_done(
    MMSDispatcherDelegate* delegate,
    MMSDispatcher* dispatcher)
{
    Test* test = G_CAST(delegate,Test,delegate);
    const TestDesc* desc = test->desc;
    const void* resp_data = NULL;
    gsize resp_len = 0;
    GBytes* reply = test_http_get_post_data(test->http);

    if (reply) resp_data = g_bytes_get_data(reply, &resp_len);
    if (resp_len > 0) {
        MMSPdu* pdu = g_new0(MMSPdu, 1);
        MMS_READ_REPORT_STATUS status;

        g_assert(mms_message_decode(resp_data, resp_len, pdu));
        g_assert_cmpint(pdu->type, == ,MMS_MESSAGE_TYPE_READ_REC_IND);
        g_assert_cmpint(pdu->ri.rr_status, == ,desc->rr_status);
        g_assert_cmpstr(pdu->ri.to, == ,desc->to);

        status = mms_handler_test_read_report_status(test->handler, test->id);
        g_assert_cmpint(status, == ,MMS_READ_REPORT_STATUS_OK);
        mms_message_free(pdu);
    }
    g_main_loop_quit(test->loop);
}

static
void
run_test(
    gconstpointer data)
{
    const TestDesc* desc = data;
    Test test;
    TestDirs dirs;
    MMSSettings* settings;
    MMSConfig config;
    GError* error = NULL;

    mms_lib_default_config(&config);
    test_dirs_init(&dirs, "test_read_report");
    config.root_dir = dirs.root;
    config.network_idle_secs = 0;
    config.attic_enabled = TRUE;

    settings = mms_settings_default_new(&config);
    test.desc = desc;
    test.cm = mms_connman_test_new();
    test.handler = mms_handler_test_new();
    test.disp = mms_dispatcher_new(settings, test.cm, test.handler, NULL);
    test.loop = g_main_loop_new(NULL, FALSE);
    test.delegate.fn_done = read_report_done;
    mms_dispatcher_set_delegate(test.disp, &test.delegate);
    test.http = test_http_new(NULL, NULL, SOUP_STATUS_OK);
    test.id = g_strdup(mms_handler_test_receive_new(test.handler, TEST_IMSI));
    mms_connman_test_set_port(test.cm, test_http_get_port(test.http), TRUE);
    mms_settings_unref(settings);

    g_assert(mms_dispatcher_send_read_report(test.disp, test.id, TEST_IMSI,
        "MessageID", desc->phone, desc->status, &error));
    g_assert(mms_dispatcher_start(test.disp));

    test_run_loop(&test_opt, test.loop);

    g_free(test.id);
    test_http_close(test.http);
    test_http_unref(test.http);
    mms_connman_test_close_connection(test.cm);
    mms_connman_unref(test.cm);
    mms_handler_unref(test.handler);
    mms_dispatcher_unref(test.disp);
    g_main_loop_unref(test.loop);

    test_dirs_cleanup(&dirs, TRUE);
}

#define TEST_(x) "/ReadReport/" x

int main(int argc, char* argv[])
{
    int ret;
    guint i;

    mms_lib_init(argv[0]);
    g_test_init(&argc, &argv, NULL);
    test_init(&test_opt, &argc, argv);
    for (i = 0; i < G_N_ELEMENTS(tests); i++) {
        const TestDesc* test = tests + i;
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
