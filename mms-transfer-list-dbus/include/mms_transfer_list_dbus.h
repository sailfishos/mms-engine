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

#ifndef JOLLA_MMS_TRANSFER_LIST_DBUS_H
#define JOLLA_MMS_TRANSFER_LIST_DBUS_H

#include "mms_transfer_list.h"

#include <dbusaccess_types.h>

#define MMS_TRANSFER_LIST_DBUS_METHODS(m) \
    m(GET)

typedef enum mms_transfer_list_action {
    /* Action ids must be non-zero, shift those by one */
    MMS_TRANSFER_LIST_ACTION_NONE = 0,
    #define MMS_TRANSFER_LIST_ACTION_(id) MMS_TRANSFER_LIST_ACTION_##id,
    MMS_TRANSFER_LIST_DBUS_METHODS(MMS_TRANSFER_LIST_ACTION_)
    #undef MMS_TRANSFER_LIST_ACTION_
} MMS_TRANSFER_LIST_ACTION;

#define MMS_TRANSFER_DBUS_METHODS(m) \
    m(GET_ALL) \
    m(ENABLE_UPDATES) \
    m(DISABLE_UPDATES) \
    m(GET_INTERFACE_VERSION) \
    m(GET_SEND_PROGRESS) \
    m(GET_RECEIVE_PROGRESS)

typedef enum mms_transfer_action {
    /* Action ids must be non-zero, shift those by one */
    MMS_TRANSFER_ACTION_NONE = 0,
    #define MMS_TRANSFER_ACTION_(id) MMS_TRANSFER_ACTION_##id,
    MMS_TRANSFER_DBUS_METHODS(MMS_TRANSFER_ACTION_)
    #undef MMS_TRANSFER_ACTION_
} MMS_TRANSFER_ACTION;

MMSTransferList*
mms_transfer_list_dbus_new(
    DAPolicy* tx_list_access,
    DAPolicy* tx_access);

#endif /* JOLLA_MMS_TRANSFER_LIST_DBUS_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

