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

#ifndef JOLLA_MMS_ENGINE_H
#define JOLLA_MMS_ENGINE_H

#include <gio/gio.h>
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

#ifndef MMS_ENGINE_CONFIG_FILE
/* Default config file */
#  define MMS_ENGINE_CONFIG_FILE    "/etc/mms-engine.conf"
#endif /* MMS_ENGINE_CONFIG_FILE */

typedef struct mms_engine MMSEngine;

MMSEngine*
mms_engine_new(
    const MMSConfig* config,
    const MMSSettingsSimData* defaults,
    unsigned int flags,
    MMSLogModule* log_modules[],
    int log_count);

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
