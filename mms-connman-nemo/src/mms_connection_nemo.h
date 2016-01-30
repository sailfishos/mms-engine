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
 *
 */

#ifndef JOLLA_MMS_CONNECTION_NEMO_H
#define JOLLA_MMS_CONNECTION_NEMO_H

#include "mms_connection.h"

#include <gofonoext_mm.h>

MMSConnection*
mms_connection_nemo_new(
    MMSConnMan* cm,
    const char* imsi,
    gboolean user_request);

#endif /* JOLLA_MMS_CONNECTION_NEMO_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
