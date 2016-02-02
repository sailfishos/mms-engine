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

#ifndef JOLLA_MMS_VERSION_H
#define JOLLA_MMS_VERSION_H

#ifndef MMS_ENGINE_VERSION
#  define MMS_ENGINE_VERSION 1.0.41
#endif

#define MMS_VERSION_STRING__(x) #x
#define MMS_VERSION_STRING_(x) MMS_VERSION_STRING__(x)
#define MMS_VERSION_STRING MMS_VERSION_STRING_(MMS_ENGINE_VERSION)

#endif /* JOLLA_MMS_VERSION_H */
