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
 */

#include "mms_settings.h"
#include "mms_lib_util.h"
#include "mms_lib_log.h"

#include <gutil_log.h>

#define DATA_DIR "data/"

#define RET_OK   (0)
#define RET_ERR  (1)

typedef struct test_desc {
    const char* name;
    MMSConfig config;
    MMSSettingsSimData defaults;
} TestDesc;

#define DEFAULT_CONFIG \
    MMS_CONFIG_DEFAULT_ROOT_DIR, MMS_CONFIG_DEFAULT_RETRY_SECS, \
    MMS_CONFIG_DEFAULT_IDLE_SECS, FALSE, FALSE
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
          MMS_CONFIG_DEFAULT_IDLE_SECS, FALSE, FALSE },
        { DEFAULT_SETTINGS }
    },{
        "RetryDelay",
        { MMS_CONFIG_DEFAULT_ROOT_DIR, 111,
          MMS_CONFIG_DEFAULT_IDLE_SECS, FALSE, FALSE },
        { DEFAULT_SETTINGS }
    },{
        "IdleTimeout",
        { MMS_CONFIG_DEFAULT_ROOT_DIR, MMS_CONFIG_DEFAULT_RETRY_SECS,
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
gboolean
test_config_equal(
    const MMSConfig* c1,
    const MMSConfig* c2)
{
    return !g_strcmp0(c1->root_dir, c2->root_dir) &&
        c1->retry_secs == c2->retry_secs &&
        c1->idle_secs == c2->idle_secs &&
        c1->keep_temp_files == c2->keep_temp_files &&
        c1->attic_enabled == c2->attic_enabled;
}

static
gboolean
test_settings_equal(
    const MMSSettingsSimData* s1,
    const MMSSettingsSimData* s2)
{
    return !g_strcmp0(s1->user_agent, s2->user_agent) &&
        !g_strcmp0(s1->uaprof, s2->uaprof) &&
        s1->size_limit == s2->size_limit &&
        s1->max_pixels == s2->max_pixels &&
        s1->allow_dr == s2->allow_dr;
}

static
gboolean
test_run(
    const TestDesc* test)
{
    int ret = RET_ERR;
    GError* error = NULL;
    char* path = g_strconcat(DATA_DIR, test->name, ".conf", NULL);
    MMSConfigCopy global;
    MMSSettingsSimDataCopy defaults;

    memset(&global, 0, sizeof(global));
    memset(&defaults, 0, sizeof(defaults));
    mms_lib_default_config(&global.config);
    mms_settings_sim_data_default(&defaults.data);

    if (mms_settings_load_defaults(path, &global, &defaults, &error) &&
        test_settings_equal(&defaults.data, &test->defaults) &&
        test_config_equal(&global.config, &test->config)) {
        ret = RET_OK;
    }

    if (error) {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }

    g_free(path);
    g_free(global.root_dir);
    mms_settings_sim_data_reset(&defaults);
    GINFO("%s: %s", (ret == RET_OK) ? "OK" : "FAILED", test->name);
    return FALSE;
}

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    gboolean verbose = FALSE;
    GOptionContext* options;
    GOptionEntry entries[] = {
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { NULL }
    };

    mms_lib_init(argv[0]);
    options = g_option_context_new("[TESTS...] - MMS codec test");
    g_option_context_add_main_entries(options, entries, NULL);
    if (g_option_context_parse(options, &argc, &argv, NULL)) {
        int i;

        gutil_log_set_type(GLOG_TYPE_STDOUT, "test_mms_codec");
        if (verbose) {
            gutil_log_default.level = GLOG_LEVEL_VERBOSE;
        } else {
            gutil_log_timestamp = FALSE;
            gutil_log_default.level = GLOG_LEVEL_INFO;
            mms_codec_log.level = GLOG_LEVEL_ERR;
        }

        ret = RET_OK;
        if (argc > 1) {
            for (i=1; i<argc; i++) {
                int j;
                for (j=0; j<G_N_ELEMENTS(tests); j++) {
                    if (!g_strcmp0(argv[i], tests[i].name)) {
                        int ret2 = test_run(tests + i);
                        if (ret == RET_ERR && ret2 != RET_ERR) {
                            ret = ret2;
                        }
                        break;
                    }
                }
            }
        } else {
            /* Default set of tests */
            for (i=0; i<G_N_ELEMENTS(tests); i++) {
                int ret2 = test_run(tests + i);
                if (ret == RET_ERR && ret2 != RET_ERR) {
                    ret = ret2;
                }
            }
        }
    }
    g_option_context_free(options);
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
