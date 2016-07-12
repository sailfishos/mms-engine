/*
 * Copyright (C) 2013-2016 Jolla Ltd.
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

#include "mms_attachment.h"
#include "mms_file_util.h"
#include "mms_settings.h"
#include "mms_codec.h"

#ifdef HAVE_MAGIC
#  include <magic.h>
#endif

/* Logging */
#define GLOG_MODULE_NAME mms_attachment_log
#include "mms_error.h"
GLOG_MODULE_DEFINE("mms-attachment");

#define MMS_ATTACHMENT_DEFAULT_TYPE "application/octet-stream"
#define CONTENT_TYPE_PARAM_NAME     "name"

G_DEFINE_TYPE(MMSAttachment, mms_attachment, G_TYPE_OBJECT)

#define MMS_ATTACHMENT(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), MMS_TYPE_ATTACHMENT, MMSAttachment))
#define MMS_ATTACHMENT_GET_CLASS(obj)  \
    (G_TYPE_INSTANCE_GET_CLASS((obj), MMS_TYPE_ATTACHMENT, MMSAttachmentClass))

#define REGION_TEXT     "Text"
#define REGION_MEDIA    "Media"

#define MEDIA_TEXT      "text"
#define MEDIA_IMAGE     "img"
#define MEDIA_VIDEO     "video"
#define MEDIA_AUDIO     "audio"
#define MEDIA_OTHER     "ref"

#define MEDIA_TYPE_TEXT_PREFIX  "text/"
#define MEDIA_TYPE_IMAGE_PREFIX "image/"
#define MEDIA_TYPE_VIDEO_PREFIX "video/"
#define MEDIA_TYPE_AUDIO_PREFIX "audio/"

static
void
mms_attachment_finalize(
    GObject* object)
{
    MMSAttachment* at = MMS_ATTACHMENT(object);
    GVERBOSE_("%p", at);
    if (at->map) g_mapped_file_unref(at->map);
    if (!at->config->keep_temp_files &&
        !(at->flags & MMS_ATTACHMENT_KEEP_FILES)) {
        char* dir = g_path_get_dirname(at->original_file);
        remove(at->original_file);
        rmdir(dir);
        g_free(dir);
    }
    g_free(at->original_file);
    g_free(at->content_type);
    g_free(at->content_location);
    g_free(at->content_id);
    G_OBJECT_CLASS(mms_attachment_parent_class)->finalize(object);
}

static
void
mms_attachment_class_init(
    MMSAttachmentClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = mms_attachment_finalize;
}

static
void
mms_attachment_init(
    MMSAttachment* at)
{
    GVERBOSE_("%p", at);
}

static
char*
mms_attachment_get_path(
    const char* file,
    GError** error)
{
#ifdef HAVE_REALPATH
    char* path = g_malloc(PATH_MAX);
    if (realpath(file, path)) {
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
            char* fname = g_strdup(path);
            g_free(path);
            return fname;
        } else {
            MMS_ERROR(error, MMS_LIB_ERROR_IO, "%s not found", file);
        }
    } else {
        MMS_ERROR(error, MMS_LIB_ERROR_IO, "%s: %s\n", file, strerror(errno));
    }
    return NULL;
#else
    return g_strdup(file);
#endif
}

static
gboolean
mms_attachment_write_smil(
    FILE* f,
    MMSAttachment** ats,
    int n,
    GError** error)
{
    static const char* text_region_1 =
        "   <region id=\"" REGION_TEXT "\" top=\"0%\" left=\"0%\" "
            "height=\"100%\" width=\"100%\" fit=\"scroll\"/>\n";
    static const char* text_region_2 =
        "   <region id=\"" REGION_TEXT "\" top=\"70%\" left=\"0%\" "
            "height=\"30%\" width=\"100%\" fit=\"scroll\"/>\n";
    static const char* media_region_1 =
        "   <region id=\"" REGION_MEDIA "\" top=\"0%\" left=\"0%\""
            " height=\"100%\" width=\"100%\" fit=\"meet\"/>\n";
    static const char* media_region_2 =
        "   <region id=\"" REGION_MEDIA "\" top=\"0%\" left=\"0%\""
            " height=\"70%\" width=\"100%\" fit=\"meet\"/>\n";

    int i;
    const char* text_region = NULL;
    const char* media_region = NULL;

    /* Check if we have text region, image region or both */
    for (i=0; i<n && !(text_region && media_region); i++) {
        const MMSAttachment* at = ats[i];
        GASSERT(!(at->flags & MMS_ATTACHMENT_SMIL));
        if (g_str_has_prefix(at->content_type, MEDIA_TYPE_TEXT_PREFIX)) {
            text_region = text_region_1;
        } else {
            media_region = media_region_1;
        }
    }

    /* Select non-overlapping layouts if we have both */
    if (text_region && media_region) {
        text_region = text_region_2;
        media_region = media_region_2;
    }

    if (fputs(
        "<smil>\n"
        " <head>\n"
        "  <layout>\n"
        "   <root-layout/>\n", f) >= 0 &&
        (!media_region || fputs(media_region, f) >= 0) &&
        (!text_region || fputs(text_region, f) >= 0) && fputs(
        "  </layout>\n"
        " </head>\n"
        " <body>\n"
        "  <par dur=\"5000ms\">\n", f) >= 0) {
        for (i=0; i<n; i++) {
            const MMSAttachment* at = ats[i];
            const char* ct = at->content_type;
            const char* elem;
            const char* region;
            GASSERT(!(at->flags & MMS_ATTACHMENT_SMIL));
            if (g_str_has_prefix(ct, MEDIA_TYPE_TEXT_PREFIX)) {
                elem = MEDIA_TEXT;
                region = REGION_TEXT;
            } else {
                region = REGION_MEDIA;
                if (g_str_has_prefix(ct, MEDIA_TYPE_IMAGE_PREFIX)) {
                    elem = MEDIA_IMAGE;
                } else if (g_str_has_prefix(ct, MEDIA_TYPE_VIDEO_PREFIX)) {
                    elem = MEDIA_VIDEO;
                } else if (g_str_has_prefix(ct, MEDIA_TYPE_AUDIO_PREFIX)) {
                    elem = MEDIA_AUDIO;
                } else {
                    elem = MEDIA_OTHER;
                }
            }
            if (fprintf(f, "   <%s src=\"%s\" region=\"%s\"/>\n", elem,
                at->content_location, region) < 0) {
                break;
            }
        }
        if (i == n && fputs("  </par>\n </body>\n</smil>\n", f) >= 0) {
            return TRUE;
        }
    }
    MMS_ERROR(error, MMS_LIB_ERROR_IO, "Error writing SMIL: %s",
        strerror(errno));
    return FALSE;
}

char*
mms_attachment_guess_content_type(
    const char* path)
{
    char* content_type = NULL;
    const char* detected_type = NULL;

#ifdef HAVE_MAGIC
    /* Use magic to determine mime type */
    magic_t magic = magic_open(MAGIC_MIME_TYPE);
    if (magic) {
        if (magic_load(magic, NULL) == 0) {
            detected_type = magic_file(magic, path);
        }
    }
#endif

    /* Magic detects SMIL as text/html */
    if ((!detected_type ||
         g_str_has_prefix(detected_type, MEDIA_TYPE_TEXT_PREFIX)) &&
         mms_file_is_smil(path)) {
        detected_type = SMIL_CONTENT_TYPE;
    }

    if (!detected_type) {
        GWARN("No mime type for %s", path);
        detected_type = MMS_ATTACHMENT_DEFAULT_TYPE;
    }

    content_type = g_strdup(detected_type);

#ifdef HAVE_MAGIC
    if (magic) magic_close(magic);
#endif

    return content_type;
}

MMSAttachment*
mms_attachment_new_smil(
    const MMSConfig* config,
    const char* path,
    MMSAttachment** ats,
    int n,
    GError** error)
{
    MMSAttachment* smil = NULL;
    int fd = open(path, O_CREAT|O_RDWR|O_TRUNC, MMS_FILE_PERM);
    if (fd >= 0) {
        FILE* f = fdopen(fd, "w");
        if (f) {
            gboolean ok;
            GVERBOSE("Writing SMIL %s", path);
            ok = mms_attachment_write_smil(f, ats, n, error);
            fclose(f);
            if (ok) {
                MMSAttachmentInfo ai;
                ai.file_name = path;
                ai.content_type = SMIL_CONTENT_TYPE "; charset=utf-8";
                ai.content_id = NULL;
                smil = mms_attachment_new(config, &ai, error);
                GASSERT(smil && (smil->flags & MMS_ATTACHMENT_SMIL));
            }
        } else {
            MMS_ERROR(error, MMS_LIB_ERROR_IO,
                "Failed to open file %s: %s", path, strerror(errno));
            close(fd);
        }
    } else {
        MMS_ERROR(error, MMS_LIB_ERROR_IO,
            "Failed to create file %s: %s", path, strerror(errno));
    }
    return smil;
}

MMSAttachment*
mms_attachment_new(
    const MMSConfig* config,
    const MMSAttachmentInfo* info,
    GError** error)
{
    char* path = mms_attachment_get_path(info->file_name, error);
    if (path) {
        GMappedFile* map = g_mapped_file_new(path, FALSE, error);
        if (map) {
            unsigned int flags = 0;
            char* media_type = NULL;
            char* name = g_path_get_basename(path);
            char* content_type = NULL;
            GType type;
            MMSAttachment* at;

            if (info->content_type && info->content_type[0]) {
                char** ct = mms_parse_http_content_type(info->content_type);
                if (ct) {
                    char** ptr;
                    gboolean append_name = TRUE;
                    if (!strcmp(ct[0], SMIL_CONTENT_TYPE)) {
                        flags |= MMS_ATTACHMENT_SMIL;
                    }
                    for (ptr = ct+1; *ptr; ptr+=2) {
                        if (!g_ascii_strcasecmp(ptr[0], CONTENT_TYPE_PARAM_NAME)) {
                            g_free(ptr[1]);
                            ptr[1] = g_strdup(name);
                            append_name = FALSE;
                        }
                    }
                    if (append_name) {
                        const guint len = g_strv_length(ct);
                        ct = g_renew(gchar*, ct, len+3);
                        ct[len] = g_strdup(CONTENT_TYPE_PARAM_NAME);
                        ct[len+1] = g_strdup(name);
                        ct[len+2] = NULL;
                    }
                    content_type = mms_unparse_http_content_type(ct);
                    media_type = g_strdup(ct[0]);
                    g_strfreev(ct);
                }
            }

            if (!content_type) {
                const char* default_charset = "utf-8";
                const char* charset = NULL;
                const char* ct[6];
                int n = 0;

                media_type = mms_attachment_guess_content_type(path);
                if (!strcmp(media_type, SMIL_CONTENT_TYPE)) {
                    flags |= MMS_ATTACHMENT_SMIL;
                    charset = default_charset;
                } else {
                    if (g_str_has_prefix(media_type, MEDIA_TYPE_TEXT_PREFIX)) {
                        charset = default_charset;
                    }
                }

                ct[n++] = media_type;
                if (charset) {
                    ct[n++] = "charset";
                    ct[n++] = charset;
                }
                ct[n++] = CONTENT_TYPE_PARAM_NAME;
                ct[n++] = name;
                ct[n++] = NULL;
                content_type = mms_unparse_http_content_type((char**)ct);
            }

            GDEBUG("%s: %s", path, content_type);

            if (!strcmp(media_type, "image/jpeg")) {
                type = MMS_TYPE_ATTACHMENT_JPEG;
            } else if (g_str_has_prefix(media_type, MEDIA_TYPE_IMAGE_PREFIX)) {
                type = MMS_TYPE_ATTACHMENT_IMAGE;
            } else {
                type = MMS_TYPE_ATTACHMENT;
            }

            at = g_object_new(type, NULL);
            at->config = config;
            at->map = map;
            at->flags |= flags;
            at->file_name = at->original_file = path;
            at->content_type = content_type;
            at->content_location = name;
            at->content_id = (info->content_id && info->content_id[0]) ?
                g_strdup(info->content_id) :
                g_strdup(at->content_location);

            g_free(media_type);
            return at;
        }
        g_free(path);
    }
    return NULL;
}

MMSAttachment*
mms_attachment_ref(
    MMSAttachment* at)
{
    if (at) g_object_ref(MMS_ATTACHMENT(at));
    return at;
}

void
mms_attachment_unref(
    MMSAttachment* at)
{
    if (at) g_object_unref(MMS_ATTACHMENT(at));
}

void
mms_attachment_reset(
    MMSAttachment* at)
{
    if (at) {
        MMSAttachmentClass* klass = MMS_ATTACHMENT_GET_CLASS(at);
        if (klass->fn_reset) {
            klass->fn_reset(at);
        }
    }
}

gboolean
mms_attachment_resize(
    MMSAttachment* at,
    const MMSSettingsSimData* settings)
{
    if (at) {
        MMSAttachmentClass* klass = MMS_ATTACHMENT_GET_CLASS(at);
        if (klass->fn_resize) {
            return klass->fn_resize(at, settings);
        }
    }
    return FALSE;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
