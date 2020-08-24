/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#ifndef SAILFISH_MMS_ATTACHMENT_INFO_H
#define SAILFISH_MMS_ATTACHMENT_INFO_H

#include "mms_lib_types.h"

/* Attachment information */
struct mms_attachment_info {
    GMappedFile* map;
    gsize size;
    const void* data;
    const char* file_name;
    const char* content_type;
    const char* content_id;
};

gboolean
mms_attachment_info_path(
    MMSAttachmentInfo* ai,
    const char* path,
    const char* content_type,
    const char* content_id,
    GError** error);

gboolean
mms_attachment_info_fd(
    MMSAttachmentInfo* ai,
    int fd,
    const char* name,
    const char* content_type,
    const char* content_id,
    GError** error);

void
mms_attachment_info_cleanup(
    MMSAttachmentInfo* ai);

#endif /* SAILFISH_MMS_ATTACHMENT_INFO_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
