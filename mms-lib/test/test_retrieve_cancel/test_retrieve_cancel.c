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

#include "test_util.h"
#include "test_connman.h"
#include "test_handler.h"

#include "mms_lib_util.h"
#include "mms_settings.h"
#include "mms_dispatcher.h"

#include <gutil_log.h>
#include <gutil_macros.h>

static TestOpt test_opt;

typedef struct test_desc {
    const char* name;
    const guint8* pdu;
    gsize pdusize;
    int retry_secs;
    mms_handler_test_prenotify_fn prenotify_fn;
    mms_handler_test_postnotify_fn postnotify_fn;
} TestDesc;

typedef struct test {
    const TestDesc* desc;
    MMSDispatcherDelegate delegate;
    MMSConnMan* cm;
    MMSHandler* handler;
    MMSDispatcher* disp;
    GBytes* pdu;
    GMainLoop* loop;
    guint cancel_id;
    char* id;
} Test;

static
void
test_done(
    MMSDispatcherDelegate* delegate,
    MMSDispatcher* dispatcher)
{
    Test* test = G_CAST(delegate,Test,delegate);

    g_main_loop_quit(test->loop);
}

static
gboolean
test_cancel(
    void* param)
{
    Test* test = param;

    test->cancel_id = 0;
    GDEBUG("Asynchronous cancel %s", test->id ? test->id : "all");
    mms_dispatcher_cancel(test->disp, test->id);
    return G_SOURCE_REMOVE;
}

static
void
test_postnotify_cancel_async(
    MMSHandler* handler,
    const char* id,
    void* param)
{
    Test* test = param;

    GASSERT(!test->id);
    GASSERT(!test->cancel_id);
    GDEBUG("Scheduling asynchronous cancel for %s", id);
    test->id = g_strdup(id);
    test->cancel_id = g_idle_add_full(G_PRIORITY_HIGH, test_cancel, test, NULL);
}

static
void
test_postnotify_cancel(
    MMSHandler* handler,
    const char* id,
    void* param)
{
    Test* test = param;

    GDEBUG("Cancel all");
    mms_dispatcher_cancel(test->disp, NULL);
}

static
gboolean
test_prenotify_cancel_async(
    MMSHandler* handler,
    const char* imsi,
    const char* from,
    const char* subject,
    time_t expiry,
    GBytes* data,
    void* param)
{
    Test* test = param;

    GASSERT(!test->cancel_id);
    /* High priority item gets executed before notification is completed */
    GDEBUG("Scheduling asynchronous cancel");
    test->cancel_id = g_idle_add_full(G_PRIORITY_HIGH, test_cancel, test, NULL);
    return TRUE;
}

static
void
run_test(
    gconstpointer data)
{
    const TestDesc* desc = data;
    Test test;
    MMSConfig config;
    MMSSettings* settings;
    GError* error = NULL;

    mms_lib_default_config(&config);
    config.root_dir = "."; /* Dispatcher will attempt to create it */
    config.retry_secs = desc->retry_secs;

    memset(&test, 0, sizeof(test));
    settings = mms_settings_default_new(&config);
    test.desc = desc;
    test.cm = mms_connman_test_new();
    test.handler = mms_handler_test_new();
    test.disp = mms_dispatcher_new(settings, test.cm, test.handler, NULL);
    test.pdu = g_bytes_new_static(desc->pdu, desc->pdusize);
    test.loop = g_main_loop_new(NULL, FALSE);
    test.delegate.fn_done = test_done;
    mms_dispatcher_set_delegate(test.disp, &test.delegate);
    mms_settings_unref(settings);
    if (desc->prenotify_fn) {
        mms_handler_test_set_prenotify_fn(test.handler,
            desc->prenotify_fn, &test);
    }
    if (desc->postnotify_fn) {
        mms_handler_test_set_postnotify_fn(test.handler,
            desc->postnotify_fn, &test);
    }

    g_assert(mms_dispatcher_handle_push(test.disp, "IMSI", test.pdu, &error));
    g_assert(mms_dispatcher_start(test.disp));
    test_run_loop(&test_opt, test.loop);

    mms_connman_unref(test.cm);
    mms_handler_unref(test.handler);
    mms_dispatcher_unref(test.disp);
    g_bytes_unref(test.pdu);
    g_main_loop_unref(test.loop);
    g_free(test.id);
}

/*
 * WSP header:
 *   application/vnd.wap.mms-message
 * MMS headers:
 *   X-Mms-Message-Type: M-Notification.ind
 *   X-Mms-Transaction-Id: Ad0b9pXNC
 *   X-Mms-MMS-Version: 1.2
 *   From: +358540000000/TYPE=PLMN
 *   X-Mms-Delivery-Report: No
 *   X-Mms-Message-Class: Personal
 *   X-Mms-Message-Size: 137105
 *   X-Mms-Expiry: +259199 sec
 *   X-Mms-Content-Location: http://mmsc42:10021/mmsc/4_2?Ad0b9pXNC
 */
static const guint8 plus_259199_sec[] = {
    0x8c,0x82,0x98,0x41,0x64,0x30,0x62,0x39,0x70,0x58,0x4e,0x43,
    0x00,0x8d,0x92,0x89,0x19,0x80,0x2b,0x33,0x35,0x38,0x35,0x34,
    0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x2f,0x54,0x59,0x50,0x45,
    0x3d,0x50,0x4c,0x4d,0x4e,0x00,0x86,0x81,0x8a,0x80,0x8e,0x03,
    0x02,0x17,0x91,0x88,0x05,0x81,0x03,0x03,0xf4,0x7f,0x83,0x68,
    0x74,0x74,0x70,0x3a,0x2f,0x2f,0x6d,0x6d,0x73,0x63,0x34,0x32,
    0x3a,0x31,0x30,0x30,0x32,0x31,0x2f,0x6d,0x6d,0x73,0x63,0x2f,
    0x34,0x5f,0x32,0x3f,0x41,0x64,0x30,0x62,0x39,0x70,0x58,0x4e,
    0x43,0x00
};

/*
 * WSP header:
 *   application/vnd.wap.mms-message
 * MMS headers:
 *   X-Mms-Message-Type: M-Notification.ind
 *   X-Mms-Transaction-Id: Ad0b9pXNC
 *   X-Mms-MMS-Version: 1.2
 *   From: +358540000000/TYPE=PLMN
 *   X-Mms-Delivery-Report: No
 *   X-Mms-Message-Class: Personal
 *   X-Mms-Message-Size: 137105
 *   X-Mms-Expiry: +1 sec
 *   X-Mms-Content-Location: http://mmsc42:10021/mmsc/4_2?Ad0b9pXNC
 */
static const guint8 plus_1_sec[] = {
    0x8c,0x82,0x98,0x41,0x64,0x30,0x62,0x39,0x70,0x58,0x4e,0x43,
    0x00,0x8d,0x92,0x89,0x19,0x80,0x2b,0x33,0x35,0x38,0x35,0x34,
    0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x2f,0x54,0x59,0x50,0x45,
    0x3d,0x50,0x4c,0x4d,0x4e,0x00,0x86,0x81,0x8a,0x80,0x8e,0x03,
    0x02,0x17,0x91,0x88,0x03,0x81,0x01,0x01,0x83,0x68,0x74,0x74,
    0x70,0x3a,0x2f,0x2f,0x6d,0x6d,0x73,0x63,0x34,0x32,0x3a,0x31,
    0x30,0x30,0x32,0x31,0x2f,0x6d,0x6d,0x73,0x63,0x2f,0x34,0x5f,
    0x32,0x3f,0x41,0x64,0x30,0x62,0x39,0x70,0x58,0x4e,0x43,0x00
};

static const TestDesc tests[] = {
    {
        "SyncCancel", plus_259199_sec, sizeof(plus_259199_sec), 0,
        NULL, test_postnotify_cancel
    },{
        "AsyncCancelBefore", plus_259199_sec, sizeof(plus_259199_sec), 0,
        test_prenotify_cancel_async, NULL
    },{
        "AsyncCancelAfter", plus_259199_sec, sizeof(plus_259199_sec), 0,
        NULL, test_postnotify_cancel_async
    },{
        "Reject", plus_1_sec, sizeof(plus_1_sec), 1, NULL, NULL
    }
};

#define TEST_(x) "/RetrieveCancel/" x

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
