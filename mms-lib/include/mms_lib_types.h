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

#ifndef SAILFISH_MMS_LIB_TYPES_H
#define SAILFISH_MMS_LIB_TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#  include <io.h>
#  include <direct.h>
#else
#  include <unistd.h>
#endif

#include <fcntl.h>

#include <gutil_types.h>

#include <glib-object.h>

typedef GLogModule MMSLogModule;

#ifndef O_BINARY
#  define O_BINARY (0)
#endif

#ifdef __linux__
#  define HAVE_MAGIC
#endif

/* Types */
typedef struct mms_attachment_info MMSAttachmentInfo;
typedef struct mms_config MMSConfig;
typedef struct mms_settings MMSSettings;
typedef struct mms_settings_sim_data MMSSettingsSimData;
typedef struct mms_handler MMSHandler;
typedef struct mms_connman MMSConnMan;
typedef struct mms_dispatcher MMSDispatcher;
typedef struct mms_connection MMSConnection;
typedef struct mms_transfer_list MMSTransferList;
typedef struct mms_message MMSPdu;
typedef struct _mms_message MMSMessage;
typedef struct _mms_attachment MMSAttachment;

/* MMS content type */
#define MMS_CONTENT_TYPE        "application/vnd.wap.mms-message"
#define SMIL_CONTENT_TYPE       "application/smil"

/* MMS read status */
typedef enum mms_read_status {
    MMS_READ_STATUS_INVALID = -1,   /* Invalid or unknown status */
    MMS_READ_STATUS_READ,           /* Message has been read */
    MMS_READ_STATUS_DELETED         /* Message deleted without reading */
} MMSReadStatus;

/* Connection type */
typedef enum _MMS_CONNECTION_TYPE {
    MMS_CONNECTION_TYPE_AUTO,       /* Internally requested connection */
    MMS_CONNECTION_TYPE_USER        /* Connection requested by user */
} MMS_CONNECTION_TYPE;

#endif /* SAILFISH_MMS_LIB_TYPES_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
