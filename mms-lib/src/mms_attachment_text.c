/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2020 Open Mobile Platform LLC.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "mms_attachment.h"
#include "mms_settings.h"
#include "mms_codec.h"
#include "mms_file_util.h"

#include <gutil_strv.h>

/* Logging */
#define GLOG_MODULE_NAME mms_attachment_log
#include <gutil_log.h>

typedef struct mms_attachment_text {
    MMSAttachment attachment;
    char* utf8file;
} MMSAttachmentText;

typedef MMSAttachmentClass MMSAttachmentTextClass;
G_DEFINE_TYPE(MMSAttachmentText, mms_attachment_text, MMS_TYPE_ATTACHMENT)
#define MMS_ATTACHMENT_TEXT(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        MMS_TYPE_ATTACHMENT_TEXT, MMSAttachmentText))

static
void
mms_attachment_text_convert_to_utf8(
    MMSAttachment* at)
{
    MMSAttachmentText* self = MMS_ATTACHMENT_TEXT(at);
    char** ct = mms_parse_http_content_type(at->content_type);
    const int n = gutil_strv_length(ct);
    int i, cs_pos = 0;
    /* Find current charset */
    for (i = 1; i < n; i += 2) {
        if (!g_ascii_strcasecmp(ct[i], CONTENT_TYPE_PARAM_CHARSET)) {
            cs_pos = i;
            break;
        }
    }
    if (cs_pos > 0) {
        /* Check if it's already UTF-8 or US-ASCII */
        const char* charset = ct[cs_pos + 1];
        if (g_ascii_strcasecmp(charset, MMS_DEFAULT_CHARSET) &&
            g_ascii_strcasecmp(charset, "US=ASCII")) {
            /* Conversion is needed */
            GError* err = NULL;
            gsize utf8size;
            const char* in = at->original_file;
            const gchar* indata = g_mapped_file_get_contents(at->map);
            const gsize insize = g_mapped_file_get_length(at->map);
            gchar* utf8 = g_convert(indata, insize, MMS_DEFAULT_CHARSET,
                charset, NULL, &utf8size, &err);
            if (utf8) {
                char* out = mms_prepare_filename(in, MMS_CONVERT_DIR);
                if (g_file_set_contents(out, utf8, utf8size, &err)) {
                    GMappedFile* map = g_mapped_file_new(out, FALSE, &err);
                    if (map) {
                        GDEBUG("%s (%d bytes) -> %s (%d bytes)", in,
                            (int)insize, out, (int)utf8size);
                        /* Substitute file mapping */
                        g_mapped_file_unref(at->map);
                        at->file_name = self->utf8file = out;
                        at->map = map;
                        out = NULL;
                        /* Update content type header */
                        g_free(at->content_type);
                        g_free(ct[cs_pos + 1]);
                        ct[cs_pos + 1] = g_strdup(MMS_DEFAULT_CHARSET);
                        at->content_type = mms_unparse_http_content_type(ct);
                    } else {
                        GERR("Failed to map %s: %s", out, GERRMSG(err));
                        g_error_free(err);
                    }
                } else {
                    GERR("Failed to write %s: %s", out, GERRMSG(err));
                    g_error_free(err);
                }
                g_free(out);
                g_free(utf8);
            } else {
                GERR("Failed to convert %s: %s", in, GERRMSG(err));
                g_error_free(err);
            }
        } else {
            GDEBUG("%s: no conversion required", at->file_name);
        }
    }
    g_strfreev(ct);
}

static
gboolean
mms_attachment_text_init_attachment(
    MMSAttachment* at)
{
    if (at->config->convert_to_utf8) {
        mms_attachment_text_convert_to_utf8(at);
    } else {
        GDEBUG("%s: preserving charset", at->file_name);
    }
    return TRUE;
}

static
void
mms_attachment_text_finalize(
    GObject* object)
{
    MMSAttachmentText* self = MMS_ATTACHMENT_TEXT(object);
    if (!self->attachment.config->keep_temp_files) {
        mms_remove_file_and_dir(self->utf8file);
    }
    g_free(self->utf8file);
    G_OBJECT_CLASS(mms_attachment_text_parent_class)->finalize(object);
}

static
void
mms_attachment_text_class_init(
    MMSAttachmentTextClass* klass)
{
    klass->fn_init = mms_attachment_text_init_attachment;
    G_OBJECT_CLASS(klass)->finalize = mms_attachment_text_finalize;
}

static
void
mms_attachment_text_init(
    MMSAttachmentText* self)
{
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
