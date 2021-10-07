/*
 * Copyright 2021 Kontain Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Test saying "hello world" via printf
 */
#include <stdio.h>
#include <stdlib.h>

#include <km_hcalls.h>

static const char msg[] = "Hello,";

int main(int argc, char** argv)
{
   char* msg2 = "world";

   uint8_t buf[10240];
   struct ifreq ifr;
   km_hc_args_t recv_args = {.arg1 = NET_RECV_PACKET, .arg2 = (uint64_t)buf, .arg3 = 1500};
   km_hc_args_t send_args = {.arg1 = NET_SEND_PACKET, .arg2 = (uint64_t)buf, .arg3 = 1500};
   km_hc_args_t ifreq_args = {.arg1 = NET_SIOCGIFNAME, .arg2 = (uint64_t)&ifr, .arg3 = sizeof(ifr)};

   km_hcall(HC_net_call, &ifreq_args);
   printf("name %s\n", ifr.ifr_name);

   for (int i = 0; i < 100; i++) {
      km_hcall(HC_net_call, &recv_args);
      printf("recv %ld\n", recv_args.hc_ret);
      printf("dst mac %02x:%02x:%02x:%02x:%02x:%02x ", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
      printf("src mac %02x:%02x:%02x:%02x:%02x:%02x ", buf[6], buf[7], buf[8], buf[9], buf[10], buf[11]);
      printf("eth typ %04x\n", *((uint16_t*)&buf[12]));
      send_args.arg3 = recv_args.hc_ret + 1;
      km_hcall(HC_net_call, &send_args);
      printf("send %ld\n", send_args.hc_ret);
   }

   printf("%s %s\n", msg, msg2);
   for (int i = 0; i < argc; i++) {
      printf("%s argv[%d] = '%s'\n", msg, i, argv[i]);
   }
   exit(0);
}
