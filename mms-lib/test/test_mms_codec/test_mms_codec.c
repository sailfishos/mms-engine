/*
 * Copyright (C) 2013-2020 Jolla Ltd.
 * Copyright (C) 2013-2020 Slava Monich <slava.monich@jolla.com>
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

#include "mms_lib_util.h"
#include "mms_lib_log.h"
#include "mms_codec.h"

#include <gutil_log.h>

#define DATA_DIR "data"

static TestOpt test_opt;

static
void
run_test(
    gconstpointer test_data)
{
    const char* file = test_data;
    GError* error = NULL;
    char* file2 = g_build_filename(DATA_DIR, file, NULL);
    const char* fname = g_file_test(file, G_FILE_TEST_EXISTS) ? file : file2;
    GMappedFile* map = g_mapped_file_new(fname, FALSE, &error);
    struct mms_message* msg = g_new0(struct mms_message, 1);
    const void* data = g_mapped_file_get_contents(map);
    const gsize length = g_mapped_file_get_length(map);

    g_assert(mms_message_decode(data, length, msg));
    g_mapped_file_unref(map);
    mms_message_free(msg);
    g_free(file2);
}

#define TEST_(x) "/MmsCodec/" x

int main(int argc, char* argv[])
{
    static const char* default_files[] = {
        "m-acknowledge.ind",
        "m-notification_1.ind",
        "m-notification_2.ind",
        "m-notification_3.ind",
        "m-notification_4.ind",
        "m-delivery_1.ind",
        "m-delivery_2.ind",
        "m-read-orig.ind",
        "m-retrieve_1.conf",
        "m-retrieve_2.conf",
        "m-retrieve_3.conf",
        "m-retrieve_4.conf",
        "m-retrieve_5.conf",
        "m-retrieve_6.conf",
        "m-retrieve_7.conf",
        "m-retrieve_8.conf",
        "m-retrieve_9.conf",
        "m-retrieve_10.conf",
        "m-notifyresp.ind",
        "m-read-rec.ind",
        "m-send_1.req",
        "m-send_2.req",
        "m-send_3.req",
        "m-send_1.conf",
        "m-send_2.conf",
        "m-send_3.conf"
    };

    int ret;

    g_test_init(&argc, &argv, NULL);
    test_init(&test_opt, &argc, argv);
    mms_lib_init(argv[0]);

    if (argc > 1) {
        int i;

        /* Take file names from the command line */
        for (i = 1; i < argc; i++) {
            const char* file = argv[i];
            char* test_name;

            G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
            test_name = g_strdup_printf(TEST_("%s"), g_basename(file));
            G_GNUC_END_IGNORE_DEPRECATIONS;

            g_test_add_data_func(test_name, file, run_test);
            g_free(test_name);
        }
    } else {
        guint i;

        /* Default set of test files */
        for (i = 0; i < G_N_ELEMENTS(default_files); i++) {
            const char* file = default_files[i];
            char* test_name = g_strdup_printf(TEST_("%s"), file);

            g_test_add_data_func(test_name, file, run_test);
            g_free(test_name);
        }
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
