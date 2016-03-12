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

#ifndef JOLLA_MMS_LOG_H
#define JOLLA_MMS_LOG_H

#include "mms_lib_types.h"

#ifdef MMS_LOG_MODULE_NAME
#  define GLOG_MODULE_NAME          MMS_LOG_MODULE_NAME
#endif

#include <gutil_log.h>

#define MMS_LOG_MODULE_CURRENT      GLOG_MODULE_CURRENT

#define mms_log                     gutil_log
#define mms_logv                    gutil_logv
#define mms_log_default             gutil_log_default
#define mms_log_parse_option        gutil_log_parse_option
#define mms_log_set_type            gutil_log_set_type
#define mms_log_description         gutil_log_description
#define mms_log_func                gutil_log_func
#define mms_log_syslog              gutil_log_syslog
#define mms_log_stdout_timestamp    gutil_log_timestamp

#define MMS_LOG_TYPE_STDOUT         GLOG_TYPE_STDOUT

#define MMS_LOGLEVEL_NONE           GLOG_LEVEL_NONE
#define MMS_LOGLEVEL_ERR            GLOG_LEVEL_ERR
#define MMS_LOGLEVEL_WARN           GLOG_LEVEL_WARN
#define MMS_LOGLEVEL_INFO           GLOG_LEVEL_INFO
#define MMS_LOGLEVEL_DEBUG          GLOG_LEVEL_DEBUG
#define MMS_LOGLEVEL_VERBOSE        GLOG_LEVEL_VERBOSE

#define MMS_ERRMSG(err)             GERRMSG(err)

#define MMS_LOG_ENABLED             GUTIL_LOG_ANY
#define MMS_LOG_ERR                 GUTIL_LOG_ERR
#define MMS_LOG_WARN                GUTIL_LOG_WARN
#define MMS_LOG_INFO                GUTIL_LOG_INFO
#define MMS_LOG_DEBUG               GUTIL_LOG_DEBUG
#define MMS_LOG_VERBOSE             GUTIL_LOG_VERBOSE
#define MMS_LOG_ASSERT              GUTIL_LOG_ASSERT

#define MMS_LOG_MODULE_DEFINE(x)    GLOG_MODULE_DEFINE(x)
#define MMS_LOG_MODULE_DEFINE2(x,y) GLOG_MODULE_DEFINE2(x,y)

#define MMS_ASSERT(expr)            GASSERT(expr)
#define MMS_VERIFY(expr)            GVERIFY(expr)

#ifdef GLOG_VARARGS
#  define MMS_ERR(f,args...)        GERR(f,##args)
#  define MMS_ERR_(f,args...)       GERR_(f,##args)
#  define MMS_WARN(f,args...)       GWARN(f,##args)
#  define MMS_WARN_(f,args...)      GWARN_(f,##args)
#  define MMS_INFO(f,args...)       GINFO(f,##args)
#  define MMS_INFO_(f,args...)      GINFO_(f,##args)
#  define MMS_DEBUG(f,args...)      GDEBUG(f,##args)
#  define MMS_DEBUG_(f,args...)     GDEBUG_(f,##args)
#  define MMS_VERBOSE(f,args...)    GVERBOSE(f,##args)
#  define MMS_VERBOSE_(f,args...)   GVERBOSE_(f,##args)
#else
#  define MMS_ERR                   GERR
#  define MMS_ERR_                  GERR_
#  define MMS_WARN                  GWARN
#  define MMS_WARN_                 GWARN_
#  define MMS_INFO                  GINFO
#  define MMS_INFO_                 GINFO_
#  define MMS_DEBUG                 GDEBUG
#  define MMS_DEBUG_                GDEBUG_
#  define MMS_VERBOSE               GVERBOSE
#  define MMS_VERBOSE_              GVERBOSE_
#endif /* GLOG_VARARGS */

#endif /* JOLLA_MMS_LOG_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
