/*
 * Copyright (C) 2014-2018 Jolla Ltd.
 * Copyright (C) 2014-2018 Slava Monich <slava.monich@jolla.com>
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

#include "mms_settings.h"

/* Logging */
#define GLOG_MODULE_NAME mms_settings_log
#include "mms_lib_log.h"
#include <gutil_log.h>
GLOG_MODULE_DEFINE("mms-settings");

#define SETTINGS_GLOBAL_GROUP                   "Global"
#define SETTINGS_GLOBAL_KEY_ROOT_DIR            "RootDir"
#define SETTINGS_GLOBAL_KEY_RETRY_SEC           "RetryDelay"
#define SETTINGS_GLOBAL_KEY_NETWORK_IDLE_SEC    "NetworkIdleTimeout"
#define SETTINGS_GLOBAL_KEY_IDLE_SEC            "IdleTimeout"

#define SETTINGS_DEFAULTS_GROUP                 "Defaults"
#define SETTINGS_DEFAULTS_KEY_USER_AGENT        "UserAgent"
#define SETTINGS_DEFAULTS_KEY_UAPROF            "UAProfile"
#define SETTINGS_DEFAULTS_KEY_SIZE_LIMIT        "SizeLimit"
#define SETTINGS_DEFAULTS_KEY_MAX_PIXELS        "MaxPixels"
#define SETTINGS_DEFAULTS_KEY_ALLOW_DR          "SendDeliveryReport"

G_DEFINE_TYPE(MMSSettings, mms_settings, G_TYPE_OBJECT)
#define MMS_SETTINGS_GET_CLASS(obj)  \
    (G_TYPE_INSTANCE_GET_CLASS((obj), MMS_TYPE_SETTINGS, MMSSettingsClass))
#define MMS_SETTINGS(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), MMS_TYPE_SETTINGS, MMSSettings))

MMSSettings*
mms_settings_ref(
    MMSSettings* s)
{
    if (s) g_object_ref(MMS_SETTINGS(s));
    return s;
}

void
mms_settings_unref(
    MMSSettings* s)
{
    if (s) g_object_unref(MMS_SETTINGS(s));
}

void
mms_settings_sim_data_default(
    MMSSettingsSimData* data)
{
    memset(data, 0, sizeof(*data));
    data->user_agent = MMS_SETTINGS_DEFAULT_USER_AGENT;
    data->uaprof = MMS_SETTINGS_DEFAULT_UAPROF;
    data->size_limit = MMS_SETTINGS_DEFAULT_SIZE_LIMIT;
    data->max_pixels = MMS_SETTINGS_DEFAULT_MAX_PIXELS;
    data->allow_dr = MMS_SETTINGS_DEFAULT_ALLOW_DR;
}

static
gboolean
mms_settings_parse_int(
    GKeyFile* file,
    const char* group,
    const char* key,
    int* out,
    int min_value)
{
    GError* error = NULL;
    const int i = g_key_file_get_integer(file, group, key, &error);

    if (error) {
        g_error_free(error);
    } else if (i >= min_value) {
        *out = i;
        GDEBUG("%s = %d", key, i);
        return TRUE;
    }
    return FALSE;
}

static
void
mms_settings_parse_uint(
    GKeyFile* file,
    const char* group,
    const char* key,
    unsigned int* out)
{
    int value;

    if (mms_settings_parse_int(file, group, key, &value, 1)) {
        *out = value;
    }
}

static
void
mms_settings_parse_global_config(
    MMSConfigCopy* global,
    GKeyFile* file)
{
    const char* group = SETTINGS_GLOBAL_GROUP;
    MMSConfig* config = &global->config;
    char* s;

    s = g_key_file_get_string(file, group,
        SETTINGS_GLOBAL_KEY_ROOT_DIR, NULL);
    if (s) {
        g_free(global->root_dir);
        config->root_dir = global->root_dir = s;
        GDEBUG("%s = %s", SETTINGS_GLOBAL_KEY_ROOT_DIR, s);
    }

    mms_settings_parse_int(file, group,
        SETTINGS_GLOBAL_KEY_RETRY_SEC,
        &config->retry_secs, 0);

    mms_settings_parse_int(file, group,
        SETTINGS_GLOBAL_KEY_NETWORK_IDLE_SEC,
        &config->network_idle_secs, 0);

    mms_settings_parse_int(file, group,
        SETTINGS_GLOBAL_KEY_IDLE_SEC,
        &config->idle_secs, 0);
}

static
void
mms_settings_parse_sim_config(
    MMSSettingsSimDataCopy* defaults,
    GKeyFile* file)
{
    const char* group = SETTINGS_DEFAULTS_GROUP;
    GError* error = NULL;
    gboolean b;
    char* s;

    s = g_key_file_get_string(file, group,
        SETTINGS_DEFAULTS_KEY_USER_AGENT, NULL);
    if (s) {
        g_free(defaults->user_agent);
        defaults->data.user_agent = defaults->user_agent = s;
        GDEBUG("%s = %s", SETTINGS_DEFAULTS_KEY_USER_AGENT, s);
    }

    s = g_key_file_get_string(file, group,
        SETTINGS_DEFAULTS_KEY_UAPROF, NULL);
    if (s) {
        g_free(defaults->uaprof);
        defaults->data.uaprof = defaults->uaprof = s;
        GDEBUG("%s = %s", SETTINGS_DEFAULTS_KEY_UAPROF, s);
    }

    mms_settings_parse_uint(file, group,
        SETTINGS_DEFAULTS_KEY_SIZE_LIMIT,
        &defaults->data.size_limit);

    mms_settings_parse_uint(file, group,
        SETTINGS_DEFAULTS_KEY_MAX_PIXELS,
        &defaults->data.max_pixels);

    b = g_key_file_get_boolean(file, group,
        SETTINGS_DEFAULTS_KEY_ALLOW_DR, &error);
    if (error) {
        g_error_free(error);
        error = NULL;
    } else {
        GDEBUG("%s = %s", SETTINGS_DEFAULTS_KEY_ALLOW_DR, b ? "on" : "off");
        defaults->data.allow_dr = b;
    }
}

gboolean
mms_settings_load_defaults(
    const char* path,
    MMSConfigCopy* config,
    MMSSettingsSimDataCopy* data,
    GError** error)
{
    gboolean ok = FALSE;
    GKeyFile* file = g_key_file_new();
    if (g_key_file_load_from_file(file, path, 0, error)) {
        GDEBUG("Loading %s", path);
        mms_settings_parse_global_config(config, file);
        mms_settings_parse_sim_config(data, file);
        ok = TRUE;
    }
    g_key_file_free(file);
    return ok;
}

void
mms_settings_sim_data_copy(
    MMSSettingsSimDataCopy* dest,
    const MMSSettingsSimData* src)
{
    g_free(dest->user_agent);
    g_free(dest->uaprof);
    if (src) {
        dest->data = *src;
        dest->data.user_agent = dest->user_agent = g_strdup(src->user_agent);
        dest->data.uaprof = dest->uaprof = g_strdup(src->uaprof);
    } else {
        dest->user_agent = NULL;
        dest->uaprof = NULL;
        mms_settings_sim_data_default(&dest->data);
    }
}

MMSSettingsSimDataCopy*
mms_settings_sim_data_copy_new(
    const MMSSettingsSimData* data)
{
    MMSSettingsSimDataCopy* copy = NULL;
    if (data) {
        copy = g_new0(MMSSettingsSimDataCopy, 1);
        mms_settings_sim_data_copy(copy, data);
    }
    return copy;
}

void
mms_settings_sim_data_copy_free(
    MMSSettingsSimDataCopy* copy)
{
    if (copy) {
        mms_settings_sim_data_reset(copy);
        g_free(copy);
    }
}

void
mms_settings_set_sim_defaults(
    MMSSettings* settings,
    const MMSSettingsSimData* data)
{
    if (settings) {
        mms_settings_sim_data_copy(&settings->sim_defaults, data);
    }
}

const MMSSettingsSimData*
mms_settings_get_sim_data(
    MMSSettings* settings,
    const char* imsi)
{
    if (settings) {
        MMSSettingsClass* klass = MMS_SETTINGS_GET_CLASS(settings);
        return klass->fn_get_sim_data(settings, imsi);
    }
    return NULL;
}

MMSSettings*
mms_settings_default_new(
    const MMSConfig* config)
{
    MMSSettings* settings = g_object_new(MMS_TYPE_SETTINGS, NULL);
    settings->config = config;
    return settings;
}

static
const MMSSettingsSimData*
mms_settings_get_default_sim_data(
    MMSSettings* settings,
    const char* imsi)
{
    return &settings->sim_defaults.data;
}

static
void
mms_settings_finalize(
    GObject* object)
{
    MMSSettings* settings = MMS_SETTINGS(object);
    mms_settings_sim_data_reset(&settings->sim_defaults);
    G_OBJECT_CLASS(mms_settings_parent_class)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
mms_settings_class_init(
    MMSSettingsClass* klass)
{
    klass->fn_get_sim_data = mms_settings_get_default_sim_data;
    G_OBJECT_CLASS(klass)->finalize = mms_settings_finalize;
}

/**
 * Per instance initializer
 */
static
void
mms_settings_init(
    MMSSettings* settings)
{
    mms_settings_sim_data_default(&settings->sim_defaults.data);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
