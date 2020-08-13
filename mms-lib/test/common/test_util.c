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
#include "mms_file_util.h"

#include <gutil_log.h>

void
test_dirs_init(
    TestDirs* dirs,
    const char* test)
{
    const char* tmp = g_get_tmp_dir();
    dirs->root = g_mkdtemp(g_strconcat(tmp, "/", test, "_XXXXXX", NULL));
    dirs->msg = g_strconcat(dirs->root, "/" MMS_MESSAGE_DIR, NULL);
    dirs->attic = g_strconcat(dirs->root, "/" MMS_ATTIC_DIR, NULL);
    GVERBOSE("Temporary directory %s", dirs->root);
}

static
void
test_dir_remove(
    const char* dir)
{
    if (rmdir(dir) < 0) {
        if (errno != ENOENT) {
            GERR("Failed to remove %s: %s", dir, strerror(errno));
        }
    } else {
        GVERBOSE("Deleted %s", dir);
    }
}

void
test_dirs_cleanup(
    TestDirs* dirs,
    gboolean remove)
{
    if (remove) {
        test_dir_remove(dirs->attic);
        test_dir_remove(dirs->msg);
        test_dir_remove(dirs->root);
    }
    g_free(dirs->root);
    g_free(dirs->msg);
    g_free(dirs->attic);
}

/* Should be invoked after g_test_init */
void
test_init(
    TestOpt* opt,
    int* argc,
    char** argv)
{
    int i;

    memset(opt, 0, sizeof(*opt));
    for (i = 1; i < (*argc); i++) {
        const char* arg = argv[i];

        if (!strcmp(arg, "-d") || !strcmp(arg, "--debug")) {
            opt->flags |= TEST_FLAG_DEBUG;
        } else if (!strcmp(arg, "-v")) {
            GTestConfig* config = (GTestConfig*)g_test_config_vars;
            config->test_verbose = TRUE;
        } else {
            /* Something that we don't recognize */
            continue;
        }
        /* Remove consumed option from the command line */
        (*argc)--;
        if (i < (*argc)) {
            memmove(argv + i, argv + i + 1, sizeof(char*) * ((*argc) - i));
        }
        i--;
    }

    /* Setup logging */
    gutil_log_timestamp = FALSE;
    gutil_log_default.level = g_test_verbose() ? GLOG_LEVEL_VERBOSE :
            GLOG_LEVEL_NONE;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
