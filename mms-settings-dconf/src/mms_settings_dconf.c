/*
 * Copyright (C) 2014-2016 Jolla Ltd.
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

#include "mms_settings_dconf.h"
#include <dconf.h>

/* Logging */
#define GLOG_MODULE_NAME mms_settings_log_dconf
#include "mms_lib_log.h"
#include <gutil_log.h>
GLOG_MODULE_DEFINE("mms-settings-dconf");

typedef MMSSettingsClass MMSSettingsDconfClass;
typedef struct mms_settings_dconf {
    MMSSettings settings;
    MMSSettingsSimDataCopy imsi_data;
    char* imsi;
    char* dir;
    DConfClient* client;
    gulong changed_signal_id;
} MMSSettingsDconf;

G_DEFINE_TYPE(MMSSettingsDconf, mms_settings_dconf, MMS_TYPE_SETTINGS)
#define MMS_TYPE_SETTINGS_DCONF mms_settings_dconf_get_type()
#define MMS_SETTINGS_DCONF_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), \
        MMS_TYPE_SETTINGS_DCONF, MMSSettingsDconfClass))
#define MMS_SETTINGS_DCONF(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        MMS_TYPE_SETTINGS_DCONF, MMSSettingsDconf))

#define MMS_DCONF_CHANGED_SIGNAL    "changed"
#define MMS_DCONF_PATH_PREFIX_OLD   "/"
#define MMS_DCONF_PATH_SUFFIX_OLD   "/"
#define MMS_DCONF_PATH_PREFIX       "/imsi/"
#define MMS_DCONF_PATH_SUFFIX       "/mms/"

#define mms_settings_dconf_path(imsi) g_strconcat(\
    MMS_DCONF_PATH_PREFIX, imsi, MMS_DCONF_PATH_SUFFIX, NULL)
#define mms_settings_dconf_path_old(imsi) g_strconcat(\
    MMS_DCONF_PATH_PREFIX_OLD, imsi, MMS_DCONF_PATH_SUFFIX_OLD, NULL)

#define MMS_DCONF_KEY_USER_AGENT    "user-agent"
#define MMS_DCONF_KEY_UAPROF        "user-agent-profile"
#define MMS_DCONF_KEY_SIZE_LIMIT    "max-message-size"
#define MMS_DCONF_KEY_MAX_PIXELS    "max-pixels"
#define MMS_DCONF_KEY_ALLOW_DR      "allow-delivery-reports"

typedef struct mms_settings_dconf_key {
    const char* name;
    unsigned int override_flag;
    void (*fn_update)(MMSSettingsSimDataCopy* dest, GVariant* variant);
} MMSSettingsDconfKey;

static
gboolean
mms_settings_dconf_get_uint32(
    GVariant* variant,
    unsigned int* value)
{
    if (variant) {
        GVariantClass klass = g_variant_classify(variant);
        if (klass == G_VARIANT_CLASS_UINT32) {
            /* Normal, most common use case */
            *value = g_variant_get_uint32(variant);
            return TRUE;
        } else if (klass == G_VARIANT_CLASS_INT32) {
            /* Typical result of updating dconf value from command line */
            gint32 i32 = g_variant_get_int32(variant);
            GASSERT(i32 >= 0);
            if (i32 >= 0) {
                *value = i32;
                return TRUE;
            }
            /* The rest is not so typical */
        } else if (klass == G_VARIANT_CLASS_INT16) {
            gint16 i16 = g_variant_get_int16(variant);
            GASSERT(i16 >= 0);
            if (i16 >= 0) {
                *value = i16;
                return TRUE;
            }
        } else if (klass == G_VARIANT_CLASS_INT64) {
            gint64 i64 = g_variant_get_int64(variant);
            GASSERT(i64 >= 0 && i64 <= (gint64)UINT_MAX);
            if (i64 >= 0 && i64 <= (gint64)UINT_MAX) {
                *value = (unsigned int)i64;
                return TRUE;
            }
        } else if (klass == G_VARIANT_CLASS_BYTE) {
            *value = g_variant_get_byte(variant);
            return TRUE;
        } else if (klass == G_VARIANT_CLASS_UINT16) {
            *value = g_variant_get_uint16(variant);
            return TRUE;
        } else if (klass == G_VARIANT_CLASS_UINT64) {
            guint64 u64 = g_variant_get_uint64(variant);
            if (u64 <= UINT_MAX) {
                *value = (unsigned int)u64;
                return TRUE;
            }
        } else if (klass == G_VARIANT_CLASS_DOUBLE) {
            gdouble d = g_variant_get_double(variant);
            if (d >= 0.0 && d <= (double)UINT_MAX) {
                *value = (unsigned int)d;
                return TRUE;
            }
        } else {
            GERR("Unexpected variant type \'%c\'", (char)klass);
            return FALSE;
        }
        GERR("Unable to convert variant type \'%c\'", (char)klass);
    }
    return FALSE;
}

static
void
mms_settings_dconf_update_user_agent(
    MMSSettingsSimDataCopy* dest,
    GVariant* variant)
{
    const char* value = g_variant_get_string(variant, NULL);
    GDEBUG(MMS_DCONF_KEY_USER_AGENT " = %s", value);
    g_free(dest->user_agent);
    dest->data.user_agent = dest->user_agent = g_strdup(value);
}

static
void
mms_settings_dconf_update_uaprof(
    MMSSettingsSimDataCopy* dest,
    GVariant* variant)
{
    const char* value = g_variant_get_string(variant, NULL);
    GDEBUG(MMS_DCONF_KEY_UAPROF " = %s", value);
    g_free(dest->uaprof);
    dest->data.uaprof = dest->uaprof = g_strdup(value);
}

static
void
mms_settings_dconf_update_size_limit(
    MMSSettingsSimDataCopy* dest,
    GVariant* variant)
{
    if (mms_settings_dconf_get_uint32(variant, &dest->data.size_limit)) {
        GDEBUG(MMS_DCONF_KEY_SIZE_LIMIT " = %u", dest->data.size_limit);
    } else {
        GWARN("Unable to decode " MMS_DCONF_KEY_SIZE_LIMIT " value");
    }
}

static
void
mms_settings_dconf_update_max_pixels(
    MMSSettingsSimDataCopy* dest,
    GVariant* variant)
{
    if (mms_settings_dconf_get_uint32(variant, &dest->data.max_pixels)) {
        GDEBUG(MMS_DCONF_KEY_MAX_PIXELS " = %u", dest->data.max_pixels);
    } else {
        GWARN("Unable to decode " MMS_DCONF_KEY_MAX_PIXELS " value");
    }
}

static
void
mms_settings_dconf_update_allow_dr(
    MMSSettingsSimDataCopy* dest,
    GVariant* variant)
{
    const gboolean value = g_variant_get_boolean(variant);
    GDEBUG(MMS_DCONF_KEY_ALLOW_DR " = %s", value ? "true" : "false");
    dest->data.allow_dr = value;
}

static const MMSSettingsDconfKey mms_settings_dconf_keys[] = {
    {
        MMS_DCONF_KEY_USER_AGENT,
        MMS_SETTINGS_FLAG_OVERRIDE_USER_AGENT,
        mms_settings_dconf_update_user_agent
    },{
        MMS_DCONF_KEY_UAPROF,
        MMS_SETTINGS_FLAG_OVERRIDE_UAPROF,
        mms_settings_dconf_update_uaprof
    }, {
        MMS_DCONF_KEY_SIZE_LIMIT,
        MMS_SETTINGS_FLAG_OVERRIDE_SIZE_LIMIT,
        mms_settings_dconf_update_size_limit
    }, {
        MMS_DCONF_KEY_MAX_PIXELS,
        MMS_SETTINGS_FLAG_OVERRIDE_MAX_PIXELS,
        mms_settings_dconf_update_max_pixels
    },{
        MMS_DCONF_KEY_ALLOW_DR,
        MMS_SETTINGS_FLAG_OVERRIDE_ALLOW_DR,
        mms_settings_dconf_update_allow_dr
    }
};

static
const MMSSettingsDconfKey*
mms_settings_dconf_key(
    const char* name)
{
    unsigned int i;
    for (i=0; i<G_N_ELEMENTS(mms_settings_dconf_keys); i++) {
        if (!strcmp(name, mms_settings_dconf_keys[i].name)) {
            return mms_settings_dconf_keys + i;
        }
    }
    return NULL;
}

static
void
mms_settings_dconf_key_changed(
    MMSSettingsDconf* dconf,
    const char* name)
{
    const MMSSettingsDconfKey* key = mms_settings_dconf_key(name);
    if (key) {
        unsigned int override = key->override_flag;
        if (override && (dconf->settings.flags & override) == override) {
            /* Value is fixed from the command line */
            GDEBUG("%s changed (ignored)", name);
        } else {
            /* Query and parse the value */
            char* path = g_strconcat(dconf->dir, name, NULL);
            GVariant* value = dconf_client_read(dconf->client, path);
            GASSERT(value);
            if (value) {
                key->fn_update(&dconf->imsi_data, value);
                g_variant_unref(value);
            }
            g_free(path);
        }
    } else {
        GDEBUG("Key %s/%s changed - ignoring", dconf->dir, name);
    }
}

static
void
mms_settings_dconf_changed(
    DConfClient* client,
    const char* prefix,
    GStrv changes,
    const char* tag,
    MMSSettingsDconf* dconf)
{
    GASSERT(dconf->client == client);
    if (dconf->dir) {
        if (*changes && **changes) {
            /* Multiple values have changed */
            if (!strcmp(dconf->dir, prefix)) {
                while (*changes) {
                    mms_settings_dconf_key_changed(dconf, *changes);
                    changes++;
                }
                return;
            }
        } else {
            /* Single value has changed */
            const size_t dlen = strlen(dconf->dir);
            const size_t plen = strlen(prefix);
            if (plen > dlen && strncmp(prefix, dconf->dir, dlen) == 0) {
                mms_settings_dconf_key_changed(dconf, prefix + dlen);
                return;
            }
        }
    }
    GDEBUG("Path %s has changed - ignoring", prefix);
}

static
void
mms_settings_dconf_unwatch(
    MMSSettingsDconf* dconf)
{
    if (dconf->dir) {
        unsigned int i;
        GDEBUG("Detaching from %s", dconf->dir);
        for (i=0; i<G_N_ELEMENTS(mms_settings_dconf_keys); i++) {
            const MMSSettingsDconfKey* key = mms_settings_dconf_keys + i;
            char* path = g_strconcat(dconf->dir, key->name, NULL);
            dconf_client_unwatch_sync(dconf->client, path);
            g_free(path);
        }
        g_free(dconf->dir);
        dconf->dir = NULL;
    }
    if (dconf->imsi) {
        g_free(dconf->imsi);
        dconf->imsi = NULL;
    }
}

static
const MMSSettingsSimData*
mms_settings_dconf_get_sim_data(
    MMSSettings* settings,
    const char* imsi)
{
    MMSSettingsDconf* dconf = MMS_SETTINGS_DCONF(settings);
    if (imsi && imsi[0] && dconf->client) {
        if (!dconf->imsi || strcmp(dconf->imsi, imsi)) {
            char* dir = mms_settings_dconf_path(imsi);
            char* dir_old = mms_settings_dconf_path_old(imsi);
            gchar** names;
            gint j, n = 0;
            unsigned int i;

            mms_settings_dconf_unwatch(dconf);
            mms_settings_sim_data_copy(&dconf->imsi_data,
                &settings->sim_defaults.data);

            GDEBUG("Attaching to %s", dir);

            /* Migrate settings from old to the new location. */
            names = dconf_client_list(dconf->client, dir_old, &n);
            for (j=0; j<n; j++) {
                const char* name = names[j];
                const MMSSettingsDconfKey* key = mms_settings_dconf_key(name);
                if (key) {
                    /* Known key found - migrate it */
                    char* from = g_strconcat(dir_old, name, NULL);
                    char* to = g_strconcat(dir, name, NULL);
                    GVariant* value = dconf_client_read(dconf->client, from);
                    GDEBUG("Migrating %s -> %s", from, to);
                    GASSERT(value);
                    if (value) {
                        GError* error = NULL;
                        if (!dconf_client_write_sync(dconf->client,
                            to, value, NULL, NULL, &error) ||
                            !dconf_client_write_sync(dconf->client,
                            from, NULL, NULL, NULL, &error)) {
                            GERR("%s", GERRMSG(error));
                            g_error_free(error);
                        }
                        g_variant_unref(value);
                    }
                    g_free(from);
                    g_free(to);
                }
            }

            g_strfreev(names);
            g_free(dir_old);

            dconf->imsi = g_strdup(imsi);
            dconf->dir = dir;

            /* Attach to the new path and query current settings */
            for (i=0; i<G_N_ELEMENTS(mms_settings_dconf_keys); i++) {
                const MMSSettingsDconfKey* key = mms_settings_dconf_keys + i;
                char* path = g_strconcat(dir, key->name, NULL);
                GVariant* value = dconf_client_read(dconf->client, path);
                dconf_client_watch_sync(dconf->client, path);
                if (value) {
                    key->fn_update(&dconf->imsi_data, value);
                    g_variant_unref(value);
                }
                g_free(path);
            }
        }
        return &dconf->imsi_data.data;
    } else {
        return &dconf->settings.sim_defaults.data;
    }
}

static
void
mms_settings_dconf_dispose(
    GObject* object)
{
    MMSSettingsDconf* dconf = MMS_SETTINGS_DCONF(object);
    mms_settings_dconf_unwatch(dconf);
    if (dconf->changed_signal_id) {
        g_signal_handler_disconnect(dconf->client, dconf->changed_signal_id);
        dconf->changed_signal_id = 0;
    }
    G_OBJECT_CLASS(mms_settings_dconf_parent_class)->dispose(object);
}

static
void
mms_settings_dconf_finalize(
    GObject* object)
{
    MMSSettingsDconf* dconf = MMS_SETTINGS_DCONF(object);
    if (dconf->client) g_object_unref(dconf->client);
    mms_settings_sim_data_reset(&dconf->imsi_data);
    G_OBJECT_CLASS(mms_settings_dconf_parent_class)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
mms_settings_dconf_class_init(
    MMSSettingsClass* klass)
{
    klass->fn_get_sim_data = mms_settings_dconf_get_sim_data;
    G_OBJECT_CLASS(klass)->dispose = mms_settings_dconf_dispose;
    G_OBJECT_CLASS(klass)->finalize = mms_settings_dconf_finalize;
}

/**
 * Per instance initializer
 */
static
void
mms_settings_dconf_init(
    MMSSettingsDconf* dconf)
{
    dconf->client = dconf_client_new();
    if (dconf->client) {
        dconf->changed_signal_id = g_signal_connect(dconf->client,
            MMS_DCONF_CHANGED_SIGNAL, G_CALLBACK(mms_settings_dconf_changed),
            dconf);
    }
}

/**
 * Instantiates GSettings/Dconf settings implementation.
 */
MMSSettings*
mms_settings_dconf_new(
    const MMSConfig* config,
    const MMSSettingsSimData* defaults)
{
    MMSSettings* settings = g_object_new(MMS_TYPE_SETTINGS_DCONF, NULL);
    mms_settings_sim_data_copy(&settings->sim_defaults, defaults);
    settings->config = config;
    return settings;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
