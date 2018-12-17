/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#include <unistd.h>
#include <sys/socket.h>

#include "km_hcalls.h"

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
   km_hc_args_t arg;

   arg.arg1 = sockfd;
   arg.arg2 = (uint64_t)addr;
   arg.arg3 = addrlen;
   return km_hcall(KM_HC_BIND, &arg);
}
