/*
 * Copyright (C) 2013-2020 Jolla Ltd.
 * Copyright (C) 2013-2020 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2019 Open Mobile Platform LLC.
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

#ifndef JOLLA_MMS_ENGINE_H
#define JOLLA_MMS_ENGINE_H

#include <gio/gio.h>
#include <dbusaccess_types.h>

#include "mms_settings.h"
#include "mms_version.h"

#define MMS_APP_LOG_PREFIX  "mms-engine"

#define MMS_ENGINE_SERVICE  "org.nemomobile.MmsEngine"
#define MMS_ENGINE_PATH      "/"

#define MMS_ENGINE_FLAG_KEEP_RUNNING        (0x01)
#define MMS_ENGINE_FLAG_OVERRIDE_USER_AGENT (0x02)
#define MMS_ENGINE_FLAG_OVERRIDE_SIZE_LIMIT (0x04)
#define MMS_ENGINE_FLAG_OVERRIDE_MAX_PIXELS (0x08)
#define MMS_ENGINE_FLAG_OVERRIDE_UAPROF     (0x10)
#define MMS_ENGINE_FLAG_DISABLE_DBUS_LOG    (0x20)

#ifndef MMS_ENGINE_CONFIG_FILE
/* Default config file */
#  define MMS_ENGINE_CONFIG_FILE    "/etc/mms-engine.conf"
#endif /* MMS_ENGINE_CONFIG_FILE */

#define MMS_ENGINE_DBUS_METHODS(m) \
    m(CANCEL) \
    m(RECEIVE_MESSAGE) \
    m(SEND_READ_REPORT) \
    m(SEND_MESSAGE) \
    m(PUSH) \
    m(PUSH_NOTIFY) \
    m(SET_LOG_LEVEL) \
    m(SET_LOG_TYPE) \
    m(GET_VERSION) \
    m(MIGRATE_SETTINGS) \
    m(EXIT)

typedef enum mms_engine_action {
    /* Action ids must be non-zero, shift those by one */
    MMS_ENGINE_ACTION_NONE = 0,
    #define MMS_ENGINE_ACTION_(id) MMS_ENGINE_ACTION_##id,
    MMS_ENGINE_DBUS_METHODS(MMS_ENGINE_ACTION_)
    #undef MMS_ENGINE_ACTION_
} MMS_ENGINE_ACTION;

typedef struct mms_engine MMSEngine;

typedef struct engine_dbus_config {
    GBusType type;
    DAPolicy* engine_access;
    DAPolicy* tx_list_access;
    DAPolicy* tx_access;
} MMSEngineDbusConfig;

MMSEngine*
mms_engine_new(
    const MMSConfig* config,
    const MMSSettingsSimData* defaults,
    const MMSEngineDbusConfig* dbus_config,
    MMSLogModule* log_modules[], /* NULL terminated */
    unsigned int flags);

MMSEngine*
mms_engine_ref(
    MMSEngine* engine);

void
mms_engine_unref(
    MMSEngine* engine);

void
mms_engine_run(
    MMSEngine* engine,
    GMainLoop* loop);

void
mms_engine_stop(
    MMSEngine* engine);

gboolean
mms_engine_register(
    MMSEngine* engine,
    GDBusConnection* bus,
    GError** error);

void
mms_engine_unregister(
    MMSEngine* engine);

#endif /* JOLLA_MMS_ENGINE_H */
