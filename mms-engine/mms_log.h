/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#ifndef JOLLA_MMS_LOG_H
#define JOLLA_MMS_LOG_H

#include <dbuslog_server.h>
#include <gutil_types.h>
#include <gio/gio.h>

typedef struct mms_log {
    DBusLogServer* server;
} MMSLog;

MMSLog*
mms_log_new(
    GBusType bus,
    GLogModule* modules[]); /* NULL terminated */

void
mms_log_free(
    MMSLog* log);

#endif /* JOLLA_MMS_LOG_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
