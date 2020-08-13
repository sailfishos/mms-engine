/*
 * Copyright (C) 2016-2020 Jolla Ltd.
 * Copyright (C) 2016-2020 Slava Monich <slava.monich@jolla.com>
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

#include "test_util.h"

#include "mms_settings.h"
#include "mms_lib_util.h"
#include "mms_lib_log.h"

#include <gutil_log.h>

static TestOpt test_opt;

#define DATA_DIR "data/"

typedef struct test_desc {
    const char* name;
    MMSConfig config;
    MMSSettingsSimData defaults;
} TestDesc;

#define DEFAULT_CONFIG \
    MMS_CONFIG_DEFAULT_ROOT_DIR, MMS_CONFIG_DEFAULT_RETRY_SECS, \
    MMS_CONFIG_DEFAULT_NETWORK_IDLE_SECS, MMS_CONFIG_DEFAULT_IDLE_SECS, \
    FALSE, FALSE
#define DEFAULT_SETTINGS \
    MMS_SETTINGS_DEFAULT_USER_AGENT, MMS_SETTINGS_DEFAULT_UAPROF, \
    MMS_SETTINGS_DEFAULT_SIZE_LIMIT, MMS_SETTINGS_DEFAULT_MAX_PIXELS, \
    MMS_SETTINGS_DEFAULT_ALLOW_DR

static const TestDesc tests [] = {
    {
        "Empty",
        { DEFAULT_CONFIG },
        { DEFAULT_SETTINGS }
    },{
        "InvalidConfig",
        { DEFAULT_CONFIG },
        { DEFAULT_SETTINGS }
    },{
        "RootDir",
        { "TestRootDir", MMS_CONFIG_DEFAULT_RETRY_SECS,
          MMS_CONFIG_DEFAULT_NETWORK_IDLE_SECS,
          MMS_CONFIG_DEFAULT_IDLE_SECS, FALSE, FALSE },
        { DEFAULT_SETTINGS }
    },{
        "RetryDelay",
        { MMS_CONFIG_DEFAULT_ROOT_DIR, 111,
          MMS_CONFIG_DEFAULT_NETWORK_IDLE_SECS,
          MMS_CONFIG_DEFAULT_IDLE_SECS, FALSE, FALSE },
        { DEFAULT_SETTINGS }
    },{
        "NetworkIdleTimeout",
        { MMS_CONFIG_DEFAULT_ROOT_DIR, MMS_CONFIG_DEFAULT_RETRY_SECS,
          111, MMS_CONFIG_DEFAULT_IDLE_SECS, FALSE, FALSE },
        { DEFAULT_SETTINGS }
    },{
        "IdleTimeout",
        { MMS_CONFIG_DEFAULT_ROOT_DIR, MMS_CONFIG_DEFAULT_RETRY_SECS,
          MMS_CONFIG_DEFAULT_NETWORK_IDLE_SECS,
          222, FALSE, FALSE },
        { DEFAULT_SETTINGS }
    },{
        "UserAgent",
        { DEFAULT_CONFIG },
        { "TestUserAgent", MMS_SETTINGS_DEFAULT_UAPROF,
          MMS_SETTINGS_DEFAULT_SIZE_LIMIT, MMS_SETTINGS_DEFAULT_MAX_PIXELS,
          MMS_SETTINGS_DEFAULT_ALLOW_DR }
    },{
        "UAProfile",
        { DEFAULT_CONFIG },
        { MMS_SETTINGS_DEFAULT_USER_AGENT, "TestUAProfile",
          MMS_SETTINGS_DEFAULT_SIZE_LIMIT, MMS_SETTINGS_DEFAULT_MAX_PIXELS,
          MMS_SETTINGS_DEFAULT_ALLOW_DR }
    },{
        "SizeLimit",
        { DEFAULT_CONFIG },
        { MMS_SETTINGS_DEFAULT_USER_AGENT, MMS_SETTINGS_DEFAULT_UAPROF,
          100000, MMS_SETTINGS_DEFAULT_MAX_PIXELS,
          MMS_SETTINGS_DEFAULT_ALLOW_DR }
    },{
        "MaxPixels",
        { DEFAULT_CONFIG },
        { MMS_SETTINGS_DEFAULT_USER_AGENT, MMS_SETTINGS_DEFAULT_UAPROF,
          MMS_SETTINGS_DEFAULT_SIZE_LIMIT, 1000000,
          MMS_SETTINGS_DEFAULT_ALLOW_DR }
    },{
        "SendDeliveryReport",
        { DEFAULT_CONFIG },
        { MMS_SETTINGS_DEFAULT_USER_AGENT, MMS_SETTINGS_DEFAULT_UAPROF,
          MMS_SETTINGS_DEFAULT_SIZE_LIMIT, MMS_SETTINGS_DEFAULT_MAX_PIXELS,
          FALSE }
    }
};

static
void
check_config(
    const MMSConfig* c1,
    const MMSConfig* c2)
{
    g_assert_cmpstr(c1->root_dir, == ,c2->root_dir);
    g_assert_cmpint(c1->retry_secs, == ,c2->retry_secs);
    g_assert_cmpint(c1->network_idle_secs, == ,c2->network_idle_secs);
    g_assert_cmpint(c1->idle_secs, == ,c2->idle_secs);
    g_assert(c1->keep_temp_files == c2->keep_temp_files);
    g_assert(c1->attic_enabled == c2->attic_enabled);
}

static
void
check_settings(
    const MMSSettingsSimData* s1,
    const MMSSettingsSimData* s2)
{
    g_assert_cmpstr(s1->user_agent, == ,s2->user_agent);
    g_assert_cmpstr(s1->uaprof, == ,s2->uaprof);
    g_assert_cmpuint(s1->size_limit, == ,s2->size_limit);
    g_assert_cmpuint(s1->max_pixels, == ,s2->max_pixels);
    g_assert(s1->allow_dr == s2->allow_dr);
}

static
void
run_test(
    gconstpointer data)
{
    const TestDesc* test = data;
    GError* error = NULL;
    char* path = g_strconcat(DATA_DIR, test->name, ".conf", NULL);
    MMSConfigCopy global;
    MMSSettingsSimDataCopy defaults;

    memset(&global, 0, sizeof(global));
    memset(&defaults, 0, sizeof(defaults));
    mms_lib_default_config(&global.config);
    mms_settings_sim_data_default(&defaults.data);

    g_assert(mms_settings_load_defaults(path, &global, &defaults, &error));
    check_settings(&defaults.data, &test->defaults);
    check_config(&global.config, &test->config);

    g_free(path);
    g_free(global.root_dir);
    mms_settings_sim_data_reset(&defaults);
}

#define TEST_(x) "/MediaType/" x

int main(int argc, char* argv[])
{
    guint i;
    int ret;

    mms_lib_init(argv[0]);
    g_test_init(&argc, &argv, NULL);
    test_init(&test_opt, &argc, argv);
    for (i = 0; i < G_N_ELEMENTS(tests); i++) {
        const TestDesc* test = tests + i;
        char* name = g_strdup_printf(TEST_("%s"), test->name);

        g_test_add_data_func(name, test, run_test);
        g_free(name);
    }
    ret = g_test_run();
    mms_lib_deinit();
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
