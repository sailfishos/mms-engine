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

#define MMS_MESSAGE_TYPE_NONE (0)

#define DATA_DIR "data/"

static TestOpt test_opt;

typedef struct test {
    const MMSConfig* config;
    MMSDispatcherDelegate delegate;
    MMSConnMan* cm;
    MMSHandler* handler;
    MMSDispatcher* disp;
    GBytes* notification_ind;
    GMappedFile* retrieve_conf;
    GMainLoop* loop;
    TestHttp* http;
} Test;

static
void
test_retrieve_no_proxy_done(
    MMSDispatcherDelegate* delegate,
    MMSDispatcher* dispatcher)
{
    Test* test = G_CAST(delegate,Test,delegate);
    const void* resp_data = NULL;
    gsize resp_len = 0;
    GBytes* reply = test_http_get_post_data(test->http);
    MMSPdu* pdu = g_new0(MMSPdu, 1);
    MMS_RECEIVE_STATE state = mms_handler_test_receive_state
        (test->handler, NULL);

    g_assert_cmpint(state, == ,MMS_RECEIVE_STATE_DECODING);
    g_assert(reply);
    resp_data = g_bytes_get_data(reply, &resp_len);
    g_assert(resp_len > 0);

    g_assert(mms_message_decode(resp_data, resp_len, pdu));
    g_assert_cmpint(pdu->type, == ,MMS_MESSAGE_TYPE_ACKNOWLEDGE_IND);
    mms_message_free(pdu);
    mms_handler_test_reset(test->handler);
    g_main_loop_quit(test->loop);
}

static
void
test_retrieve_no_proxy(
    void)
{
    MMSConfig config;
    MMSSettings* settings;
    Test test;
    TestDirs dirs;
    GError* error = NULL;
    guint port;
    char* port_string;
    char* push_data;
    gsize push_len;

    static const guint8 push_template[] = {
        0x8C,0x82,0x98,0x42,0x49,0x33,0x52,0x34,0x56,0x32,0x49,0x53,
        0x4C,0x52,0x34,0x31,0x40,0x78,0x6D,0x61,0x2E,0x37,0x32,0x34,
        0x2E,0x63,0x6F,0x6D,0x00,0x8D,0x91,0x86,0x80,0x88,0x05,0x81,
        0x03,0x03,0xF4,0x7E,0x89,0x19,0x80,0x2B,0x33,0x35,0x38,0x35,
        0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x2F,0x54,0x59,0x50,
        0x45,0x3D,0x50,0x4C,0x4D,0x4E,0x00,0x8A,0x80,0x8E,0x03,0x01,
        0xB9,0x7A,0x96,0x0D,0xEA,0x7F,0xD0,0x9F,0xD0,0xB8,0xD1,0x82,
        0xD0,0xB5,0xD1,0x80,0x00,0x83,0x68,0x74,0x74,0x70,0x3A,0x2F,
        0x2F,0x31,0x32,0x37,0x2E,0x30,0x2E,0x30,0x2E,0x31,0x3A
    };

    test_dirs_init(&dirs, "test_retrieve_no_proxy");
    mms_lib_default_config(&config);
    config.network_idle_secs = 0;
    config.root_dir = dirs.root;
    settings = mms_settings_default_new(&config);

    /* Initialize */
    memset(&test, 0, sizeof(test));
    test.config = &config;
    test.retrieve_conf = g_mapped_file_new(DATA_DIR "m-retrieve.conf", FALSE,
        &error);
    g_assert(test.retrieve_conf);
    test.cm = mms_connman_test_new();
    test.handler = mms_handler_test_new();
    test.disp = mms_dispatcher_new(settings, test.cm, test.handler, NULL);
    test.loop = g_main_loop_new(NULL, FALSE);
    test.delegate.fn_done = test_retrieve_no_proxy_done;
    mms_dispatcher_set_delegate(test.disp, &test.delegate);
    test.http = test_http_new(test.retrieve_conf, MMS_CONTENT_TYPE,
        SOUP_STATUS_OK);

    port = test_http_get_port(test.http);
    mms_connman_test_set_port(test.cm, port, FALSE);
    port_string = g_strdup_printf("%u", port);

    push_len = strlen(port_string) + 1 + sizeof(push_template);
    push_data = g_malloc(push_len);
    memcpy(push_data, push_template, sizeof(push_template));
    strcpy(push_data + sizeof(push_template), port_string);
    test.notification_ind = g_bytes_new(push_data, push_len);

    g_free(push_data);
    g_free(port_string);
    mms_settings_unref(settings);

    /* Start the dispatcher */
    g_assert(mms_dispatcher_handle_push(test.disp, "TestConnection",
        test.notification_ind, &error));
    g_assert(mms_dispatcher_start(test.disp));

    /* Run the loop */
    test_run_loop(&test_opt, test.loop);

    /* Done */
    test_http_close(test.http);
    test_http_unref(test.http);
    mms_connman_test_close_connection(test.cm);
    mms_connman_unref(test.cm);
    mms_handler_unref(test.handler);
    mms_dispatcher_unref(test.disp);
    g_main_loop_unref(test.loop);
    g_bytes_unref(test.notification_ind);
    g_mapped_file_unref(test.retrieve_conf);

    test_dirs_cleanup(&dirs, TRUE);
}

#define TEST_(x) "/RetrieveNoProxy/" x

int main(int argc, char* argv[])
{
    int ret;

    mms_lib_init(argv[0]);
    g_test_init(&argc, &argv, NULL);
    test_init(&test_opt, &argc, argv);
    g_test_add_func(TEST_("OK"), test_retrieve_no_proxy);
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
