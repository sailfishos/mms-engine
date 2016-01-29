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

#include "mms_connman_ofono.h"
#include "mms_connection_ofono.h"

#include <gofono_manager.h>
#include <gofono_connmgr.h>
#include <gofono_connctx.h>
#include <gofono_simmgr.h>
#include <gofono_modem.h>

/* Logging */
#define MMS_LOG_MODULE_NAME mms_connman_log
#include "mms_connman_ofono_log.h"
MMS_LOG_MODULE_DEFINE("mms-connman-ofono");

enum manager_handler_id {
    MANAGER_HANDLER_VALID_CHANGED,
    MANAGER_HANDLER_MODEM_ADDED,
    MANAGER_HANDLER_MODEM_REMOVED,
    MANAGER_HANDLER_COUNT
};

enum simmgr_handler_id {
    SIMMGR_HANDLER_VALID_CHANGED,
    SIMMGR_HANDLER_PRESENT_CHANGED,
    SIMMGR_HANDLER_COUNT
};

typedef struct mms_ofono_modem {
    OfonoModem* modem;
    OfonoSimMgr* simmgr;
    OfonoConnMgr* connmgr;
    MMSConnection* conn;
    gulong simmgr_handler_id[SIMMGR_HANDLER_COUNT];
} MMSOfonoModem;

typedef MMSConnManClass MMSConnManOfonoClass;
typedef struct mms_connman_ofono {
    MMSConnMan cm;
    GHashTable* modems;
    MMSOfonoModem* default_modem;
    OfonoManager* manager;
    gulong manager_handler_id[MANAGER_HANDLER_COUNT];
} MMSConnManOfono;

G_DEFINE_TYPE(MMSConnManOfono, mms_connman_ofono, MMS_TYPE_CONNMAN)
#define MMS_TYPE_CONNMAN_OFONO (mms_connman_ofono_get_type())
#define MMS_CONNMAN_OFONO(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
    MMS_TYPE_CONNMAN_OFONO, MMSConnManOfono))

#define MMS_OFONO_TIMEOUT (30000)

/**
 * Waits until ofono client interfaces are initialized.
 */
static
void
mms_connman_ofono_wait_valid(
    MMSConnManOfono* self)
{
    GError* error = NULL;
    if (ofono_manager_wait_valid(self->manager, MMS_OFONO_TIMEOUT, &error)) {
        if (self->default_modem &&
            self->default_modem->modem &&
            !ofono_modem_wait_valid(self->default_modem->modem,
            MMS_OFONO_TIMEOUT, &error)) {
            MMS_ERR("%s", MMS_ERRMSG(error));
            g_error_free(error);
        }
        if (self->default_modem &&
            self->default_modem->simmgr &&
            !ofono_simmgr_wait_valid(self->default_modem->simmgr,
            MMS_OFONO_TIMEOUT, &error)) {
            MMS_ERR("%s", MMS_ERRMSG(error));
            g_error_free(error);
        }
        if (self->default_modem &&
            self->default_modem->connmgr &&
            !ofono_connmgr_wait_valid(self->default_modem->connmgr,
            MMS_OFONO_TIMEOUT, &error)) {
            MMS_ERR("%s", MMS_ERRMSG(error));
            g_error_free(error);
        }
    } else {
        MMS_ERR("%s", MMS_ERRMSG(error));
        g_error_free(error);
    }
}

/**
 * Finds OfonoModem for the specified IMSI. If it returns non-NULL, the modem
 * is guaranteed to have SimManager associated with it.
 */
static
MMSOfonoModem*
mms_connman_ofono_modem_for_imsi(
    MMSConnManOfono* self,
    const char* imsi)
{
    mms_connman_ofono_wait_valid(self);
    if (imsi) {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, self->modems);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            MMSOfonoModem* modem = value;
            if (modem->simmgr &&
                ofono_simmgr_valid(modem->simmgr) &&
                modem->simmgr->present &&
                !g_strcmp0(imsi, modem->simmgr->imsi)) {
                return modem;
            }
        }
        MMS_INFO("SIM %s is not avialable", imsi);
    } else if (self->default_modem) {
        if (ofono_simmgr_valid(self->default_modem->simmgr)) {
            if (self->default_modem->simmgr->present) {
                return self->default_modem;
            } else {
                MMS_INFO("SIM card is not present");
            }
        } else {
            MMS_INFO("No SIM card found");
        }
    } else {
        MMS_WARN("No default modem");
    }
    return NULL;
}

/**
 * Returns IMSI of the default SIM
 */
static
char*
mms_connman_ofono_default_imsi(
    MMSConnMan* cm)
{
    MMSConnManOfono* self = MMS_CONNMAN_OFONO(cm);
    mms_connman_ofono_wait_valid(self);
    if (self->default_modem &&
        self->default_modem->simmgr &&
        self->default_modem->simmgr->imsi &&
        self->default_modem->simmgr->imsi[0]) {
        return g_strdup(self->default_modem->simmgr->imsi);
    }
    return NULL;
}

/**
 * MMSConnection destroy notification.
 */
static
void
mms_connman_ofono_connection_gone(
    gpointer arg,
    GObject* connection)
{
    MMSConnManOfono* self = MMS_CONNMAN_OFONO(arg);
    GHashTableIter it;
    gpointer value;
    g_hash_table_iter_init(&it, self->modems);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        MMSOfonoModem* modem = value;
        if (((GObject*)modem->conn) == connection) {
            modem->conn = NULL;
            MMS_VERBOSE_("%s", ofono_modem_path(modem->modem));
            break;
        }
    }
}

/**
 * Creates a new connection or returns the reference to an aready active one.
 * The caller must release the reference. Returns NULL if the modem is offline
 * and the network task should fail immediately.
 */
static
MMSConnection*
mms_connman_ofono_open_connection(
    MMSConnMan* cm,
    const char* imsi,
    gboolean user_request)
{
    MMSConnManOfono* self = MMS_CONNMAN_OFONO(cm);
    MMSOfonoModem* modem = mms_connman_ofono_modem_for_imsi(self, imsi);
    if (modem) {
        OfonoConnCtx* ctx = ofono_connmgr_get_context_for_type(modem->connmgr,
            OFONO_CONNCTX_TYPE_MMS);
        if (ctx) {
            if (modem->conn) {
                mms_connection_ref(modem->conn);
            } else {
                modem->conn = mms_connection_ofono_new(modem->simmgr, ctx,
                    user_request);
                g_object_weak_ref(G_OBJECT(modem->conn),
                    mms_connman_ofono_connection_gone, self);
            }
            if (!ctx->active) {
                ofono_connctx_activate(ctx);
            }
            return modem->conn;
        } else {
            MMS_WARN("SIM %s has no MMS context", imsi);
        }
    }
    return NULL;
}

static
void
mms_connman_ofono_select_default_modem(
    MMSConnManOfono* self)
{
    MMSOfonoModem* context = NULL;
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, self->modems);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (!context ||
            !ofono_simmgr_valid(context->simmgr) ||
            !context->simmgr->present) {
            context = value;
            if (ofono_simmgr_valid(context->simmgr) &&
                context->simmgr->present) {
                break;
            }
        }
    }
    if (self->default_modem != context) {
        if (context) {
            MMS_INFO("Default modem %s", ofono_modem_path(context->modem));
        } else {
            MMS_INFO("No default modem");
        }
        self->default_modem = context;
    }
}

static
void
mms_ofono_simmgr_changed(
    OfonoSimMgr* sender,
    void* arg)
{
    mms_connman_ofono_select_default_modem(MMS_CONNMAN_OFONO(arg));
}

static
void
mms_connman_ofono_add_modem(
    MMSConnManOfono* self,
    OfonoModem* modem)
{
    MMSOfonoModem* context = g_new0(MMSOfonoModem,1);
    const char* path = modem->object.path;
    context->modem = ofono_modem_ref(modem);
    context->connmgr = ofono_connmgr_new(path);
    context->simmgr = ofono_simmgr_new(path);
    context->simmgr_handler_id[SIMMGR_HANDLER_VALID_CHANGED] =
        ofono_simmgr_add_valid_changed_handler(context->simmgr,
            mms_ofono_simmgr_changed, self);
    context->simmgr_handler_id[SIMMGR_HANDLER_PRESENT_CHANGED] =
        ofono_simmgr_add_present_changed_handler(context->simmgr,
            mms_ofono_simmgr_changed, self);
    g_hash_table_replace(self->modems, g_strdup(path), context);
}

static
void
mms_connman_ofono_modem_added(
    OfonoManager* manager,
    OfonoModem* modem,
    void* arg)
{
    if (manager->valid) {
        MMSConnManOfono* self = MMS_CONNMAN_OFONO(arg);
        MMS_ASSERT(manager == self->manager);
        mms_connman_ofono_add_modem(self, modem);
        mms_connman_ofono_select_default_modem(self);
    }
}

static
void
mms_connman_ofono_modem_removed(
    OfonoManager* manager,
    const char* path,
    void* arg)
{
    if (manager->valid) {
        MMSConnManOfono* self = MMS_CONNMAN_OFONO(arg);
        MMS_ASSERT(manager == self->manager);
        g_hash_table_remove(self->modems, path);
        mms_connman_ofono_select_default_modem(self);
    }
}

static
void
mms_connman_ofono_init_modems(
    MMSConnManOfono* self)
{
    guint i;
    GPtrArray* modems = ofono_manager_get_modems(self->manager);
    for (i=0; i<modems->len; i++) {
        mms_connman_ofono_add_modem(self, OFONO_MODEM(modems->pdata[i]));
    }
    mms_connman_ofono_select_default_modem(self);
    g_ptr_array_unref(modems);
}

static
void
mms_connman_ofono_valid_changed(
    OfonoManager* manager,
    void* arg)
{
    MMSConnManOfono* self = MMS_CONNMAN_OFONO(arg);
    MMS_ASSERT(manager == self->manager);
    if (manager->valid) {
        mms_connman_ofono_init_modems(self);
    } else {
        self->default_modem = NULL;
        g_hash_table_remove_all(self->modems);
    }
}

/**
 * Creates oFono connection manager
 */
MMSConnMan*
mms_connman_ofono_new()
{
    MMSConnManOfono* self = g_object_new(MMS_TYPE_CONNMAN_OFONO, NULL);
    self->manager = ofono_manager_new();
    self->manager_handler_id[MANAGER_HANDLER_VALID_CHANGED] =
        ofono_manager_add_valid_changed_handler(self->manager,
            mms_connman_ofono_valid_changed, self);
    self->manager_handler_id[MANAGER_HANDLER_MODEM_ADDED] =
        ofono_manager_add_modem_added_handler(self->manager,
            mms_connman_ofono_modem_added, self);
    self->manager_handler_id[MANAGER_HANDLER_MODEM_REMOVED] =
        ofono_manager_add_modem_removed_handler(self->manager,
            mms_connman_ofono_modem_removed, self);
    if (self->manager->valid) mms_connman_ofono_init_modems(self);
    return &self->cm;
}

/**
 * Cleanup callback for the modem context
 */
static
void
mms_ofono_modem_cleanup(
    gpointer data)
{
    MMSOfonoModem* context = data;
    ofono_simmgr_remove_handlers(context->simmgr, context->simmgr_handler_id,
        G_N_ELEMENTS(context->simmgr_handler_id));
    ofono_simmgr_unref(context->simmgr);
    ofono_connmgr_unref(context->connmgr);
    ofono_modem_unref(context->modem);
    if (context->conn) {
        mms_connection_close(context->conn);
        mms_connection_unref(context->conn);
    }
    g_free(context);
}

/**
 * First stage of deinitialization (release all references).
 * May be called more than once in the lifetime of the object.
 */
static
void
mms_connman_ofono_dispose(
    GObject* object)
{
    MMSConnManOfono* self = MMS_CONNMAN_OFONO(object);
    MMS_VERBOSE_("");
    self->default_modem = NULL;
    g_hash_table_remove_all(self->modems);
    if (self->manager) {
        ofono_manager_remove_handlers(self->manager, self->manager_handler_id,
            G_N_ELEMENTS(self->manager_handler_id));
        ofono_manager_unref(self->manager);
        self->manager = NULL;
    }
    G_OBJECT_CLASS(mms_connman_ofono_parent_class)->dispose(object);
}

/**
 * Final stage of deinitialization
 */
static
void
mms_connman_ofono_finalize(
    GObject* object)
{
    MMSConnManOfono* self = MMS_CONNMAN_OFONO(object);
    MMS_VERBOSE_("");
    g_hash_table_destroy(self->modems);
    G_OBJECT_CLASS(mms_connman_ofono_parent_class)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
mms_connman_ofono_class_init(
    MMSConnManOfonoClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    klass->fn_default_imsi = mms_connman_ofono_default_imsi;
    klass->fn_open_connection = mms_connman_ofono_open_connection;
    object_class->dispose = mms_connman_ofono_dispose;
    object_class->finalize = mms_connman_ofono_finalize;
}

/**
 * Per instance initializer
 */
static
void
mms_connman_ofono_init(
    MMSConnManOfono* self)
{
    self->modems = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, mms_ofono_modem_cleanup);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
