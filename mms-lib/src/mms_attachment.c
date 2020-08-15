/*
 * Copyright (C) 2013-2020 Jolla Ltd.
 * Copyright (C) 2013-2020 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2020 Open Mobile Platform LLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "mms_attachment.h"
#include "mms_attachment_info.h"
#include "mms_file_util.h"
#include "mms_settings.h"
#include "mms_codec.h"

#include "gutil_strv.h"

#ifdef HAVE_MAGIC
#  include <magic.h>
#endif

/* Logging */
#define GLOG_MODULE_NAME mms_attachment_log
#include "mms_error.h"
GLOG_MODULE_DEFINE("mms-attachment");

#define MMS_ATTACHMENT_DEFAULT_TYPE "application/octet-stream"

G_DEFINE_TYPE(MMSAttachment, mms_attachment, G_TYPE_OBJECT)

#define MMS_ATTACHMENT(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), MMS_TYPE_ATTACHMENT, MMSAttachment))
#define MMS_ATTACHMENT_GET_CLASS(obj)  \
    (G_TYPE_INSTANCE_GET_CLASS((obj), MMS_TYPE_ATTACHMENT, MMSAttachmentClass))

#define SMIL_CONTENT_ID "smil"

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

#define MEDIA_TYPE_IMAGE_JPEG   MEDIA_TYPE_IMAGE_PREFIX "jpeg"

static
void
mms_attachment_finalize(
    GObject* object)
{
    MMSAttachment* at = MMS_ATTACHMENT(object);
    GVERBOSE_("%p", at);
    if (at->map) g_mapped_file_unref(at->map);
    if (!at->config->keep_temp_files) {
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

static
char*
mms_attachment_guess_content_type(
    const MMSAttachmentInfo* ai)
{
    char* content_type = NULL;
    const char* detected_type = NULL;

#ifdef HAVE_MAGIC
    /* Use magic to determine mime type */
    magic_t magic = magic_open(MAGIC_MIME_TYPE);
    if (magic) {
        if (magic_load(magic, NULL) == 0) {
            detected_type = magic_buffer(magic, ai->data, ai->size);
        }
    }
#endif

    /* Magic detects SMIL as text/html */
    if ((!detected_type ||
         g_str_has_prefix(detected_type, MEDIA_TYPE_TEXT_PREFIX)) &&
         mms_file_is_smil(ai)) {
        detected_type = SMIL_CONTENT_TYPE;
    }

    if (!detected_type) {
        GWARN("No mime type for %s", ai->file_name);
        detected_type = MMS_ATTACHMENT_DEFAULT_TYPE;
    }

    content_type = g_strdup(detected_type);

#ifdef HAVE_MAGIC
    if (magic) magic_close(magic);
#endif

    return content_type;
}

static
char*
mms_attachment_guess_text_encoding(
    const MMSAttachmentInfo* ai)
{
    char* encoding = NULL;
    const char* detected = NULL;

#ifdef HAVE_MAGIC
    /* Use magic to determine mime type */
    magic_t magic = magic_open(MAGIC_MIME_ENCODING);
    if (magic) {
        if (magic_load(magic, NULL) == 0) {
            detected = magic_buffer(magic, ai->data, ai->size);
        }
    }
#endif

    if (detected) {
        GDEBUG("%s: detected %s", ai->file_name, detected);
        encoding = g_strdup(detected);
    } else {
        encoding = g_strdup(MMS_DEFAULT_CHARSET);
    }

#ifdef HAVE_MAGIC
    if (magic) magic_close(magic);
#endif

    return encoding;
}

static
gboolean
mms_attachment_info_init(
    MMSAttachmentInfo* ai,
    GMappedFile* map,
    const char* path,
    const char* content_type,
    const char* content_id)
{
    if (map) {
        ai->map = map;
        ai->data = g_mapped_file_get_contents(map);
        ai->size = g_mapped_file_get_length(map);
        ai->file_name = path;
        ai->content_type = content_type;
        ai->content_id = content_id;
        return TRUE;
    } else {
        memset(ai, 0, sizeof(*ai));
        return FALSE;
    }
}

gboolean
mms_attachment_info_path(
    MMSAttachmentInfo* ai,
    const char* path,
    const char* content_type,
    const char* content_id,
    GError** error)
{
    return mms_attachment_info_init(ai,
        g_mapped_file_new(path, FALSE, error),
        path, content_type, content_id);
}

gboolean
mms_attachment_info_fd(
    MMSAttachmentInfo* ai,
    int fd,
    const char* name,
    const char* content_type,
    const char* content_id,
    GError** error)
{
    return mms_attachment_info_init(ai,
        g_mapped_file_new_from_fd(fd, FALSE, error),
        name, content_type, content_id);
}

void
mms_attachment_info_cleanup(
    MMSAttachmentInfo* ai)
{
    if (ai->map) g_mapped_file_unref(ai->map);
    memset(ai, 0, sizeof(*ai));
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

                if (mms_attachment_info_path(&ai, path,
                    SMIL_CONTENT_TYPE "; " CONTENT_TYPE_PARAM_CHARSET "="
                    MMS_DEFAULT_CHARSET, SMIL_CONTENT_ID, error)) {
                    smil = mms_attachment_new(config, &ai, error);
                    GASSERT(smil && (smil->flags & MMS_ATTACHMENT_SMIL));
                    mms_attachment_info_cleanup(&ai);
                }
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
    unsigned int flags = 0;
    char* media_type = NULL;
    char* content_type = NULL;
    char* name = g_path_get_basename(info->file_name);
    GType type = MMS_TYPE_ATTACHMENT;
    MMSAttachment* at;
    MMSAttachmentClass* klass;

    /*
     * We always need to provide charset for text attachments because
     * operators may (and often do) want to convert those to their
     * favorite charset, in which case they need to know the original
     * one (some just blindly assume us-ascii and mess things up).
     */
    if (info->content_type && info->content_type[0]) {
        char** ct = mms_parse_http_content_type(info->content_type);
        if (ct) {
            char** ptr;
            gboolean append_name = TRUE;
            gboolean append_charset = g_str_has_prefix(ct[0],
                MEDIA_TYPE_TEXT_PREFIX);

            for (ptr = ct+1; *ptr; ptr+=2) {
                const char* p = ptr[0];

                if (!g_ascii_strcasecmp(p, CONTENT_TYPE_PARAM_NAME)) {
                    g_free(ptr[1]);
                    ptr[1] = g_strdup(name);
                    append_name = FALSE;
                } else if (append_charset && !g_ascii_strcasecmp(p,
                    CONTENT_TYPE_PARAM_CHARSET)) {
                    /* Charset is provided by the caller */
                    append_charset = FALSE;
                }
            }
            if (append_name) {
                ct = gutil_strv_addv(ct, CONTENT_TYPE_PARAM_NAME, name, NULL);
            }
            if (append_charset) {
                char* enc = mms_attachment_guess_text_encoding(info);

                if (enc) {
                    ct = gutil_strv_addv(ct, CONTENT_TYPE_PARAM_CHARSET,
                        enc, NULL);
                    g_free(enc);
                }
            }
            content_type = mms_unparse_http_content_type(ct);
            media_type = g_strdup(ct[0]);
            g_strfreev(ct);
        }
    }

    if (!content_type) {
        char* detected = NULL;
        const char* charset = NULL;
        const char* ct[6];
        int n = 0;

        media_type = mms_attachment_guess_content_type(info);
        if (g_str_has_prefix(media_type, MEDIA_TYPE_TEXT_PREFIX)) {
            detected = mms_attachment_guess_text_encoding(info);
            charset = detected;
        }

        ct[n++] = media_type;
        if (charset) {
            ct[n++] = CONTENT_TYPE_PARAM_CHARSET;
            ct[n++] = charset;
        }
        ct[n++] = CONTENT_TYPE_PARAM_NAME;
        ct[n++] = name;
        ct[n++] = NULL;
        content_type = mms_unparse_http_content_type((char**)ct);
        g_free(detected);
    }

    if (!strcmp(media_type, SMIL_CONTENT_TYPE)) {
        flags |= MMS_ATTACHMENT_SMIL;
    } else if (!strcmp(media_type, MEDIA_TYPE_IMAGE_JPEG)) {
        type = MMS_TYPE_ATTACHMENT_JPEG;
    } else if (g_str_has_prefix(media_type, MEDIA_TYPE_IMAGE_PREFIX)) {
        type = MMS_TYPE_ATTACHMENT_IMAGE;
    } else if (g_str_has_prefix(media_type, MEDIA_TYPE_TEXT_PREFIX)) {
        type = MMS_TYPE_ATTACHMENT_TEXT;
    }

    at = g_object_new(type, NULL);
    at->config = config;
    at->flags |= flags;
    at->file_name = at->original_file = g_strdup(info->file_name);
    at->content_type = content_type;
    at->content_location = name;
    at->content_id = (info->content_id && info->content_id[0]) ?
        g_strdup(info->content_id) :
        g_strdup(at->content_location);

    if (info->map) {
        at->map = g_mapped_file_ref(info->map);
    }

    g_free(media_type);
    klass = MMS_ATTACHMENT_GET_CLASS(at);

    if (!klass->fn_init || klass->fn_init(at)) {
        GDEBUG("%s: %s", at->file_name, at->content_type);
        return at;
    }
    /* Init failed */
    mms_attachment_unref(at);
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
