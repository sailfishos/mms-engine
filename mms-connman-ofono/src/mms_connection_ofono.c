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

#include "mms_connection_ofono.h"

#include <gofono_connctx.h>
#include <gofono_simmgr.h>

/* Logging */
#define MMS_LOG_MODULE_NAME mms_connection_log
#include "mms_connman_ofono_log.h"
MMS_LOG_MODULE_DEFINE("mms-connection-ofono");

enum context_handler_id {
    CONTEXT_HANDLER_ACTIVATE_FAILED,
    CONTEXT_HANDLER_ACTIVE_CHANGED,
    CONTEXT_HANDLER_INTERFACE_CHANGED,
    CONTEXT_HANDLER_MMS_PROXY,
    CONTEXT_HANDLER_MMS_CENTER,
    CONTEXT_HANDLER_COUNT
};

enum sim_handler_id {
    SIM_HANDLER_IMSI_CHANGED,
    SIM_HANDLER_PRESENT_CHANGED,
    SIM_HANDLER_COUNT
};

typedef struct mms_connection_ofono {
    MMSConnection connection;
    OfonoSimMgr* sim;
    OfonoConnCtx* context;
    gulong context_handler_id[CONTEXT_HANDLER_COUNT];
    gulong sim_handler_id[SIM_HANDLER_COUNT];
    char* imsi;
} MMSConnectionOfono;

typedef MMSConnectionClass MMSConnectionOfonoClass;
G_DEFINE_TYPE(MMSConnectionOfono, mms_connection_ofono, MMS_TYPE_CONNECTION)
#define MMS_TYPE_CONNECTION_OFONO (mms_connection_ofono_get_type())
#define MMS_CONNECTION_OFONO(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
   MMS_TYPE_CONNECTION_OFONO, MMSConnectionOfono))

static
void
mms_connection_ofono_disconnect(
    MMSConnectionOfono* self)
{
    ofono_connctx_remove_handlers(self->context,
        self->context_handler_id, CONTEXT_HANDLER_COUNT);
    ofono_simmgr_remove_handlers(self->sim,
        self->sim_handler_id, SIM_HANDLER_COUNT);
}

static
gboolean
mms_connection_ofono_set_state(
    MMSConnectionOfono* self,
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
            /* Stop listening for property changes */
            mms_connection_ofono_disconnect(self);
        }
        self->connection.state = state;
        mms_connection_signal_state_change(&self->connection);
    }
    return TRUE;
}

static
void
mms_connection_ofono_cancel(
    MMSConnectionOfono* self)
{
    if (self->connection.state <= MMS_CONNECTION_STATE_OPENING) {
        mms_connection_ofono_set_state(self, MMS_CONNECTION_STATE_FAILED);
    } else {
        mms_connection_ofono_set_state(self, MMS_CONNECTION_STATE_CLOSED);
    }
}

static
void
mms_connection_ofono_active_changed(
    OfonoConnCtx* context,
    void* arg)
{
    MMSConnectionOfono* self = MMS_CONNECTION_OFONO(arg);
    MMS_ASSERT(self->context == context);
    if (context->active) {
        MMS_DEBUG("Connection %s opened", self->connection.imsi);
        mms_connection_ofono_set_state(self, MMS_CONNECTION_STATE_OPEN);
    } else {
        mms_connection_ofono_set_state(self, MMS_CONNECTION_STATE_CLOSED);
    }
}

static
void
mms_connection_ofono_interface_changed(
    OfonoConnCtx* context,
    void* arg)
{
    MMSConnectionOfono* self = MMS_CONNECTION_OFONO(arg);
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
mms_connection_ofono_mms_proxy_changed(
    OfonoConnCtx* context,
    void* arg)
{
    MMSConnectionOfono* self = MMS_CONNECTION_OFONO(arg);
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
mms_connection_ofono_mms_center_changed(
    OfonoConnCtx* context,
    void* arg)
{
    MMSConnectionOfono* self = MMS_CONNECTION_OFONO(arg);
    MMS_ASSERT(self->context == context);
    if (context->mms_center) {
        self->connection.mmsc = context->mms_center;
        MMS_DEBUG("MessageCenter: %s", self->connection.mmsc);
    } else {
        self->connection.mmsc = NULL;
    }
}

static
void
mms_connection_ofono_imsi_changed(
    OfonoSimMgr* sim,
    void* arg)
{
    MMSConnectionOfono* self = MMS_CONNECTION_OFONO(arg);
    MMS_ASSERT(self->sim == sim);
    if (g_strcmp0(sim->imsi, self->connection.imsi)) {
        MMS_DEBUG_("%s", sim->imsi);
        mms_connection_ofono_cancel(self);
    }
}

static
void
mms_connection_ofono_present_changed(
    OfonoSimMgr* sim,
    void* arg)
{
    MMSConnectionOfono* self = MMS_CONNECTION_OFONO(arg);
    MMS_ASSERT(self->sim == sim);
    MMS_DEBUG_("%s", sim->present ? "true" : "false");
    if (!sim->present) {
        MMS_DEBUG_("%s", sim->imsi);
        mms_connection_ofono_cancel(self);
    }
}

static
void
mms_connection_ofono_activate_failed(
    OfonoConnCtx* context,
    const GError* error,
    void* arg)
{
    MMSConnectionOfono* self = MMS_CONNECTION_OFONO(arg);
    MMS_ASSERT(self->context == context);
    mms_connection_ofono_cancel(self);
}

MMSConnection*
mms_connection_ofono_new(
    OfonoSimMgr* sim,
    OfonoConnCtx* context,
    MMS_CONNECTION_TYPE type)
{
    MMSConnectionOfono* self = g_object_new(MMS_TYPE_CONNECTION_OFONO, NULL);
    MMSConnection* conn = &self->connection;

    MMS_ASSERT(ofono_simmgr_valid(sim));
    MMS_ASSERT(sim->present);
    MMS_VERBOSE_("%p %s", self, sim->imsi);

    conn->type = type;
    conn->imsi = self->imsi = g_strdup(sim->imsi);
    self->sim = ofono_simmgr_ref(sim);
    self->context = ofono_connctx_ref(context);
    self->connection.state = context->active ?
        MMS_CONNECTION_STATE_OPEN : MMS_CONNECTION_STATE_OPENING;

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

    /* Listen for property changes */
    self->context_handler_id[CONTEXT_HANDLER_ACTIVATE_FAILED] =
        ofono_connctx_add_activate_failed_handler(context,
        mms_connection_ofono_activate_failed, self);
    self->context_handler_id[CONTEXT_HANDLER_ACTIVE_CHANGED] =
        ofono_connctx_add_active_changed_handler(context,
        mms_connection_ofono_active_changed, self);
    self->context_handler_id[CONTEXT_HANDLER_INTERFACE_CHANGED] =
        ofono_connctx_add_interface_changed_handler(context,
        mms_connection_ofono_interface_changed, self);
    self->context_handler_id[CONTEXT_HANDLER_MMS_PROXY] =
        ofono_connctx_add_mms_proxy_changed_handler(context,
        mms_connection_ofono_mms_proxy_changed, self);
    self->context_handler_id[CONTEXT_HANDLER_MMS_CENTER] =
        ofono_connctx_add_mms_center_changed_handler(context,
        mms_connection_ofono_mms_center_changed, self);

    self->sim_handler_id[SIM_HANDLER_IMSI_CHANGED] =
        ofono_simmgr_add_imsi_changed_handler(sim,
        mms_connection_ofono_imsi_changed, self);
    self->sim_handler_id[SIM_HANDLER_PRESENT_CHANGED] =
        ofono_simmgr_add_present_changed_handler(sim,
        mms_connection_ofono_present_changed, self);

    return &self->connection;
}

static
void
mms_connection_ofono_close(
    MMSConnection* connection)
{
    MMSConnectionOfono* self = MMS_CONNECTION_OFONO(connection);
    ofono_connctx_deactivate(self->context);
}

/**
 * First stage of deinitialization (release all references).
 * May be called more than once in the lifetime of the object.
 */
static
void
mms_connection_ofono_dispose(
    GObject* object)
{
    MMSConnectionOfono* self = MMS_CONNECTION_OFONO(object);
    MMS_VERBOSE_("%p %s", self, self->imsi);
    MMS_ASSERT(!mms_connection_is_active(&self->connection));
    mms_connection_ofono_disconnect(self);
    if (self->sim) {
        ofono_simmgr_unref(self->sim);
        self->sim = NULL;
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
    G_OBJECT_CLASS(mms_connection_ofono_parent_class)->dispose(object);
}

/**
 * Final stage of deinitialization
 */
static
void
mms_connection_ofono_finalize(
    GObject* object)
{
    MMSConnectionOfono* self = MMS_CONNECTION_OFONO(object);
    MMS_VERBOSE_("%p %s", self, self->imsi);
    g_free(self->imsi);
    G_OBJECT_CLASS(mms_connection_ofono_parent_class)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
mms_connection_ofono_class_init(
    MMSConnectionOfonoClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    klass->fn_close = mms_connection_ofono_close;
    object_class->dispose = mms_connection_ofono_dispose;
    object_class->finalize = mms_connection_ofono_finalize;
}

/**
 * Per instance initializer
 */
static
void
mms_connection_ofono_init(
    MMSConnectionOfono* self)
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
