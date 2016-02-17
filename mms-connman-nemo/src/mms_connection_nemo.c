/*
 * Copyright (C) 2016 Jolla Ltd.
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

#include "mms_connection_nemo.h"
#include "mms_connman.h"

#include <gofono_connmgr.h>
#include <gofono_connctx.h>
#include <gofono_error.h>

/* Logging */
#define MMS_LOG_MODULE_NAME mms_connection_log
#include "mms_connman_nemo_log.h"
MMS_LOG_MODULE_DEFINE("mms-connection-nemo");

#define RETRY_DELAY_SEC (1)
#define MAX_RETRY_COUNT (100)

enum mm_handler_id {
    MM_HANDLER_VALID,
    MM_HANDLER_MMS_IMSI,
    MM_HANDLER_COUNT
};

enum connmgr_handler_id {
    CONNMGR_HANDLER_VALID,
    CONNMGR_HANDLER_ATTACHED,
    CONNMGR_HANDLER_COUNT
};

enum context_handler_id {
    CONTEXT_HANDLER_VALID,
    CONTEXT_HANDLER_ACTIVE,
    CONTEXT_HANDLER_INTERFACE,
    CONTEXT_HANDLER_MMS_PROXY,
    CONTEXT_HANDLER_MMS_CENTER,
    CONTEXT_HANDLER_ACTIVATE_FAILED,
    CONTEXT_HANDLER_COUNT
};

typedef struct mms_connection_nemo {
    MMSConnection connection;
    MMSConnMan* cm;
    OfonoExtModemManager* mm;
    OfonoConnCtx* context;
    OfonoConnMgr* connmgr;
    gulong mm_handler_id[MM_HANDLER_COUNT];
    gulong context_handler_id[CONTEXT_HANDLER_COUNT];
    gulong connmgr_handler_id[CONNMGR_HANDLER_COUNT];
    guint retry_id;
    int retry_count;
    char* imsi;
    char* path;
} MMSConnectionNemo;

typedef MMSConnectionClass MMSConnectionNemoClass;
G_DEFINE_TYPE(MMSConnectionNemo, mms_connection_nemo, MMS_TYPE_CONNECTION)
#define MMS_TYPE_CONNECTION_NEMO (mms_connection_nemo_get_type())
#define MMS_CONNECTION_NEMO(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
   MMS_TYPE_CONNECTION_NEMO, MMSConnectionNemo))

static
void
mms_connection_nemo_disconnect(
    MMSConnectionNemo* self)
{
    ofonoext_mm_remove_handlers(self->mm, self->mm_handler_id,
        G_N_ELEMENTS(self->mm_handler_id));
    ofono_connmgr_remove_handlers(self->connmgr, self->connmgr_handler_id,
        G_N_ELEMENTS(self->connmgr_handler_id));
    ofono_connctx_remove_handlers(self->context, self->context_handler_id,
        G_N_ELEMENTS(self->context_handler_id));
}

static
void
mms_connection_nemo_disconnect_connmgr(
    MMSConnectionNemo* self,
    enum connmgr_handler_id id)
{
    if (self->connmgr_handler_id[id]) {
        ofono_connmgr_remove_handler(self->connmgr,
            self->connmgr_handler_id[id]);
        self->connmgr_handler_id[id] = 0;
    }
}

static
void
mms_connection_nemo_disconnect_context(
    MMSConnectionNemo* self,
    enum context_handler_id id)
{
    if (self->context_handler_id[id]) {
        ofono_connctx_remove_handler(self->context,
            self->context_handler_id[id]);
        self->context_handler_id[id] = 0;
    }
}

static
void
mms_connection_nemo_reset_mms_imsi_done(
    OfonoExtModemManager* mm,
    const char* path,
    const GError* error,
    void* arg)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(arg);
    MMS_DEBUG("Released %s", self->path);
    mms_connman_busy_dec(self->cm);
    mms_connection_unref(&self->connection);
}

static
gboolean
mms_connection_nemo_set_state(
    MMSConnectionNemo* self,
    MMS_CONNECTION_STATE state)
{
    if (self->connection.state != state) {
        if (self->connection.state == MMS_CONNECTION_STATE_FAILED ||
            self->connection.state == MMS_CONNECTION_STATE_CLOSED) {
            /* These are terminal states, can't change those */
            return FALSE;
        } else if (self->connection.state > state) {
            /* Can't move back to a previous state */
            return FALSE;
        }
        if (state == MMS_CONNECTION_STATE_FAILED ||
            state == MMS_CONNECTION_STATE_CLOSED) {

            if (self->path) {
                /* Tell ofono that this SIM is no longer reserved for MMS.
                 * Bump the reference while this D-Bus call is pending */
                mms_connection_ref(&self->connection);
                mms_connman_busy_inc(self->cm);
                ofonoext_mm_set_mms_imsi_full(self->mm, "",
                    mms_connection_nemo_reset_mms_imsi_done, self);
            }

            /* Stop listening for property changes */
            mms_connection_nemo_disconnect(self);
        }
        self->connection.state = state;
        mms_connection_signal_state_change(&self->connection);
    }
    return TRUE;
}

static
void
mms_connection_nemo_cancel(
    MMSConnectionNemo* self)
{
    if (self->retry_id) {
        g_source_remove(self->retry_id);
        self->retry_id = 0;
    }
    if ((self->connection.state <= MMS_CONNECTION_STATE_OPENING &&
        mms_connection_nemo_set_state(self, MMS_CONNECTION_STATE_FAILED)) ||
        mms_connection_nemo_set_state(self, MMS_CONNECTION_STATE_CLOSED)) {
        MMS_DEBUG("Cancelled %s", self->connection.imsi);
    }
}

static
void
mms_connection_nemo_cancel_if_invalid(
    OfonoObject* obj,
    void* arg)
{
    if (!obj->valid) {
        MMS_VERBOSE_("oops!");
        mms_connection_nemo_cancel(MMS_CONNECTION_NEMO(arg));
    }
}

static
void
mms_connection_nemo_active_changed(
    OfonoConnCtx* context,
    void* arg)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(arg);
    MMS_ASSERT(self->context == context);
    if (context->active) {
        MMS_DEBUG("Connection %s opened", self->connection.imsi);
        mms_connection_nemo_set_state(self, MMS_CONNECTION_STATE_OPEN);
    } else {
        mms_connection_nemo_disconnect_context(self, CONTEXT_HANDLER_ACTIVE);
        mms_connection_nemo_set_state(self, MMS_CONNECTION_STATE_CLOSED);
    }
}

static
void
mms_connection_nemo_interface_changed(
    OfonoConnCtx* context,
    void* arg)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(arg);
    MMS_ASSERT(self->context == context);
    if (context->ifname) {
        self->connection.netif = context->ifname;
        MMS_DEBUG("Interface: %s", self->connection.netif);
    } else {
        self->connection.netif = NULL;
    }
}

static
void
mms_connection_nemo_mms_proxy_changed(
    OfonoConnCtx* context,
    void* arg)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(arg);
    MMS_ASSERT(self->context == context);
    if (context->mms_proxy) {
        self->connection.mmsproxy = context->mms_proxy;
        MMS_DEBUG("MessageProxy: %s", self->connection.mmsproxy);
    } else {
        self->connection.mmsproxy = NULL;
    }
}

static
void
mms_connection_nemo_mms_center_changed(
    OfonoConnCtx* context,
    void* arg)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(arg);
    MMS_ASSERT(self->context == context);
    if (context->mms_center) {
        self->connection.mmsc = context->mms_center;
        MMS_DEBUG("MessageCenter: %s", self->connection.mmsc);
    } else {
        self->connection.mmsc = NULL;
    }
}

static
gboolean
mms_connection_nemo_activate_retry(
    gpointer arg)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(arg);
    OfonoConnCtx* context = self->context;
    MMS_ASSERT(self->retry_id);
    self->retry_id = 0;
    MMS_DEBUG("Activating %s again", ofono_connctx_path(context));
    ofono_connctx_activate(context);
    return G_SOURCE_REMOVE;
}

static
void
mms_connection_nemo_activate_failed(
    OfonoConnCtx* context,
    const GError* error,
    void* arg)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(arg);
    MMS_ASSERT(self->context == context);
    MMS_ASSERT(!self->retry_id);
    if (error->domain == OFONO_ERROR &&
        error->code == OFONO_ERROR_BUSY &&
        self->retry_count < MAX_RETRY_COUNT) {
        self->retry_count++;
        MMS_DEBUG("Retry %d in %d sec", self->retry_count, RETRY_DELAY_SEC);
        self->retry_id = g_timeout_add_seconds(RETRY_DELAY_SEC,
            mms_connection_nemo_activate_retry, self);
    } else {
        mms_connection_nemo_cancel(self);
    }
}

static
void
mms_connection_nemo_check_context(
    MMSConnectionNemo* self)
{
    if (self->connection.state == MMS_CONNECTION_STATE_OPENING) {
        OfonoConnCtx* context = self->context;
        if (ofono_connctx_valid(context) && context->active) {
            /* Already connected */
            mms_connection_nemo_set_state(self, MMS_CONNECTION_STATE_OPEN);
        } else {
            OfonoConnMgr* connmgr = self->connmgr;
            if (ofono_connmgr_valid(connmgr) && connmgr->attached) {
                MMS_DEBUG("Activate %s", ofono_connctx_path(context));
                ofono_connctx_activate(context);
            }
        }
    }
}

static
void
mms_connection_nemo_connmgr_attached_changed(
    OfonoConnMgr* connmgr,
    void* arg)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(arg);
    MMS_DEBUG("Data registration %s for %s", connmgr->attached ? "on" : "off",
        ofono_connmgr_path(connmgr));
    mms_connection_nemo_check_context(self);
}

static
void
mms_connection_nemo_setup_context(
    MMSConnectionNemo* self)
{
    MMSConnection* conn = &self->connection;
    OfonoConnCtx* context = self->context;

    /* From this point on, cancel the connection if OfonoConnMgr becomes
     * invalid (which would probably mean SIM removal or ofono crash) */
    mms_connection_nemo_disconnect_context(self, CONTEXT_HANDLER_VALID);
    self->context_handler_id[CONTEXT_HANDLER_VALID] =
        ofono_object_add_valid_changed_handler(
            ofono_connctx_object(self->context),
            mms_connection_nemo_cancel_if_invalid, self);

    /* Capture the current context state */
    if (context->mms_proxy && context->mms_proxy[0]) {
        conn->mmsproxy = context->mms_proxy;
        MMS_DEBUG("MessageProxy: %s", conn->mmsproxy);
    }
    if (context->mms_center && context->mms_center[0]) {
        conn->mmsc = context->mms_center;
        MMS_DEBUG("MessageCenter: %s", conn->mmsc);
    }
    if (context->ifname && context->ifname[0]) {
        conn->netif = context->ifname;
        MMS_DEBUG("Interface: %s", conn->netif);
    }

    /* Track context property changes */
    self->context_handler_id[CONTEXT_HANDLER_ACTIVE] =
        ofono_connctx_add_active_changed_handler(context,
        mms_connection_nemo_active_changed, self);
    self->context_handler_id[CONTEXT_HANDLER_INTERFACE] =
        ofono_connctx_add_interface_changed_handler(context,
        mms_connection_nemo_interface_changed, self);
    self->context_handler_id[CONTEXT_HANDLER_MMS_PROXY] =
        ofono_connctx_add_mms_proxy_changed_handler(context,
        mms_connection_nemo_mms_proxy_changed, self);
    self->context_handler_id[CONTEXT_HANDLER_MMS_CENTER] =
        ofono_connctx_add_mms_center_changed_handler(context,
        mms_connection_nemo_mms_center_changed, self);

    /* Will most likely need this one too */
    self->context_handler_id[CONTEXT_HANDLER_ACTIVATE_FAILED] =
        ofono_connctx_add_activate_failed_handler(context,
        mms_connection_nemo_activate_failed, self);

    /* And start tracking the data registration state */
    self->connmgr_handler_id[CONNMGR_HANDLER_ATTACHED] =
        ofono_connmgr_add_attached_changed_handler(self->connmgr,
        mms_connection_nemo_connmgr_attached_changed, self);

    /* Check if we can actually connect */
    mms_connection_nemo_check_context(self);
}

static
void
mms_connection_nemo_context_valid_changed(
    OfonoConnCtx* context,
    void* arg)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(arg);
    GASSERT(self->connection.state == MMS_CONNECTION_STATE_OPENING);
    if (ofono_connctx_valid(context)) {
        mms_connection_nemo_setup_context(self);
    }
}

static
void
mms_connection_nemo_init_context(
    MMSConnectionNemo* self)
{
    MMS_ASSERT(ofono_connmgr_valid(self->connmgr));
    MMS_ASSERT(!self->context);

    /* From this point on, cancel the connection if OfonoConnMgr becomes
     * invalid (which would probably mean SIM removal or ofono crash) */
    mms_connection_nemo_disconnect_connmgr(self, CONNMGR_HANDLER_VALID);
    self->connmgr_handler_id[CONNMGR_HANDLER_VALID] =
        ofono_object_add_valid_changed_handler(
            ofono_connmgr_object(self->connmgr),
            mms_connection_nemo_cancel_if_invalid, self);

    /* ofono_connctx_ref has no problem with a NULL pointer */
    self->context = ofono_connctx_ref(ofono_connmgr_get_context_for_type(
        self->connmgr, OFONO_CONNCTX_TYPE_MMS));
    if (self->context) {
        MMS_DEBUG("MMS context %s", ofono_connctx_path(self->context));
        if (ofono_connctx_valid(self->context)) {
            mms_connection_nemo_setup_context(self);
        } else {
            self->context_handler_id[CONTEXT_HANDLER_VALID] =
                ofono_connctx_add_valid_changed_handler(self->context,
                mms_connection_nemo_context_valid_changed, self);
        }
    } else {
        MMS_WARN("No MMS context");
        mms_connection_nemo_cancel(self);
    }
}

static
void
mms_connection_nemo_connmgr_valid_changed(
    OfonoConnMgr* connmgr,
    void* arg)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(arg);
    GASSERT(self->connection.state == MMS_CONNECTION_STATE_OPENING);
    if (ofono_connmgr_valid(connmgr)) {
        mms_connection_nemo_init_context(self);
    }
}

static
void
mms_connection_nemo_mms_imsi_changed(
    OfonoExtModemManager* mm,
    void* arg)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(arg);
    if (mm->valid && g_strcmp0(mm->mms_imsi, self->connection.imsi)) {
        MMS_VERBOSE_("%s", mm->mms_imsi);
        mms_connection_nemo_cancel(self);
    }
}

static
void
mms_connection_nemo_set_mms_imsi_done(
    OfonoExtModemManager* mm,
    const char* path,
    const GError* error,
    void* arg)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(arg);
    if (error) {
        mms_connection_nemo_cancel(self);
    } else {
        MMS_DEBUG("MMS modem path %s", path);
        MMS_ASSERT(!self->path);
        MMS_ASSERT(!self->connmgr);
        self->path = g_strdup(path);

        if (self->connection.state == MMS_CONNECTION_STATE_OPENING) {
            self->connmgr = ofono_connmgr_new(path);

            /* Cancel connection if MMS SIM changes */
            self->mm_handler_id[MM_HANDLER_MMS_IMSI] =
                ofonoext_mm_add_mms_imsi_changed_handler(mm,
                mms_connection_nemo_mms_imsi_changed, self);

            if (ofono_connmgr_valid(self->connmgr)) {
                mms_connection_nemo_init_context(self);
            } else {
                /* Wait for OfonoConnMgr to become valid */
                self->connmgr_handler_id[CONNMGR_HANDLER_VALID] =
                    ofono_connmgr_add_valid_changed_handler(self->connmgr,
                    mms_connection_nemo_connmgr_valid_changed, self);
            }
        } else {
            /* Connection has been cancelled, release the slot */
            MMS_DEBUG("Canceled, releasing %s", path);
            mms_connection_ref(&self->connection);
            mms_connman_busy_inc(self->cm);
            ofonoext_mm_set_mms_imsi_full(self->mm, "",
                mms_connection_nemo_reset_mms_imsi_done, self);
        }
    }
    /* Release the reference */
    mms_connman_busy_dec(self->cm);
    mms_connection_unref(&self->connection);
}

static
void
mms_connection_nemo_cancel_if_mm_invalid(
    OfonoExtModemManager* mm,
    void* arg)
{
    if (!mm->valid) {
        MMS_VERBOSE_("oops!");
        mms_connection_nemo_cancel(MMS_CONNECTION_NEMO(arg));
    }
}

static
void
mms_connection_nemo_request_sim(
    MMSConnectionNemo* self)
{
    /* Cancel the connection if OfonoExtModemManager becomes invalid */
    GASSERT(self->mm->valid);
    ofonoext_mm_remove_handler(self->mm,
        self->mm_handler_id[MM_HANDLER_VALID]);
    self->mm_handler_id[MM_HANDLER_VALID] =
        ofonoext_mm_add_valid_changed_handler(self->mm,
        mms_connection_nemo_cancel_if_mm_invalid, self);

    /* Bump the reference while this D-Bus call is pending */
    mms_connection_ref(&self->connection);
    mms_connman_busy_inc(self->cm);
    ofonoext_mm_set_mms_imsi_full(self->mm, self->imsi,
        mms_connection_nemo_set_mms_imsi_done, self);
}

static
void
mms_connection_nemo_mm_valid_changed(
    OfonoExtModemManager* mm,
    void* arg)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(arg);
    MMS_VERBOSE_("%p %d", self, mm->valid);
    if (mm->valid) {
        mms_connection_nemo_request_sim(arg);
    }
}

MMSConnection*
mms_connection_nemo_new(
    MMSConnMan* cm,
    const char* imsi,
    MMS_CONNECTION_TYPE type)
{
    MMSConnectionNemo* self = g_object_new(MMS_TYPE_CONNECTION_NEMO, NULL);
    MMSConnection* conn = &self->connection;

    MMS_VERBOSE_("%p %s", self, imsi);
    conn->type = type;
    conn->imsi = self->imsi = g_strdup(imsi);
    conn->state = MMS_CONNECTION_STATE_OPENING;
    self->mm = ofonoext_mm_new();
    self->cm = mms_connman_ref(cm);

    if (self->mm->valid) {
        mms_connection_nemo_request_sim(self);
    } else {
        self->mm_handler_id[MM_HANDLER_VALID] =
            ofonoext_mm_add_valid_changed_handler(self->mm,
            mms_connection_nemo_mm_valid_changed, self);
    }

    return &self->connection;
}

static
void
mms_connection_nemo_close(
    MMSConnection* connection)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(connection);
    OfonoConnCtx* context = self->context;
    if (ofono_connctx_valid(context) && context->active) {
        MMS_DEBUG("Deactivate %s", ofono_connctx_path(context));
        ofono_connctx_deactivate(context);
    } else {
        mms_connection_nemo_cancel(self);
    }
}

/**
 * First stage of deinitialization (release all references).
 * May be called more than once in the lifetime of the object.
 */
static
void
mms_connection_nemo_dispose(
    GObject* object)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(object);
    MMS_VERBOSE_("%p %s", self, self->imsi);
    MMS_ASSERT(!mms_connection_is_active(&self->connection));
    mms_connection_nemo_disconnect(self);
    if (self->retry_id) {
        g_source_remove(self->retry_id);
        self->retry_id = 0;
    }
    if (self->context) {
        if (mms_connection_is_active(&self->connection) &&
            self->context->active) {
            ofono_connctx_deactivate(self->context);
        }
        ofono_connctx_unref(self->context);
        self->context = NULL;
    }
    self->connection.netif = NULL;
    G_OBJECT_CLASS(mms_connection_nemo_parent_class)->dispose(object);
}

/**
 * Final stage of deinitialization
 */
static
void
mms_connection_nemo_finalize(
    GObject* object)
{
    MMSConnectionNemo* self = MMS_CONNECTION_NEMO(object);
    MMS_VERBOSE_("%p %s", self, self->imsi);
    ofono_connmgr_unref(self->connmgr);
    ofono_connctx_unref(self->context);
    ofonoext_mm_unref(self->mm);
    mms_connman_unref(self->cm);
    g_free(self->imsi);
    g_free(self->path);
    G_OBJECT_CLASS(mms_connection_nemo_parent_class)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
mms_connection_nemo_class_init(
    MMSConnectionNemoClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    klass->fn_close = mms_connection_nemo_close;
    object_class->dispose = mms_connection_nemo_dispose;
    object_class->finalize = mms_connection_nemo_finalize;
}

/**
 * Per instance initializer
 */
static
void
mms_connection_nemo_init(
    MMSConnectionNemo* self)
{
    MMS_VERBOSE_("%p", self);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
