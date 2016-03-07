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

#ifndef JOLLA_MMS_ERROR_H
#define JOLLA_MMS_ERROR_H

#include "mms_lib_util.h"

void
mms_error(
    const MMSLogModule* module,
    GError** error,
    MMSLibError code,
    const char* format,
    ...);

void
mms_error_valist(
    const MMSLogModule* module,
    GError** error,
    MMSLibError code,
    const char* format,
    va_list va);

#ifdef __GNUC__
#  define MMS_ERROR(error,code,format,args...) \
    mms_error(MMS_LOG_MODULE_CURRENT, error, code, format, ##args)
#else
#  define MMS_ERROR mms_error_static
static inline void mms_error_static(GError** error, MMSLibError code,   \
    const char* format, ...) {                                          \
    va_list va; va_start(va,format);                                    \
    mms_error_valist(MMS_LOG_MODULE_CURRENT, error, code, format, va);  \
    va_end(va);                                                         \
}
#endif /* __GNUC__ */

#endif /* JOLLA_MMS_ERROR_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
