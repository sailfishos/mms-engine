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
 *
 */

#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include "mms_lib_types.h"

#define TEST_TIMEOUT_SEC (10)

typedef struct test_opt {
    int flags;
} TestOpt;

#define TEST_FLAG_DEBUG (0x01)

typedef struct test_dirs {
    char* root;
    char* msg;
    char* attic;
} TestDirs;

void
test_dirs_init(
    TestDirs* dirs,
    const char* test);

void
test_dirs_cleanup(
    TestDirs* dirs,
    gboolean remove);

/* Should be invoked after g_test_init */
void
test_init(
    TestOpt* opt,
    int* argc,
    char** argv);

/* Run main loop with a timeout */
void
test_run_loop(
    const TestOpt* opt,
    GMainLoop* loop);

#endif /* TEST_UTIL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
