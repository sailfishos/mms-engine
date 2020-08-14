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
#include <gio/gio.h>
#include <libsoup/soup-status.h>

#define DATA_DIR "data"

static TestOpt test_opt;

typedef struct test_desc {
    const char* name;
    const MMSAttachmentInfo* parts;
    int nparts;
    gsize size_limit;
    const char* subject;
    const char* to;
    const char* cc;
    const char* bcc;
    const char* imsi;
    unsigned int flags;
    const char* resp_file;
    const char* resp_type;
    unsigned int resp_status;
    MMS_SEND_STATE expected_state;
    const char* details;
    const char* msgid;
} TestDesc;

#define TEST_FLAG_CANCEL                  (0x1000)
#define TEST_FLAG_NO_SIM                  (0x2000)
#define TEST_FLAG_DONT_CONVERT_TO_UTF8    (0x4000)
#define TEST_FLAG_REQUEST_DELIVERY_REPORT MMS_SEND_FLAG_REQUEST_DELIVERY_REPORT
#define TEST_FLAG_REQUEST_READ_REPORT     MMS_SEND_FLAG_REQUEST_READ_REPORT

/* ASSERT that test and dispatcher flags don't interfere with each other */ 
#define TEST_DISPATCHER_FLAGS       (\
  TEST_FLAG_REQUEST_DELIVERY_REPORT |\
  TEST_FLAG_REQUEST_READ_REPORT     )
#define TEST_PRIVATE_FLAGS (\
  TEST_FLAG_CANCEL         |\
  TEST_FLAG_NO_SIM         |\
  TEST_FLAG_DONT_CONVERT_TO_UTF8)
G_STATIC_ASSERT(!(TEST_PRIVATE_FLAGS & TEST_DISPATCHER_FLAGS));

typedef struct test {
    const TestDesc* desc;
    const MMSConfig* config;
    MMSAttachmentInfo* parts;
    char** files;
    MMSDispatcherDelegate delegate;
    MMSConnMan* cm;
    MMSHandler* handler;
    MMSDispatcher* disp;
    GMainLoop* loop;
    TestHttp* http;
    char* id;
    GMappedFile* resp_file;
} Test;

static const MMSAttachmentInfo test_files_accept [] = {
    { "smil", "application/smil;charset=us-ascii", NULL },
    { "0001.jpg", "image/jpeg;name=0001.jpg", "image" },
    { "test.txt", "text/plain;charset=utf-8;name=wrong.name", "text" }
};

static const MMSAttachmentInfo test_files_accept_no_ext [] = {
    { "smil", NULL, NULL },
    { "0001", NULL, "image1" },
    { "0001", "", "image2" },
    { "test.text", "text/plain;charset=utf-8", "text" }
};

static const MMSAttachmentInfo test_files_reject [] = {
    { "0001.png", "image/png", "image" },
    { "test.txt", "text/plain", "text" }
};

static const MMSAttachmentInfo test_txt [] = {
    { "test.txt", NULL, "text" }
};

#define ATTACHMENTS(a) a, G_N_ELEMENTS(a)

static const TestDesc send_tests[] = {
    {
        "Accept",
        ATTACHMENTS(test_files_accept),
        0,
        "Test of successful delivery",
        "+1234567890",
        "+2345678901,+3456789012",
        "+4567890123",
        "IMSI",
        0,
        "m-send.conf",
        MMS_CONTENT_TYPE,
        SOUP_STATUS_OK,
        MMS_SEND_STATE_SENDING,
        NULL,
        "TestMessageId"
    },{
        "AcceptNoExt",
        ATTACHMENTS(test_files_accept_no_ext),
        0,
        "Test of successful delivery (no extensions)",
        "+1234567890",
        "+2345678901,+3456789012",
        "+4567890123",
        "IMSI",
        0,
        "m-send.conf",
        MMS_CONTENT_TYPE,
        SOUP_STATUS_OK,
        MMS_SEND_STATE_SENDING,
        NULL,
        "TestMessageId"
    },{
        "ServiceDenied",
        ATTACHMENTS(test_files_reject),
        0,
        "Rejection test",
        "+1234567890",
        NULL,
        NULL,
        NULL,
        0,
        "m-send.conf",
        MMS_CONTENT_TYPE,
        SOUP_STATUS_OK,
        MMS_SEND_STATE_REFUSED,
        "Unable to send",
        NULL
    },{
        "Failure",
        ATTACHMENTS(test_files_reject),
        0,
        "Failure test",
        "+1234567890",
        NULL,
        NULL,
        NULL,
        0,
        "m-send.conf",
        MMS_CONTENT_TYPE,
        SOUP_STATUS_OK,
        MMS_SEND_STATE_SEND_ERROR,
        NULL,
        NULL
    },{
        "UnparsableResp",
        ATTACHMENTS(test_txt),
        0,
        "Testing unparsable response",
        "+1234567890",
        NULL,
        NULL,
        NULL,
        0,
        "m-send.conf",
        MMS_CONTENT_TYPE,
        SOUP_STATUS_OK,
        MMS_SEND_STATE_SEND_ERROR,
        NULL,
        NULL
    },{
        "UnexpectedResp",
        ATTACHMENTS(test_txt),
        0,
        "Testing unexpected response",
        "+1234567890",
        NULL,
        NULL,
        NULL,
        0,
        "m-send.conf",
        MMS_CONTENT_TYPE,
        SOUP_STATUS_OK,
        MMS_SEND_STATE_SEND_ERROR,
        NULL,
        NULL
    },{
        "EmptyMessageID",
        ATTACHMENTS(test_txt),
        0,
        "Testing empty message id",
        "+1234567890",
        NULL,
        NULL,
        NULL,
        0,
        "m-send.conf",
        MMS_CONTENT_TYPE,
        SOUP_STATUS_OK,
        MMS_SEND_STATE_SEND_ERROR,
        NULL,
        NULL
    },{
        "Cancel",
        ATTACHMENTS(test_txt),
        0,
        "Failure test",
        "+1234567890",
        NULL,
        NULL,
        NULL,
        TEST_FLAG_CANCEL,
        NULL,
        NULL,
        SOUP_STATUS_INTERNAL_SERVER_ERROR,
        MMS_SEND_STATE_SEND_ERROR,
        NULL,
        NULL
    },{
        "TooBig",
        ATTACHMENTS(test_txt),
        100,
        "Size limit test",
        "+1234567890",
        NULL,
        NULL,
        NULL,
        0,
        NULL,
        NULL,
        SOUP_STATUS_INTERNAL_SERVER_ERROR,
        MMS_SEND_STATE_TOO_BIG,
        NULL,
        NULL
    },{
        "DontConvertToUtf8",
        ATTACHMENTS(test_txt),
        100,
        "No conversion to UTF-8",
        "+1234567890",
        NULL,
        NULL,
        NULL,
        TEST_FLAG_DONT_CONVERT_TO_UTF8,
        NULL,
        NULL,
        SOUP_STATUS_INTERNAL_SERVER_ERROR,
        MMS_SEND_STATE_TOO_BIG,
        NULL,
        NULL
    },{
        "NoSim",
        NULL, 0,
        0,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        TEST_FLAG_NO_SIM,
        NULL,
        NULL,
        0,
        MMS_SEND_STATE_SEND_ERROR,
        NULL,
        NULL
    }
};

static
void
test_finish(
    Test* test)
{
    const TestDesc* desc = test->desc;
    MMSHandler* handler = test->handler;
    MMS_SEND_STATE state = mms_handler_test_send_state(handler, test->id);
    const char* details = mms_handler_test_send_details(handler, test->id);

    g_assert_cmpint(state, == ,desc->expected_state);
    g_assert_cmpstr(details, == ,desc->details);
    if (desc->msgid) {
        const char* msgid = mms_handler_test_send_msgid(handler, test->id);

        if (msgid) {
            g_assert_cmpstr(msgid, == ,desc->msgid);
        } else {
            g_assert(!desc->msgid);
        }
    }
    mms_handler_test_reset(test->handler);
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
void
test_cancel(
    void* param)
{
    Test* test = param;

    GDEBUG("Cancelling %s", test->id);
    mms_dispatcher_cancel(test->disp, test->id);
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
    guint port, i;
    const char* id;
    char* imsi;
    char* imsi2;

    test_dirs_init(&dirs, "test_send");
    mms_lib_default_config(&config);
    config.root_dir = dirs.root;
    config.keep_temp_files = (test_opt.flags & TEST_FLAG_DEBUG) != 0;
    config.network_idle_secs = 0;
    config.attic_enabled = TRUE;
    if (desc->flags & TEST_FLAG_DONT_CONVERT_TO_UTF8) {
        config.convert_to_utf8 = FALSE;
    }

    settings = mms_settings_default_new(&config);

    memset(&test, 0, sizeof(test));
    test.config = &config;

    /* Initialize the test */
    memset(&test, 0, sizeof(test));
    if (desc->resp_file) {
        char* f = g_build_filename(DATA_DIR, desc->name, desc->resp_file, NULL);

        test.resp_file = g_mapped_file_new(f, FALSE, &error);
        g_assert(test.resp_file);
        g_free(f);
    }
    g_assert(!desc->resp_file || test.resp_file);
    test.parts = g_new0(MMSAttachmentInfo, desc->nparts);
    test.files = g_new0(char*, desc->nparts);
    for (i = 0; i < desc->nparts; i++) {
        test.files[i] = g_build_filename(DATA_DIR, desc->name,
            desc->parts[i].file_name, NULL);
        test.parts[i] = desc->parts[i];
        test.parts[i].file_name = test.files[i];
    }
    test.config = &config;
    test.desc = desc;
    test.cm = mms_connman_test_new();
    test.handler = mms_handler_test_new();
    test.disp = mms_dispatcher_new(settings, test.cm, test.handler, NULL);
    test.loop = g_main_loop_new(NULL, FALSE);
    test.delegate.fn_done = test_done;
    mms_dispatcher_set_delegate(test.disp, &test.delegate);
    test.http = test_http_new(test.resp_file, desc->resp_type,
        desc->resp_status);
    port = test_http_get_port(test.http);
    mms_connman_test_set_port(test.cm, port, TRUE);
    if (desc->flags & TEST_FLAG_NO_SIM) {
        mms_connman_test_set_default_imsi(test.cm, NULL);
    }
    if (desc->flags & TEST_FLAG_CANCEL) {
        mms_connman_test_set_connect_callback(test.cm, test_cancel, &test);
    }
    if (desc->size_limit) {
        MMSSettingsSimData sim_settings;

        mms_settings_sim_data_default(&sim_settings);
        sim_settings.size_limit = desc->size_limit;
        mms_settings_set_sim_defaults(settings, NULL);
        mms_settings_set_sim_defaults(settings, &sim_settings);
    }
    mms_settings_unref(settings);

    /* Send message and run the event loop */
    imsi = desc->imsi ? g_strdup(desc->imsi) :
        mms_connman_default_imsi(test.cm);
    id = mms_handler_test_send_new(test.handler, imsi);
    imsi2 = mms_dispatcher_send_message(test.disp, id, desc->imsi,
        desc->to, desc->cc, desc->bcc, desc->subject,
        desc->flags & TEST_DISPATCHER_FLAGS, test.parts,
        desc->nparts, &error);
    test.id = g_strdup(id);
    if (imsi2) {
        g_assert(!desc->imsi || !g_strcmp0(desc->imsi, imsi2));
        g_assert(mms_dispatcher_start(test.disp));
        test_run_loop(&test_opt, test.loop);
    } else {
        g_assert(desc->flags & TEST_FLAG_NO_SIM);
        g_assert(error);
        g_error_free(error);
    }

    /* Done */
    g_free(imsi);
    g_free(imsi2);

    test_http_close(test.http);
    test_http_unref(test.http);
    mms_connman_test_close_connection(test.cm);
    mms_connman_unref(test.cm);
    mms_handler_unref(test.handler);
    mms_dispatcher_unref(test.disp);
    g_main_loop_unref(test.loop);
    if (test.resp_file) g_mapped_file_unref(test.resp_file);
    for (i = 0; i < test.desc->nparts; i++) g_free(test.files[i]);
    g_free(test.files);
    g_free(test.parts);
    g_free(test.id);

    test_dirs_cleanup(&dirs, TRUE);
}

#define TEST_(x) "/Send/" x

int main(int argc, char* argv[])
{
    int ret;
    guint i;

    mms_lib_init(argv[0]);
    g_test_init(&argc, &argv, NULL);
    test_init(&test_opt, &argc, argv);
    for (i = 0; i < G_N_ELEMENTS(send_tests); i++) {
        const TestDesc* test = send_tests + i;
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
