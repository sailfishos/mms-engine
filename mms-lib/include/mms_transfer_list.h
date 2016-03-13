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

#ifndef JOLLA_MMS_TRANSFER_LIST_H
#define JOLLA_MMS_TRANSFER_LIST_H

#include "mms_message.h"

/* Instance */
struct mms_transfer_list {
    GObject object;
};

/* Class */
typedef struct mms_transfer_list_class {
    GObjectClass parent;
    void (*fn_transfer_started)(
        MMSTransferList* list,      /* Instance */
        char* id,                   /* Message ID */
        char* type);                /* Transfer type */
    void (*fn_transfer_finished)(
        MMSTransferList* list,      /* Instance */
        char* id,                   /* Message ID */
        char* type);                /* Transfer type */
    void (*fn_send_progress)(
        MMSTransferList* list,      /* Instance */
        char* id,                   /* Message ID */
        char* type,                 /* Transfer type */
        guint bytes_sent,           /* Bytes sent so far */
        guint bytes_total);         /* Total bytes to send */
    void (*fn_receive_progress)(
        MMSTransferList* list,      /* Instance */
        char* id,                   /* Message ID */
        char* type,                 /* Transfer type */
        guint bytes_received,       /* Bytes received so far */
        guint bytes_total);         /* Total bytes to receive*/
} MMSTransferListClass;

GType mms_transfer_list_get_type(void);
#define MMS_TYPE_TRANSFER_LIST (mms_transfer_list_get_type())

MMSTransferList*
mms_transfer_list_ref(
    MMSTransferList* list);

void
mms_transfer_list_unref(
    MMSTransferList* list);

void
mms_transfer_list_transfer_started(
    MMSTransferList* list,          /* Instance */
    char* id,                       /* Message ID */
    char* type);                    /* Transfer type */

void
mms_transfer_list_transfer_finished(
    MMSTransferList* list,          /* Instance */
    char* id,                       /* Message ID */
    char* type);                    /* Transfer type */

void
mms_transfer_list_transfer_send_progress(
    MMSTransferList* list,          /* Instance */
    char* id,                       /* Message ID */
    char* type,                     /* Transfer type */
    guint bytes_sent,               /* Bytes sent so far */
    guint bytes_total);             /* Total bytes to send */

void
mms_transfer_list_transfer_receive_progress(
    MMSTransferList* list,          /* Instance */
    char* id,                       /* Message ID */
    char* type,                     /* Transfer type */
    guint bytes_received,           /* Bytes received so far */
    guint bytes_total);             /* Total bytes to receive*/

#endif /* JOLLA_MMS_TRANSFER_LIST_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
