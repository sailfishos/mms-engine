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
 *
 */

#include "test_util.h"
#include "mms_file_util.h"
#include "mms_log.h"

void
test_dirs_init(
    TestDirs* dirs,
    const char* test)
{
    const char* tmp = g_get_tmp_dir();
    dirs->root = g_mkdtemp(g_strconcat(tmp, "/", test, "_XXXXXX", NULL));
    dirs->msg = g_strconcat(dirs->root, "/" MMS_MESSAGE_DIR, NULL);
    dirs->attic = g_strconcat(dirs->root, "/" MMS_ATTIC_DIR, NULL);
    MMS_VERBOSE("Temporary directory %s", dirs->root);
}

static
void
test_dir_remove(
    const char* dir)
{
    if (rmdir(dir) < 0) {
        if (errno != ENOENT) {
            MMS_ERR("Failed to remove %s: %s", dir, strerror(errno));
        }
    } else {
        MMS_VERBOSE("Deleted %s", dir);
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

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
