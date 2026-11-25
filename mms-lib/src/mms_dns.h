/*
 * Copyright (C) 2026 Jolla Mobile Ltd.
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

#ifndef JOLLA_MMS_DNS_H
#define JOLLA_MMS_DNS_H

#include "mms_lib_types.h"

#include <netinet/in.h>

typedef struct mms_dns_host MMSDnsHost;

/*
 * Until unregistered, lookups of the host name made through the default
 * GResolver go to the given DNS servers and are sent from local_ip, i.e.
 * over the MMS connection. Other host names, and lookups that fail, go
 * to the system resolver. Servers are IPv4 addresses, optionally followed
 * by ":port", others are skipped. Returns NULL if the host is an IP address
 * or there are no usable servers.
 */
MMSDnsHost*
mms_dns_register(
    const char* host,
    const char* const* servers,
    const struct in_addr* local_ip);

void
mms_dns_unregister(
    MMSDnsHost* host);

#endif /* JOLLA_MMS_DNS_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
