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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "mms_attachment_image.h"
#include "mms_settings.h"
#include "mms_file_util.h"

#ifdef MMS_RESIZE_IMAGEMAGICK
#  include <magick/api.h>
#endif

/* Logging */
#define GLOG_MODULE_NAME mms_attachment_log
#include <gutil_log.h>

G_DEFINE_TYPE(MMSAttachmentImage, mms_attachment_image, MMS_TYPE_ATTACHMENT)
#define MMS_ATTACHMENT_IMAGE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        MMS_TYPE_ATTACHMENT_IMAGE, MMSAttachmentImage))
#define MMS_ATTACHMENT_IMAGE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), \
        MMS_TYPE_ATTACHMENT_IMAGE, MMSAttachmentImageClass))

int
mms_attachment_image_next_resize_step(
    MMSAttachmentImage* image,
    const MMSSettingsSimData* settings,
    unsigned int columns,
    unsigned int rows)
{
    int next_step = image->resize_step + 1;
    const unsigned int max_pixels = settings ? settings->max_pixels :
        MMS_SETTINGS_DEFAULT_MAX_PIXELS;
    if (max_pixels > 0) {
        unsigned int size = (columns/(next_step+1))*(rows/(next_step+1));
        while (size > 0 && size > max_pixels) {
            next_step++;
            size = (columns/(next_step+1))*(rows/(next_step+1));
        }
    }
    return next_step;
}

const char*
mms_attachment_image_prepare_filename(
    MMSAttachmentImage* image)
{
    char* original = image->attachment.original_file;
    if (image->resized) {
        remove(image->resized);
        image->attachment.file_name = original;
    } else {
        image->resized = mms_prepare_filename(original, MMS_CONVERT_DIR);
    }
    return image->resized;
}

static
gboolean
mms_attachment_image_resize_default(
    MMSAttachmentImage* image,
    const MMSSettingsSimData* settings)
{
    gboolean ok = FALSE;
#ifdef MMS_RESIZE_IMAGEMAGICK
    ExceptionInfo ex;
    Image* src;
    ImageInfo* info = CloneImageInfo(NULL);
    const char* fname = mms_attachment_image_prepare_filename(image);
    GetExceptionInfo(&ex);
    strncpy(info->filename, image->attachment.original_file,
        G_N_ELEMENTS(info->filename));
    info->filename[G_N_ELEMENTS(info->filename)-1] = 0;
    src = ReadImage(info, &ex);
    if (src) {
        if (src->magick_columns > 1 && src->magick_rows > 1) {;
            const unsigned int src_cols = src->magick_columns;
            const unsigned int src_rows = src->magick_rows;
            const int next_step = mms_attachment_image_next_resize_step(image,
                settings, src_cols, src_rows);
            const unsigned int cols = src_cols/(next_step+1);
            const unsigned int rows = src_rows/(next_step+1);
            Image* dest;
            GDEBUG("Resizing (%ux%u -> %ux%u) with ImageMagick",
                src_cols, src_rows, cols, rows);
            dest = ResizeImage(src, cols, rows, BoxFilter, 1.0, &ex);
            if (dest) {
                image->resize_step = next_step;
                strncpy(info->filename, fname, G_N_ELEMENTS(info->filename));
                strncpy(dest->filename, fname, G_N_ELEMENTS(dest->filename));
                info->filename[G_N_ELEMENTS(info->filename)-1] = 0;
                dest->filename[G_N_ELEMENTS(dest->filename)-1] = 0;
                if (WriteImage(info, dest)) {
                    GDEBUG("Resized %s with ImageMagick", fname);
                    ok = TRUE;
                } else {
                    GERR("Failed to write %s", dest->filename);
                }
                DestroyImage(dest);
            }
        }
        DestroyImage(src);
    } else {
        GERR("Failed to read %s", info->filename);
    }
    ClearMagickException(&ex);
    DestroyExceptionInfo(&ex);
    DestroyImageInfo(info);
#else
#  ifdef MMS_RESIZE_QT
    ok = mms_attachment_image_resize_qt(image, settings);
#  endif /* MMS_RESIZE_QT */
#endif /* MMS_RESIZE_IMAGEMAGICK */
    return ok;
}

static
gboolean
mms_attachment_image_resize_type_specific(
    MMSAttachmentImage* image,
    const MMSSettingsSimData* settings)
{
    /* If klass->fn_resize_new is not NULL, then we assume that all
     * other callbacks are present as well */
    gboolean ok = FALSE;
    MMSAttachment* at = &image->attachment;
    MMSAttachmentImageClass* klass = MMS_ATTACHMENT_IMAGE_GET_CLASS(image);
    MMSAttachmentImageResize* resize;
    if (klass->fn_resize_new && (resize =
        klass->fn_resize_new(image, at->original_file)) != NULL) {
        gboolean can_resize;
        const char* fname = mms_attachment_image_prepare_filename(image);
        const int next_step = mms_attachment_image_next_resize_step(image,
            settings, resize->image.width, resize->image.height);
        MMSAttachmentImageSize image_size;
        MMSAttachmentImageSize out_size;
        image_size = resize->image;
        out_size.width = image_size.width/(next_step+1);
        out_size.height = image_size.height/(next_step+1);

        resize->in = resize->out = out_size;
        can_resize = klass->fn_resize_prepare(resize, fname);
        if (!can_resize) {
            klass->fn_resize_free(resize);
            resize = klass->fn_resize_new(image, at->original_file);
            if (!resize) return FALSE;
            GASSERT(resize->image.width == image_size.width);
            GASSERT(resize->image.height == image_size.height);
            resize->in = image_size;
            resize->out = out_size;
            can_resize = klass->fn_resize_prepare(resize, fname);
        }

        if (can_resize) {
            unsigned char* line = g_malloc(3*resize->in.width);
            guint y;
            if (resize->in.width == resize->out.width &&
                resize->in.height == resize->out.height) {
                /* Nothing to resize, image decompressor is doing all
                 * the job for us */
                GDEBUG("Decoder-assisted resize (%ux%u -> %ux%u)",
                    image_size.width, image_size.height,
                    out_size.width, out_size.height);
                for (y=0;
                     y<resize->in.height &&
                     klass->fn_resize_read_line(resize, line) &&
                     klass->fn_resize_write_line(resize, line);
                     y++);
            } else {
                const guint nx = (resize->in.width/resize->out.width);
                const guint ny = (resize->in.height/resize->out.height);
                gsize bufsize = 3*resize->out.width*sizeof(guint);
                guint* buf = g_malloc(bufsize);
                memset(buf, 0, bufsize);
                GDEBUG("Resizing (%ux%u -> %ux%u)",
                    image_size.width, image_size.height,
                    out_size.width, out_size.height);
                for (y=0;
                     y<resize->in.height &&
                     klass->fn_resize_read_line(resize, line);
                     y++) {

                    /* Update the resize buffer */
                    guint x;
                    guint* bufptr = buf;
                    const unsigned char* lineptr = line;
                    for (x=0; x<resize->out.width; x++) {
                        guint k;
                        for (k=0; k<nx; k++) {
                            bufptr[0] += (*lineptr++);
                            bufptr[1] += (*lineptr++);
                            bufptr[2] += (*lineptr++);
                        }
                        bufptr += 3;
                    }

                    if ((y % ny) == (ny-1)) {
                        /* Average the pixels */
                        unsigned char* outptr = line;
                        const guint denominator = nx*ny;
                        bufptr = buf;
                        for (x=0; x<resize->out.width; x++) {
                            (*outptr++) = (*bufptr++)/denominator;
                            (*outptr++) = (*bufptr++)/denominator;
                            (*outptr++) = (*bufptr++)/denominator;
                        }

                        /* And write the next line */
                        if (klass->fn_resize_write_line(resize, line)) {
                            memset(buf, 0, bufsize);
                        } else {
                            break;
                        }
                    }
                }
                g_free(buf);
            }

            if (klass->fn_resize_finish) {
                klass->fn_resize_finish(resize);
            }

            if (y == resize->in.height) {
                GDEBUG("Resized %s", fname);
                image->resize_step = next_step;
                ok = TRUE;
            }

            g_free(line);
        }

        klass->fn_resize_free(resize);
    }

    return ok;
}

static
gboolean
mms_attachment_image_resize(
    MMSAttachment* at,
    const MMSSettingsSimData* settings)
{
    MMSAttachmentImage* image = MMS_ATTACHMENT_IMAGE(at);
    gboolean ok;
    if (at->map && image->resized) {
        g_mapped_file_unref(at->map);
        at->map = NULL;
    }
    ok = mms_attachment_image_resize_type_specific(image, settings);
    if (!ok) ok = mms_attachment_image_resize_default(image, settings);
    if (ok) {
        GError* error = NULL;
        GMappedFile* map = g_mapped_file_new(image->resized, FALSE, &error);
        if (map) {
            if (at->map) g_mapped_file_unref(at->map);
            at->file_name = image->resized;
            at->map = map;
        } else {
            GERR("%s", GERRMSG(error));
            g_error_free(error);
            ok = FALSE;
        }
    }
    return ok;
}

static
void
mms_attachment_image_reset(
    MMSAttachment* at)
{
    MMSAttachmentImage* image = MMS_ATTACHMENT_IMAGE(at);
    at->file_name = at->original_file;
    if (image->resize_step) {
        image->resize_step = 0;
        if (at->map) g_mapped_file_unref(at->map);
        at->map = g_mapped_file_new(at->original_file, FALSE, NULL);
    }
}

static
void
mms_attachment_image_finalize(
    GObject* object)
{
    MMSAttachmentImage* image = MMS_ATTACHMENT_IMAGE(object);
    if (!image->attachment.config->keep_temp_files) {
        mms_remove_file_and_dir(image->resized);
    }
    g_free(image->resized);
    G_OBJECT_CLASS(mms_attachment_image_parent_class)->finalize(object);
}

static
void
mms_attachment_image_class_init(
    MMSAttachmentImageClass* klass)
{
    klass->attachment.fn_reset = mms_attachment_image_reset;
    klass->attachment.fn_resize = mms_attachment_image_resize;
    G_OBJECT_CLASS(klass)->finalize = mms_attachment_image_finalize;
}

static
void
mms_attachment_image_init(
    MMSAttachmentImage* image)
{
#if defined(MMS_RESIZE_IMAGEMAGICK) || defined(MMS_RESIZE_QT)
    image->attachment.flags |= MMS_ATTACHMENT_RESIZABLE;
#endif
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
