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

#include <syslog.h>
#include <glib-unix.h>

#include "mms_engine.h"
#include "mms_lib_log.h"
#include "mms_lib_util.h"
#include "mms_settings.h"

#include "mms_transfer_list_dbus.h"

#ifdef SAILFISH
#  include "mms_connman_nemo_log.h"
#else
#  include "mms_connman_ofono_log.h"
#endif

#include <dbusaccess_policy.h>
#include <gutil_log.h>

#define RET_OK  (0)
#define RET_ERR (1)

typedef struct mms_app_dbus_policy {
    const char* spec;
    const DA_ACTION* actions;
} MMSAppDBusPolicy;

#define RADIO_USER "radio"              /* ofono */
#define RADIO_GROUP "radio"

#define PRIVILEGED_GROUP "privileged"   /* commhistoryd */
#define MMS_GROUP "sailfish-mms"

/* org.nemomobile.MmsEngine */
#define MMS_ENGINE_DBUS_METHOD_CANCEL           "cancel"
#define MMS_ENGINE_DBUS_METHOD_RECEIVE_MESSAGE  "receiveMessage"
#define MMS_ENGINE_DBUS_METHOD_SEND_READ_REPORT "sendReadReport"
#define MMS_ENGINE_DBUS_METHOD_SEND_MESSAGE     "sendMessage"
#define MMS_ENGINE_DBUS_METHOD_PUSH             "push"
#define MMS_ENGINE_DBUS_METHOD_PUSH_NOTIFY      "pushNotify"
#define MMS_ENGINE_DBUS_METHOD_SET_LOG_LEVEL    "setLogLevel"
#define MMS_ENGINE_DBUS_METHOD_SET_LOG_TYPE     "setLogType"
#define MMS_ENGINE_DBUS_METHOD_GET_VERSION      "getVersion"
#define MMS_ENGINE_DBUS_METHOD_MIGRATE_SETTINGS "migrateSettings"

static const DA_ACTION mms_engine_dbus_actions[] = {
    #define INIT_DA_ACTION(id) \
        {MMS_ENGINE_DBUS_METHOD_##id, MMS_ENGINE_ACTION_##id, 0},
    MMS_ENGINE_DBUS_METHODS(INIT_DA_ACTION)
    #undef INIT_DA_ACTION
    { NULL }
};

static const MMSAppDBusPolicy mms_engine_default_dbus_policy = {
    "((!group("PRIVILEGED_GROUP"))&(!group("MMS_GROUP"))&("
    MMS_ENGINE_DBUS_METHOD_CANCEL "()|"
    MMS_ENGINE_DBUS_METHOD_RECEIVE_MESSAGE "()|"
    MMS_ENGINE_DBUS_METHOD_SEND_READ_REPORT"()|"
    MMS_ENGINE_DBUS_METHOD_SEND_MESSAGE"()|"
    MMS_ENGINE_DBUS_METHOD_SET_LOG_LEVEL"()|"
    MMS_ENGINE_DBUS_METHOD_SET_LOG_TYPE"()|"
    MMS_ENGINE_DBUS_METHOD_MIGRATE_SETTINGS"()))|"
    "((!(user("RADIO_USER")&group("RADIO_GROUP")))&("
    MMS_ENGINE_DBUS_METHOD_PUSH"()|"
    MMS_ENGINE_DBUS_METHOD_PUSH_NOTIFY "()))=deny",
    mms_engine_dbus_actions
};

/* org.nemomobile.MmsEngine.TransferList */
#define MMS_TRANSFER_LIST_DBUS_METHOD_GET  "Get"

static const DA_ACTION mms_tx_list_dbus_actions[] = {
    #define INIT_DA_ACTION(id) \
        {MMS_TRANSFER_LIST_DBUS_METHOD_##id, \
            MMS_TRANSFER_LIST_ACTION_##id, 0},
    MMS_TRANSFER_LIST_DBUS_METHODS(INIT_DA_ACTION)
    #undef INIT_DA_ACTION
    { NULL }
};

static const MMSAppDBusPolicy mms_tx_list_default_dbus_policy = {
    "(!group("PRIVILEGED_GROUP"))&(!group("MMS_GROUP"))&"
    MMS_TRANSFER_LIST_DBUS_METHOD_GET "()=deny",
    mms_tx_list_dbus_actions
};

/* org.nemomobile.MmsEngine.Transfer */
#define MMS_TRANSFER_DBUS_METHOD_GET_ALL                "GetAll"
#define MMS_TRANSFER_DBUS_METHOD_ENABLE_UPDATES         "EnableUpdates"
#define MMS_TRANSFER_DBUS_METHOD_DISABLE_UPDATES        "DisableUpdates"
#define MMS_TRANSFER_DBUS_METHOD_GET_INTERFACE_VERSION  "GetInterfaceVersion"
#define MMS_TRANSFER_DBUS_METHOD_GET_SEND_PROGRESS      "GetSendProgress"
#define MMS_TRANSFER_DBUS_METHOD_GET_RECEIVE_PROGRESS   "GetReceiveProgress"

static const DA_ACTION mms_tx_dbus_actions[] = {
    #define INIT_DA_ACTION(id) \
        {MMS_TRANSFER_DBUS_METHOD_##id, MMS_TRANSFER_ACTION_##id, 0},
    MMS_TRANSFER_DBUS_METHODS(INIT_DA_ACTION)
    #undef INIT_DA_ACTION
    { NULL }
};

static const MMSAppDBusPolicy mms_tx_default_dbus_policy = {
    "(!group("PRIVILEGED_GROUP"))&(!group("MMS_GROUP"))&("
    MMS_TRANSFER_DBUS_METHOD_GET_ALL "()|"
    MMS_TRANSFER_DBUS_METHOD_ENABLE_UPDATES"()|"
    MMS_TRANSFER_DBUS_METHOD_DISABLE_UPDATES"()|"
    MMS_TRANSFER_DBUS_METHOD_GET_SEND_PROGRESS"()|"
    MMS_TRANSFER_DBUS_METHOD_GET_RECEIVE_PROGRESS"())=deny",
    mms_tx_dbus_actions
};

/* Config groups and keys */
static const char SETTINGS_DBUS_GROUP[] = "DBus";
static const char SETTINGS_DBUS_TYPE[] = "Bus";
static const char SETTINGS_DBUS_ENGINE_ACCESS[] = "MmsEngineAccess";
static const char SETTINGS_DBUS_TRANSFER_ACCESS[] = "TransferAccess";
static const char SETTINGS_DBUS_TRANSFER_LIST_ACCESS[] = "TransferListAccess";

/* Options configurable from the command line */
typedef struct mms_app_options {
    int flags;
    MMSConfigCopy global;
    MMSSettingsSimDataCopy settings;
    MMSEngineDbusConfig dbus;
} MMSAppOptions;

/* All known log modules */
static MMSLogModule* mms_app_log_modules[] = {
    &gutil_log_default,
#define MMS_LIB_LOG_MODULE(m) &(m),
    MMS_LIB_LOG_MODULES(MMS_LIB_LOG_MODULE)
    MMS_CONNMAN_LOG_MODULES(MMS_LIB_LOG_MODULE)
#undef MMS_LIB_LOG_MODULE
    NULL
};

/* Signal handler */
static
gboolean
mms_app_signal(
    gpointer arg)
{
    MMSEngine* engine = arg;
    GINFO("Caught signal, shutting down...");
    mms_engine_stop(engine);
    return TRUE;
}

/* D-Bus event handlers */
static
void
mms_app_bus_acquired(
    GDBusConnection* bus,
    const gchar* name,
    gpointer arg)
{
    MMSEngine* engine = arg;
    GError* error = NULL;
    GDEBUG("Bus acquired, starting...");
    if (!mms_engine_register(engine, bus, &error)) {
        GERR("Could not start: %s", GERRMSG(error));
        g_error_free(error);
        mms_engine_stop(engine);
    }
}

static
void
mms_app_name_acquired(
    GDBusConnection* bus,
    const gchar* name,
    gpointer arg)
{
    GDEBUG("Acquired service name '%s'", name);
}

static
void
mms_app_name_lost(
    GDBusConnection* bus,
    const gchar* name,
    gpointer arg)
{
    MMSEngine* engine = arg;
    GERR("'%s' service already running or access denied", name);
    mms_engine_stop(engine);
}

/* Option parsing callbacks */
static
gboolean
mms_app_option_loglevel(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    int count = 0;
    MMSLogModule** ptr = mms_app_log_modules;
    while (*ptr++) count++;
    return gutil_log_parse_option(value, mms_app_log_modules, count, error);
}

static
gboolean
mms_app_option_logtype(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    if (gutil_log_set_type(value, MMS_APP_LOG_PREFIX)) {
        return TRUE;
    } else {
        if (error) {
            *error = g_error_new(G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                "Invalid log type \'%s\'", value);
        }
        return FALSE;
    }
}

static
gboolean
mms_app_option_verbose(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    gutil_log_default.level = GLOG_LEVEL_VERBOSE;
    return TRUE;
}

/* Load settings from config file */

static
DAPolicy*
mms_app_dbus_policy_new(
    const MMSAppDBusPolicy* policy)
{
    if (policy) {
        DAPolicy* access = da_policy_new_full(policy->spec, policy->actions);

        if (access) {
            return access;
        }
        GWARN("Invalid D-Bus policy \"%s\"", policy->spec);
    }
    return NULL;
}

static
DAPolicy*
mms_app_dbus_config_update(
    DAPolicy* current_policy,
    GKeyFile* file,
    const char* group,
    const char* key,
    const MMSAppDBusPolicy* default_policy)
{
    char* value = g_key_file_get_string(file, group, key, NULL);
    DAPolicy* policy = NULL;

    if (value) {
        policy = da_policy_new_full(value, default_policy->actions);
        if (policy) {
            GDEBUG("Using %s policy \"%s\"", key, value);
        } else {
            GWARN("Invalid %s policy \"%s\"", key, value);
        }
        g_free(value);
    }
    if (policy) {
        da_policy_unref(current_policy);
        return policy;
    } else if (!current_policy) {
        return mms_app_dbus_policy_new(default_policy);
    } else {
        return current_policy;
    }
}

static
void
mms_app_dbus_config_init(
    MMSEngineDbusConfig* dbus)
{
    dbus->type = G_BUS_TYPE_SYSTEM;
    dbus->engine_access =
        mms_app_dbus_policy_new(&mms_engine_default_dbus_policy);
    dbus->tx_list_access =
        mms_app_dbus_policy_new(&mms_tx_list_default_dbus_policy);
    dbus->tx_access =
        mms_app_dbus_policy_new(&mms_tx_default_dbus_policy);
}

static
void
mms_app_dbus_config_clear(
    MMSEngineDbusConfig* dbus)
{
    da_policy_unref(dbus->engine_access);
    da_policy_unref(dbus->tx_list_access);
    da_policy_unref(dbus->tx_access);
}

static
void
mms_app_dbus_config_parse(
    GKeyFile* file,
    MMSEngineDbusConfig* dbus)
{
    const char* group = SETTINGS_DBUS_GROUP;
    char* type = g_key_file_get_string(file, group, SETTINGS_DBUS_TYPE, NULL);

    if (type) {
        static const char SYSTEM_BUS[] = "system";
        static const char SESSION_BUS[] = "session";

        if (!g_strcmp0(type, SYSTEM_BUS)) {
            dbus->type = G_BUS_TYPE_SYSTEM;
        } else if (!g_strcmp0(type, SESSION_BUS)) {
            dbus->type = G_BUS_TYPE_SESSION;
        } else {
            GWARN("Invalid D-Bys type \"%s\"", type);
        }
        g_free(type);
    }
    dbus->engine_access = mms_app_dbus_config_update(dbus->engine_access,
        file, group, SETTINGS_DBUS_ENGINE_ACCESS,
        &mms_engine_default_dbus_policy);
    dbus->tx_list_access = mms_app_dbus_config_update(dbus->tx_list_access,
        file, group, SETTINGS_DBUS_TRANSFER_LIST_ACCESS,
        &mms_tx_list_default_dbus_policy);
    dbus->tx_access = mms_app_dbus_config_update(dbus->tx_access,
        file, group, SETTINGS_DBUS_TRANSFER_ACCESS,
        &mms_tx_default_dbus_policy);
}

static
gboolean
mms_app_config_load(
    const char* config_file,
    MMSAppOptions* opt,
    GError** error)
{
    GKeyFile* file = g_key_file_new();
    gboolean ok = g_key_file_load_from_file(file, config_file, 0, error);

    if (ok) {
        GDEBUG("Loading %s", config_file);
        mms_settings_parse(file, &opt->global, &opt->settings);
        mms_app_dbus_config_parse(file, &opt->dbus);
    }
    g_key_file_free(file);
    return ok;
}

/**
 * Parses command line and sets up application options. Returns TRUE if
 * we should go ahead and run the application, FALSE if we should exit
 * immediately.
 */
static
gboolean
mms_app_parse_options(
    MMSAppOptions* opt,
    int argc,
    char* argv[],
    int* result)
{
    gboolean ok;
    GError* error = NULL;
    gboolean session_bus = FALSE;
    char* config_file = NULL;
#ifdef MMS_VERSION_STRING
    gboolean print_version = FALSE;
#endif
    char* ua = NULL;
    char* uaprof = NULL;
    char* root_dir = NULL;
    gboolean log_modules = FALSE;
    gboolean keep_running = FALSE;
    gboolean disable_dbus_log = FALSE;
    gint size_limit_kb = -1;
    gdouble megapixels = -1;
    char* root_dir_help = g_strdup_printf(
        "Root directory for MMS files [%s]",
        opt->global.config.root_dir);
    char* retry_secs_help = g_strdup_printf(
        "Retry period in seconds [%d]",
        opt->global.config.retry_secs);
    char* network_idle_secs_help = g_strdup_printf(
        "Network inactivity timeout in seconds [%d]",
        opt->global.config.network_idle_secs);
    char* idle_secs_help = g_strdup_printf(
        "Service inactivity timeout in seconds [%d]",
        opt->global.config.idle_secs);
    char* description = gutil_log_description(NULL, 0);

    GOptionContext* options;
    GOptionEntry entries[] = {
        { "session", 0, 0, G_OPTION_ARG_NONE, &session_bus,
          "Use session bus (default is system)", NULL },
#define OPT_CONFIG_INDEX 1
        { "config", 'c', 0, G_OPTION_ARG_FILENAME,
          &config_file, "Use the specified config file ["
          MMS_ENGINE_CONFIG_FILE "]", "FILE" },
        { "root-dir", 'd', 0, G_OPTION_ARG_FILENAME,
          &root_dir, root_dir_help, "DIR" },
        { "retry-secs", 'r', 0, G_OPTION_ARG_INT,
          &opt->global.config.retry_secs, retry_secs_help, "SEC" },
        { "network-idle-secs", 'n', 0, G_OPTION_ARG_INT,
          &opt->global.config.network_idle_secs,
          network_idle_secs_help, "SEC" },
        { "idle-secs", 'i', 0, G_OPTION_ARG_INT,
          &opt->global.config.idle_secs, idle_secs_help, "SEC" },
        { "size-limit", 's', 0, G_OPTION_ARG_INT,
          &size_limit_kb, "Maximum size for outgoing messages", "KB" },
        { "pix-limit", 'p', 0, G_OPTION_ARG_DOUBLE,
          &megapixels, "Maximum pixel count for outgoing images", "MPIX" },
        { "user-agent", 'u', 0, G_OPTION_ARG_STRING,
          &ua, "The value of the User-Agent header", "STRING" },
        { "x-wap-profile", 'x', 0, G_OPTION_ARG_STRING,
          &uaprof, "User agent profile", "URL" },
        { "keep-running", 'k', 0, G_OPTION_ARG_NONE, &keep_running,
          "Keep running after everything is done", NULL },
        { "keep-temp-files", 't', 0, G_OPTION_ARG_NONE,
           &opt->global.config.keep_temp_files,
          "Don't delete temporary files", NULL },
        { "attic", 'a', 0, G_OPTION_ARG_NONE,
          &opt->global.config.attic_enabled,
          "Store unrecognized push messages in the attic", NULL },
#define OPT_VERBOSE_INDEX 13
        { "verbose", 'v', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          mms_app_option_verbose, "Be verbose (equivalent to -l=verbose)",
          NULL },
        { "log-output", 'o', 0, G_OPTION_ARG_CALLBACK, mms_app_option_logtype,
          "Log output (stdout|syslog|glib) [stdout]", "TYPE" },
        { "log-level", 'l', 0, G_OPTION_ARG_CALLBACK, mms_app_option_loglevel,
          "Set log level (repeatable)", "[MODULE:]LEVEL" },
        { "disable-dbus-log", 'D', 0, G_OPTION_ARG_NONE, &disable_dbus_log,
          "Disable logging over D-Bus", NULL },
        { "log-modules", 0, 0, G_OPTION_ARG_NONE, &log_modules,
          "List available log modules", NULL },
#ifdef MMS_VERSION_STRING
        { "version", 0, 0, G_OPTION_ARG_NONE, &print_version,
          "Print program version and exit", NULL },
#endif
        { NULL }
    };

    /*
     * First pre-parse the command line to get the config file name.
     * Only then we can initialize the defaults and parse the rest.
     * Verbose option is handled here too, in order to allow verbose
     * logging during config file parsing.
     */
    GOptionContext* config_options;
    GOptionEntry config_entries[3];
    memset(config_entries, 0, sizeof(config_entries));
    config_entries[0] = entries[OPT_VERBOSE_INDEX];
    config_entries[1] = entries[OPT_CONFIG_INDEX];

    /*
     * Initialize the main parsing context. We would still need it
     * even if the preparsing fails - to print the help
     */
    options = g_option_context_new("- part of Jolla MMS system");
    g_option_context_add_main_entries(options, entries, NULL);
    g_option_context_set_description(options, description);

    /* Pre-parsing context */
    config_options = g_option_context_new(NULL);
    g_option_context_add_main_entries(config_options, config_entries, NULL);
    g_option_context_set_help_enabled(config_options, FALSE);
    g_option_context_set_ignore_unknown_options(config_options, TRUE);
    ok = g_option_context_parse(config_options, &argc, &argv, &error);

    /*
     * If pre-parsing succeeds, we need to read the config before
     * parsing the rest of the command line, to allow command line
     * options to overwrite those specified in the config file.
     */
    if (ok) {
        if (config_file) {
            /* Config file was specified on the command line */
            ok = mms_app_config_load(config_file, opt, &error);
        } else {
            /* The default config file may be (and usually is) missing */
            if (g_file_test(MMS_ENGINE_CONFIG_FILE, G_FILE_TEST_EXISTS)) {
                mms_app_config_load(MMS_ENGINE_CONFIG_FILE, opt, NULL);
            }
        }
        if (ok) {
            /* Parse the rest of the command line */
            session_bus = (opt->dbus.type == G_BUS_TYPE_SESSION);
            ok = g_option_context_parse(options, &argc, &argv, &error);
        } else if (error) {
            /* Improve error message by prepending the file name */
            GError* details = g_error_new(error->domain, error->code,
                "%s: %s", config_file, error->message);
            g_error_free(error);
            error = details;
        }
    }

    g_option_context_free(options);
    g_option_context_free(config_options);
    g_free(config_file);
    g_free(root_dir_help);
    g_free(retry_secs_help);
    g_free(idle_secs_help);
    g_free(network_idle_secs_help);
    g_free(description);

    if (!ok) {
        fprintf(stderr, "%s\n", GERRMSG(error));
        g_error_free(error);
        *result = RET_ERR;
    } else if (log_modules) {
        MMSLogModule** ptr = mms_app_log_modules;

        while (*ptr) {
            MMSLogModule* log = *ptr++;

            printf("%s\n", log->name);
        }
        *result = RET_OK;
        ok = FALSE;
#ifdef MMS_VERSION_STRING
    } else if (print_version) {
        printf("MMS engine %s\n", MMS_VERSION_STRING);
        *result = RET_OK;
        ok = FALSE;
#endif
    } else {
#ifdef MMS_VERSION_STRING
        GINFO("Version %s starting", MMS_VERSION_STRING);
#else
        GINFO("Starting");
#endif
        if (size_limit_kb >= 0) {
            opt->settings.data.size_limit = size_limit_kb * 1024;
            opt->flags |= MMS_ENGINE_FLAG_OVERRIDE_SIZE_LIMIT;
        }
        if (megapixels >= 0) {
            opt->settings.data.max_pixels = (int)(megapixels*1000)*1000;
            opt->flags |= MMS_ENGINE_FLAG_OVERRIDE_MAX_PIXELS;
        }
        if (ua) {
            g_free(opt->settings.user_agent);
            opt->settings.data.user_agent = opt->settings.user_agent = ua;
            opt->flags |= MMS_ENGINE_FLAG_OVERRIDE_USER_AGENT;
            ua = NULL;
        }
        if (uaprof) {
            g_free(opt->settings.uaprof);
            opt->settings.data.uaprof = opt->settings.uaprof = uaprof;
            opt->flags |= MMS_ENGINE_FLAG_OVERRIDE_UAPROF;
            uaprof = NULL;
        }
        if (root_dir) {
            g_free(opt->global.root_dir);
            opt->global.config.root_dir = opt->global.root_dir = root_dir;
            root_dir = NULL;
        }
        if (keep_running) opt->flags |= MMS_ENGINE_FLAG_KEEP_RUNNING;
        if (disable_dbus_log) opt->flags |= MMS_ENGINE_FLAG_DISABLE_DBUS_LOG;
        if (session_bus) {
            GDEBUG("Attaching to session bus");
            opt->dbus.type = G_BUS_TYPE_SESSION;
        } else {
            GDEBUG("Attaching to system bus");
            opt->dbus.type = G_BUS_TYPE_SYSTEM;
        }
        *result = RET_OK;
    }

    g_free(ua);
    g_free(uaprof);
    g_free(root_dir);
    return ok;
}

int main(int argc, char* argv[])
{
    int result = RET_ERR;
    MMSAppOptions opt;
    MMSConfig* config = &opt.global.config;
    MMSSettingsSimData* settings = &opt.settings.data;

    mms_lib_init(argv[0]);
    gofono_log.name = "mms-ofono";
    gutil_log_default.name = MMS_APP_LOG_PREFIX;
    memset(&opt, 0, sizeof(opt));
    mms_lib_default_config(config);
    mms_settings_sim_data_default(settings);
    mms_app_dbus_config_init(&opt.dbus);
    if (mms_app_parse_options(&opt, argc, argv, &result)) {
        MMSEngine* engine;

        /* Create engine instance. This may fail */
        engine = mms_engine_new(config, settings, &opt.dbus,
            mms_app_log_modules, opt.flags);
        if (engine) {

            /* Setup main loop */
            GMainLoop* loop = g_main_loop_new(NULL, FALSE);
            guint sigtrm = g_unix_signal_add(SIGTERM, mms_app_signal, engine);
            guint sigint = g_unix_signal_add(SIGINT, mms_app_signal, engine);

            /* Acquire name, don't allow replacement */
            guint name_id = g_bus_own_name(opt.dbus.type, MMS_ENGINE_SERVICE,
                G_BUS_NAME_OWNER_FLAGS_REPLACE, mms_app_bus_acquired,
                mms_app_name_acquired, mms_app_name_lost, engine, NULL);

            /* Run the main loop */
            mms_engine_run(engine, loop);

            /* Cleanup and exit */
            if (sigtrm) g_source_remove(sigtrm);
            if (sigint) g_source_remove(sigint);
            g_bus_unown_name(name_id);
            g_main_loop_unref(loop);
            mms_engine_unref(engine);
        }
        GINFO("Exiting");
    }
    if (gutil_log_func == gutil_log_syslog) {
        closelog();
    }
    g_free(opt.global.root_dir);
    mms_settings_sim_data_reset(&opt.settings);
    mms_app_dbus_config_clear(&opt.dbus);
    mms_lib_deinit();
    return result;
}
