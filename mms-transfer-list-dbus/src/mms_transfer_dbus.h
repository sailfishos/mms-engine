/*
 * Copyright (C) 2016-2019 Jolla Ltd.
 * Copyright (C) 2016-2019 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2019 Open Mobile Platform LLC.
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

#ifndef JOLLA_MMS_TRANSFER_DBUS_H
#define JOLLA_MMS_TRANSFER_DBUS_H

#include "mms_transfer_list_dbus.h"

#include <gio/gio.h>

typedef struct mms_transfer_key {
    const char* id;
    const char* type;
} MMSTransferKey;

typedef struct mms_transfer_list_dbus MMSTransferListDbus;
typedef struct mms_transfer_dbus_priv MMSTransferDbusPriv;
typedef struct mms_transfer_dbus {
    GObject object;
    MMSTransferDbusPriv* priv;
    MMSTransferListDbus* list;
    MMSTransferKey key;
    const char* path;
} MMSTransferDbus;

GType mms_transfer_dbus_get_type(void);
#define MMS_TYPE_TRANSFER_DBUS (mms_transfer_dbus_get_type())
#define MMS_TRANSFER_DBUS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
    MMS_TYPE_TRANSFER_DBUS, MMSTransferDbus))

MMSTransferDbus*
mms_transfer_dbus_new(
    GDBusConnection* bus,
    DA_BUS da_bus,
    DAPolicy* access,
    const char* id,
    const char* type);

void
mms_transfer_dbus_send_progress(
    MMSTransferDbus* transfer,      /* Instance */
    guint sent,                     /* Bytes sent so far */
    guint total);                   /* Total bytes to send */

void
mms_transfer_dbus_receive_progress(
    MMSTransferDbus* transfer,      /* Instance */
    guint received,                 /* Bytes received so far */
    guint total);                   /* Total bytes to receive*/

void
mms_transfer_dbus_finished(
    MMSTransferDbus* transfer);

#endif /* JOLLA_MMS_TRANSFER_DBUS_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

