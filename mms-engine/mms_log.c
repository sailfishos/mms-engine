/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#include "mms_log.h"

#include <dbuslog_server_gio.h>
#include <dbuslog_util.h>

#include <gutil_log.h>
#include <gutil_macros.h>

enum {
    DBUSLOG_EVENT_CATEGORY_ENABLED,
    DBUSLOG_EVENT_CATEGORY_DISABLED,
    DBUSLOG_EVENT_CATEGORY_LEVEL_CHANGED,
    DBUSLOG_EVENT_DEFAULT_LEVEL_CHANGED,
    DBUSLOG_EVENT_COUNT
};

typedef struct mms_log_priv {
    MMSLog log;
    GHashTable* map;
    gulong event_id[DBUSLOG_EVENT_COUNT];
    GLogProc2 default_func;
} MMSLogPriv;

static MMSLogPriv* mms_log_active = NULL;

static
void
mms_log_func(
    MMSLogPriv* self,
    const GLogModule* log,
    int level,
    const char* format,
    va_list va)
{
    va_list va2;

    va_copy(va2, va);
    dbus_log_server_logv(self->log.server, dbus_log_level_from_gutil(level),
        log->name, format, va2);
    va_end(va2);
    if (self->default_func) {
        self->default_func(log, level, format, va);
    }
}

static
void
mms_log_hook(
    const GLogModule* log,
    int level,
    const char* format,
    va_list va)
{
    if (mms_log_active) {
        mms_log_func(mms_log_active, log, level, format, va);
    }
}

static
void
mms_log_add_category(
    MMSLogPriv* self,
    GLogModule* module)
{
    gulong flags = 0;

    GVERBOSE("Adding \"%s\"", module->name);
    g_hash_table_replace(self->map, (gpointer)module->name, module);
    if (!(module->flags & GLOG_FLAG_DISABLE)) {
        flags |= (DBUSLOG_CATEGORY_FLAG_ENABLED |
            DBUSLOG_CATEGORY_FLAG_ENABLED_BY_DEFAULT);
    }
    if (module->flags & GLOG_FLAG_HIDE_NAME) {
        flags |= DBUSLOG_CATEGORY_FLAG_HIDE_NAME;
    }
    dbus_log_server_add_category(self->log.server, module->name,
        dbus_log_level_from_gutil(module->level), flags);
}

/*==========================================================================*
 * Events
 *==========================================================================*/

static
void
mms_log_category_enabled(
    DBusLogServer* server,
    const char* name,
    gpointer user_data)
{
    MMSLogPriv* self = user_data;
    GLogModule* module = g_hash_table_lookup(self->map, name);

    GASSERT(module);
    if (module) {
        module->flags &= ~GLOG_FLAG_DISABLE;
    }
}

static
void
mms_log_category_disabled(
    DBusLogServer* server,
    const char* name,
    gpointer user_data)
{
    MMSLogPriv* self = user_data;
    GLogModule* module = g_hash_table_lookup(self->map, name);

    GASSERT(module);
    if (module) {
        module->flags |= GLOG_FLAG_DISABLE;
    }
}

static
void
mms_log_category_level_changed(
    DBusLogServer* server,
    const char* name,
    DBUSLOG_LEVEL dbus_level,
    gpointer user_data)
{
    MMSLogPriv* self = user_data;
    GLogModule* module = g_hash_table_lookup(self->map, name);
    const int level = dbus_log_level_to_gutil(dbus_level);

    GASSERT(module);
    if (module && level != GLOG_LEVEL_NONE) {
        module->level = level;
    }
}

static
void
mms_log_default_level_changed(
    DBusLogServer* server,
    DBUSLOG_LEVEL dbus_level,
    gpointer user_data)
{
    const int level = dbus_log_level_to_gutil(dbus_level);

    if (level != GLOG_LEVEL_NONE) {
        gutil_log_default.level = level;
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

MMSLog*
mms_log_new(
    GBusType bus,
    GLogModule* modules[]) /* NULL terminated */
{
    MMSLogPriv* self = g_new0(MMSLogPriv, 1);
    GLogModule** ptr = modules;

    self->log.server = dbus_log_server_new(bus, NULL, "/");
    self->map = g_hash_table_new(g_str_hash, g_str_equal);
    while (*ptr) {
        mms_log_add_category(self, *ptr++);
    }

    self->event_id[DBUSLOG_EVENT_CATEGORY_ENABLED] =
        dbus_log_server_add_category_enabled_handler(self->log.server,
            mms_log_category_enabled, self);
    self->event_id[DBUSLOG_EVENT_CATEGORY_DISABLED] =
        dbus_log_server_add_category_disabled_handler(self->log.server,
            mms_log_category_disabled, self);
    self->event_id[DBUSLOG_EVENT_CATEGORY_LEVEL_CHANGED] =
        dbus_log_server_add_category_level_handler(self->log.server,
            mms_log_category_level_changed, self);
    self->event_id[DBUSLOG_EVENT_DEFAULT_LEVEL_CHANGED] =
        dbus_log_server_add_default_level_handler(self->log.server,
            mms_log_default_level_changed, self);

    mms_log_active = self;
    self->default_func = gutil_log_func2;
    gutil_log_func2 = mms_log_hook;
    dbus_log_server_set_default_level(self->log.server,
        dbus_log_level_from_gutil(gutil_log_default.level));
    return &self->log;
}

void
mms_log_free(
    MMSLog* log)
{
    MMSLogPriv* self = G_CAST(log,MMSLogPriv,log);

    if (mms_log_active == self) {
        mms_log_active = NULL;
        gutil_log_func2 = self->default_func;
    }
    dbus_log_server_remove_all_handlers(log->server, self->event_id);
    dbus_log_server_unref(log->server);
    g_hash_table_destroy(self->map);
    g_free(self);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
