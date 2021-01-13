/*
 * Copyright (C) 2014-2020 Jolla Ltd.
 * Copyright (C) 2014-2020 Slava Monich <slava.monich@jolla.com>
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

#ifndef SAILFISH_MMS_SETTINGS_H
#define SAILFISH_MMS_SETTINGS_H

#include "mms_lib_types.h"

/* Static configuration, chosen at startup and never changing since then */
struct mms_config {
    const char* root_dir;       /* Root directory for storing MMS files */
    int retry_secs;             /* Retry timeout in seconds */
    int network_idle_secs;      /* Network inactivity timeout */
    int idle_secs;              /* Service inactivity timeout */
    gboolean convert_to_utf8;   /* Convert text parts to UTF-8 */
    gboolean keep_temp_files;   /* Keep temporary files around */
    gboolean attic_enabled;     /* Keep unrecognized push message in attic */
};

typedef struct mms_config_copy {
    MMSConfig config;           /* Config data */
    char* root_dir;             /* Allocated copy of root_dir */
} MMSConfigCopy;

#define MMS_CONFIG_DEFAULT_ROOT_DIR             NULL /* Dynamic */
#define MMS_CONFIG_DEFAULT_RETRY_SECS           (15)
#define MMS_CONFIG_DEFAULT_NETWORK_IDLE_SECS    (10)
#define MMS_CONFIG_DEFAULT_IDLE_SECS            (30)

/* Persistent mutable per-SIM settings */
struct mms_settings_sim_data {
    const char* user_agent;     /* User agent string */
    const char* uaprof;         /* User agent profile string */
    unsigned int size_limit;    /* Maximum size of m-Send.req PDU */
    unsigned int max_pixels;    /* Pixel limit for outbound images */
    gboolean allow_dr;          /* Allow sending delivery reports */
};

/* Copy of per-SIM settings */
typedef struct mms_settings_sim_data_copy {
    MMSSettingsSimData data;    /* Settings data */
    char* user_agent;           /* Allocated copy of user_agent */
    char* uaprof;               /* Allocated copy of uaprof */
} MMSSettingsSimDataCopy;

/* Instance */
struct mms_settings {
    GObject object;
    const MMSConfig* config;
    MMSSettingsSimDataCopy sim_defaults;
    unsigned int flags;

#define MMS_SETTINGS_FLAG_OVERRIDE_USER_AGENT   (0x01)
#define MMS_SETTINGS_FLAG_OVERRIDE_SIZE_LIMIT   (0x02)
#define MMS_SETTINGS_FLAG_OVERRIDE_MAX_PIXELS   (0x04)
#define MMS_SETTINGS_FLAG_OVERRIDE_ALLOW_DR     (0x08)
#define MMS_SETTINGS_FLAG_OVERRIDE_UAPROF       (0x10)
};

/* Class */
typedef struct mms_settings_class {
    GObjectClass parent;
    const MMSSettingsSimData* (*fn_get_sim_data)(
        MMSSettings* settings,
        const char* imsi);
} MMSSettingsClass;

/* Default values. If the GSettings backend is used then these
 * should match the default values defined in the GSettings
 * schema (org.nemomobile.mms.sim.gschema.xml) */
#define MMS_SETTINGS_DEFAULT_USER_AGENT "Mozilla/5.0 (Sailfish; Jolla)"
#define MMS_SETTINGS_DEFAULT_UAPROF     "http://static.jolla.com/uaprof/Jolla.xml"
#define MMS_SETTINGS_DEFAULT_SIZE_LIMIT (300*1024)
#define MMS_SETTINGS_DEFAULT_MAX_PIXELS (3000000)
#define MMS_SETTINGS_DEFAULT_ALLOW_DR   TRUE

GType mms_settings_get_type(void);
#define MMS_TYPE_SETTINGS (mms_settings_get_type())
#define MMS_SETTINGS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), \
        MMS_TYPE_SETTINGS, MMSSettingsClass))

MMSSettings*
mms_settings_ref(
    MMSSettings* settings);

void
mms_settings_unref(
    MMSSettings* settings);

MMSSettings*
mms_settings_default_new(
    const MMSConfig* config);

const MMSSettingsSimData*
mms_settings_get_sim_data(
    MMSSettings* settings,
    const char* imsi);

void
mms_settings_sim_data_default(
    MMSSettingsSimData* data);

gboolean
mms_settings_load_defaults(
    const char* path,
    MMSConfigCopy* config,
    MMSSettingsSimDataCopy* simconfig,
    GError** error);

void
mms_settings_parse(
    GKeyFile* file,
    MMSConfigCopy* config,
    MMSSettingsSimDataCopy* data);

void
mms_settings_set_sim_defaults(
    MMSSettings* settings,
    const MMSSettingsSimData* data);

MMSSettingsSimDataCopy*
mms_settings_sim_data_copy_new(
    const MMSSettingsSimData* data);

void
mms_settings_sim_data_copy_free(
    MMSSettingsSimDataCopy* data);

void
mms_settings_sim_data_copy(
    MMSSettingsSimDataCopy* dest,
    const MMSSettingsSimData* src);

#define mms_settings_sim_data_reset(data) \
    mms_settings_sim_data_copy(data,NULL)

#endif /* SAILFISH_MMS_SETTINGS_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
