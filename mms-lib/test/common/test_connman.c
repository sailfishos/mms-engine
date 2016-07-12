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

#include "test_connman.h"
#include "test_connection.h"

/* Logging */
#define GLOG_MODULE_NAME mms_connman_log
#include <gutil_log.h>
GLOG_MODULE_DEFINE("mms-connman-test");

typedef MMSConnManClass MMSConnManTestClass;
typedef struct mms_connman_test {
    MMSConnMan cm;
    MMSConnection* conn;
    unsigned short port;
    gboolean proxy;
    char* default_imsi;
    gboolean offline;
    mms_connman_test_connect_fn connect_fn;
    void* connect_param;
} MMSConnManTest;

G_DEFINE_TYPE(MMSConnManTest, mms_connman_test, MMS_TYPE_CONNMAN)
#define MMS_TYPE_CONNMAN_TEST (mms_connman_test_get_type())
#define MMS_CONNMAN_TEST(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
    MMS_TYPE_CONNMAN_TEST, MMSConnManTest))

static
gboolean
mms_connman_test_busy_cb(
    gpointer data)
{
    MMSConnManTest* test = MMS_CONNMAN_TEST(data);
    mms_connman_busy_dec(&test->cm);
    mms_connman_ref(&test->cm);
    return G_SOURCE_REMOVE;
}

static
void
mms_connman_test_make_busy(
    MMSConnManTest* test)
{
    mms_connman_ref(&test->cm);
    mms_connman_busy_inc(&test->cm);
    g_idle_add(mms_connman_test_busy_cb, test);
}

void
mms_connman_test_set_port(
    MMSConnMan* cm,
    unsigned short port,
    gboolean proxy)
{
    MMSConnManTest* test = MMS_CONNMAN_TEST(cm);
    test->port = port;
    test->proxy = proxy;
}

void
mms_connman_test_set_offline(
    MMSConnMan* cm,
    gboolean offline)
{
    MMS_CONNMAN_TEST(cm)->offline = offline;
}

void
mms_connman_test_set_default_imsi(
    MMSConnMan* cm,
    const char* imsi)
{
    MMSConnManTest* test = MMS_CONNMAN_TEST(cm);
    g_free(test->default_imsi);
    test->default_imsi = g_strdup(imsi);
}

void
mms_connman_test_close_connection(
    MMSConnMan* cm)
{
    MMSConnManTest* test = MMS_CONNMAN_TEST(cm);
    if (test->conn) {
        GDEBUG("Closing connection...");
        mms_connman_test_make_busy(test);
        mms_connection_close(test->conn);
        mms_connection_unref(test->conn);
        test->conn = NULL;
    }
}

void
mms_connman_test_set_connect_callback(
    MMSConnMan* cm,
    mms_connman_test_connect_fn fn,
    void* param)
{
    MMSConnManTest* test = MMS_CONNMAN_TEST(cm);
    test->connect_fn = fn;
    test->connect_param = param;
}

static
char*
mms_connman_test_default_imsi(
    MMSConnMan* cm)
{
    return g_strdup(MMS_CONNMAN_TEST(cm)->default_imsi);
}

static
MMSConnection*
mms_connman_test_open_connection(
    MMSConnMan* cm,
    const char* imsi,
    MMS_CONNECTION_TYPE type)
{
    MMSConnManTest* test = MMS_CONNMAN_TEST(cm);
    mms_connman_test_close_connection(cm);
    if (test->offline) {
        return NULL;
    } else {
        mms_connman_test_make_busy(test);
        test->conn = mms_connection_test_new(imsi, test->port, test->proxy);
        if (test->connect_fn) test->connect_fn(test->connect_param);
        return mms_connection_ref(test->conn);
    }
}

static
void
mms_connman_test_dispose(
    GObject* object)
{
    mms_connman_test_close_connection(&MMS_CONNMAN_TEST(object)->cm);
    G_OBJECT_CLASS(mms_connman_test_parent_class)->dispose(object);
}

static
void
mms_connman_test_finalize(
    GObject* object)
{
    g_free(MMS_CONNMAN_TEST(object)->default_imsi);
    G_OBJECT_CLASS(mms_connman_test_parent_class)->finalize(object);
}

static
void
mms_connman_test_class_init(
    MMSConnManTestClass* klass)
{
    klass->fn_default_imsi = mms_connman_test_default_imsi;
    klass->fn_open_connection = mms_connman_test_open_connection;
    G_OBJECT_CLASS(klass)->dispose = mms_connman_test_dispose;
    G_OBJECT_CLASS(klass)->finalize = mms_connman_test_finalize;
}

static
void
mms_connman_test_init(
    MMSConnManTest* test)
{
    test->default_imsi = g_strdup("Default");
}

MMSConnMan*
mms_connman_test_new()
{
    return g_object_new(MMS_TYPE_CONNMAN_TEST,NULL);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
