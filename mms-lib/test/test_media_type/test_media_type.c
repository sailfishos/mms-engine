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

#include "mms_codec.h"

#include <gutil_log.h>

static TestOpt test_opt;

typedef struct test_desc {
    const char* name;
    const char* input;
    const char* output;
    char** parsed;
} TestDesc;

static const char* parsed_basic[] =
    { "text/html", "charset", "ISO-8859-4", NULL };
static const char* parsed_quotes[] =
    { "application/octet-stream", "foo", " quoted \"text\" ", NULL};
static const char* parsed_parameters[] =
    { "type/subtype", "p1", "v1", "p2", "v2", NULL };

static const TestDesc media_type_tests[] = {
    {
        "Basic",
        "text/html; charset=ISO-8859-4",
        "text/html; charset=ISO-8859-4",
        (char**)parsed_basic
    },{
        "Spaces",
        " text/html ;\tcharset = ISO-8859-4\n",
        "text/html; charset=ISO-8859-4",
        (char**)parsed_basic
    },{
        "Quotes",
        "application/octet-stream; foo = \"\\ quoted \\\"text\\\" \"",
        "application/octet-stream; foo=\" quoted \\\"text\\\" \"",
        (char**)parsed_quotes
    },{
        "Parameters",
        "type/subtype; p1=v1 ; p2=\"v2\"",
        "type/subtype; p1=v1; p2=v2",
        (char**)parsed_parameters
    },{
        "MissingSubtype",
        "type",
        NULL,
        NULL
    },{
        "MissingParameter",
        "type/subtype; ",
        NULL,
        NULL
    }
};

static
void
run_test(
    gconstpointer data)
{
    const TestDesc* test = data;
    char** parsed = mms_parse_http_content_type(test->input);

    if (parsed) {
        if (test->output) {
            char* unparsed = mms_unparse_http_content_type(parsed);
            char** p1 = parsed;
            char** p2 = test->parsed;

            g_assert(unparsed);
            g_assert_cmpstr(unparsed, == ,test->output);
            while (*p1) {
                g_assert(*p2);
                g_assert_cmpstr(*p1, == ,*p2);
                p1++;
                p2++;
            }
            g_free(unparsed);
        }
        g_strfreev(parsed);
    } else {
        g_assert(!test->output); /* Test is expected to fail */
    }
}

#define TEST_(x) "/MediaType/" x

int main(int argc, char* argv[])
{
    guint i;

    g_test_init(&argc, &argv, NULL);
    test_init(&test_opt, &argc, argv);
    for (i = 0; i < G_N_ELEMENTS(media_type_tests); i++) {
        const TestDesc* test = media_type_tests + i;
        char* name = g_strdup_printf(TEST_("%s"), test->name);

        g_test_add_data_func(name, test, run_test);
        g_free(name);
    }
    return g_test_run();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
