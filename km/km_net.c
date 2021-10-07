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

// clang-format off
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/if_arp.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>

#include "bsd_queue.h"
#include "km.h"
// clang-format on

typedef struct km_net_interface_info {
   char* if_name;
   uint64_t if_index;
   int sock_fd;
   size_t mtu;
   int packet_version;
   int packet_header_length;
   uint64_t recv_packets;
   uint64_t send_packets;
   uint64_t recv_bytes;
   uint64_t send_bytes;
   struct tpacket_req3 tr3;
   size_t tr_buf_len;
   void* tr_buf;
   struct iovec* block_wait;
   int current_block_index;
   int avail_packet_count;
   uint32_t current_offset;
} km_net_interface_info_t;

typedef struct block_header {
   uint32_t version;
   uint32_t offset;
   struct tpacket_hdr_v1 block_hdr;
} block_header_t;

km_net_interface_info_t net_info;

void km_net_set_packet_version()
{
   int pkt_version = TPACKET_V3;
   if (setsockopt(net_info.sock_fd,
                  SOL_PACKET,
                  PACKET_VERSION,
                  (void*)&pkt_version,
                  (socklen_t)sizeof(pkt_version)) == -1) {
      km_err(2, "setsockopt failed when setting packet version to TPACKET_V3");
   }
   net_info.packet_version = pkt_version;
   km_infox(KM_TRACE_NET, "set packet version for interface %s to TPACKET_V3", net_info.if_name);
}

void km_net_get_ifname_to_index()
{
   net_info.if_index = if_nametoindex(net_info.if_name);
   km_infox(KM_TRACE_NET, "if_index for interface %s is %ld", net_info.if_name, net_info.if_index);
}

void km_net_bind_interface()
{
   struct sockaddr_ll sock_ll;

   memset(&sock_ll, 0, sizeof(sock_ll));
   sock_ll.sll_family = AF_PACKET;
   sock_ll.sll_protocol = htons(ETH_P_ALL);
   sock_ll.sll_ifindex = net_info.if_index;
   km_infox(KM_TRACE_NET, "%d %d %d\n", sock_ll.sll_family, sock_ll.sll_protocol, sock_ll.sll_ifindex);
   if (bind(net_info.sock_fd, (struct sockaddr*)&sock_ll, sizeof(sock_ll)) == -1) {
      km_err(2, "bind failed");
   }
   km_infox(KM_TRACE_NET, "bind successfull for interface %s", net_info.if_name);
}

void km_net_get_mtu_size()
{
   struct ifreq ifr;

   memset(&ifr, 0, sizeof(ifr));

   strncpy(ifr.ifr_name, net_info.if_name, IFNAMSIZ - 1);
   if (ioctl(net_info.sock_fd, SIOCGIFMTU, &ifr) == -1) {
      km_err(2, "ioctl SIOCGIFMTU failed");
   }
   net_info.mtu = ifr.ifr_mtu;
   km_infox(KM_TRACE_NET, "mtu size for interface %s is %ld", net_info.if_name, net_info.mtu);
}

void km_net_get_packet_header_length()
{
   int val = TPACKET_V3;
   socklen_t len = sizeof(val);

   if (getsockopt(net_info.sock_fd, SOL_PACKET, PACKET_HDRLEN, &val, &len) == -1) {
      km_err(2, "getsockopt failed when getting packet header length");
   }
   net_info.packet_header_length = val;
   km_infox(KM_TRACE_NET,
            "packet header length for interface %s is %d",
            net_info.if_name,
            net_info.packet_header_length);
}

void km_net_rx_ring()
{
   net_info.tr3.tp_block_size = 4 * 1024 * 1024;
   net_info.tr3.tp_block_nr = 64;
   net_info.tr3.tp_frame_size = 2048;
   net_info.tr3.tp_frame_nr =
       net_info.tr3.tp_block_size * net_info.tr3.tp_block_nr / net_info.tr3.tp_frame_size;
   net_info.tr3.tp_retire_blk_tov = 60;
   net_info.tr3.tp_sizeof_priv = 0;
   net_info.tr3.tp_feature_req_word = TP_FT_REQ_FILL_RXHASH;
   if (setsockopt(net_info.sock_fd, SOL_PACKET, PACKET_RX_RING, &net_info.tr3, sizeof(net_info.tr3)) ==
       -1) {
      km_err(2, "setsockopt failed when creating RX_RING");
   }
   net_info.tr_buf_len = net_info.tr3.tp_block_size * net_info.tr3.tp_block_nr;
   net_info.tr_buf =
       mmap(0, net_info.tr_buf_len, PROT_READ | PROT_WRITE, MAP_SHARED, net_info.sock_fd, 0);
   if (net_info.tr_buf == MAP_FAILED) {
      km_err(2, "mmap failed for RX_RING buffer");
   }
   km_infox(KM_TRACE_NET,
            "interface %s RX_RING addr %p len %ld",
            net_info.if_name,
            net_info.tr_buf,
            net_info.tr_buf_len);
   net_info.block_wait = (struct iovec*)malloc(net_info.tr3.tp_block_nr * sizeof(struct iovec));
   if (net_info.block_wait == NULL) {
      km_err(2, "no memory for wait buffer");
   }
   for (int i = 0; i < net_info.tr3.tp_block_nr; i++) {
      net_info.block_wait[i].iov_base = net_info.tr_buf + net_info.tr3.tp_block_size * i;
      net_info.block_wait[i].iov_len = net_info.tr3.tp_block_size;
   }
   for (int i = 0; i < net_info.tr3.tp_block_nr; i++) {
      block_header_t* bd = net_info.block_wait[i].iov_base;
      km_infox(KM_TRACE_NET,
               "interface %s index %d bd %p version %d offset %d status %d num %d",
               net_info.if_name,
               i,
               bd,
               bd->version,
               bd->offset,
               bd->block_hdr.block_status,
               bd->block_hdr.num_pkts);
   }
}

void km_net_init(char* interface_name)
{
   km_infox(KM_TRACE_NET, "km_net_init");
   if (interface_name == NULL || strlen(interface_name) == 0) {
      return;
   }
   net_info.if_name = strdup(interface_name);
   if (net_info.if_name == NULL) {
      km_err(2, "no memory for interface name");
   }

   net_info.sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
   if (net_info.sock_fd < 0) {
      km_err(2, "socket creation failed");
   }

   km_net_set_packet_version();

   km_net_get_mtu_size();

   km_net_get_packet_header_length();

   km_net_rx_ring();

   km_net_get_ifname_to_index();

   km_net_bind_interface();

   net_info.current_block_index = 0;
   net_info.avail_packet_count = 0;
   net_info.current_offset = 0;
}

void km_net_fini()
{
   km_infox(KM_TRACE_NET, "km_net_fini");
   free(net_info.block_wait);
   munmap(net_info.tr_buf, net_info.tr_buf_len);
   close(net_info.sock_fd);
   free(net_info.if_name);
}

uint64_t km_net_recv_packet(km_vcpu_t* vcpu, void* buf, size_t count)
{
   size_t byte_count = 0;
   km_infox(KM_TRACE_NET, "km_net_recv_packet");
   if (count < net_info.mtu) {
      return -EFAULT;
   }

   if (net_info.avail_packet_count == 0) {
      block_header_t* bh = (block_header_t*)net_info.block_wait[net_info.current_block_index].iov_base;

      if ((bh->block_hdr.block_status & TP_STATUS_USER) == 0) {
         struct pollfd p;
         memset(&p, 0, sizeof(p));
         p.fd = net_info.sock_fd;
         p.events = POLLIN | POLLERR;
         p.revents = 0;

         poll(&p, 1, -1);
      }
      net_info.avail_packet_count = bh->block_hdr.num_pkts;
      net_info.current_offset = bh->block_hdr.offset_to_first_pkt;
      km_infox(KM_TRACE_NET,
               "km_net_recv_packet block status %d number of packets%d offset to first packet %d "
               "block length %d",
               bh->block_hdr.block_status,
               bh->block_hdr.num_pkts,
               bh->block_hdr.offset_to_first_pkt,
               bh->block_hdr.blk_len);
   }

   if (net_info.avail_packet_count > 0) {
      struct tpacket3_hdr* t3_hdr =
          (struct tpacket3_hdr*)((uint8_t*)net_info.block_wait[net_info.current_block_index].iov_base +
                                 net_info.current_offset);
      uint8_t* mac_start_addr = (uint8_t*)t3_hdr + t3_hdr->tp_mac;
      byte_count = (t3_hdr->tp_snaplen) > count ? count : t3_hdr->tp_snaplen;
      memcpy(buf, mac_start_addr, byte_count);
      km_infox(KM_TRACE_NET,
               "km_net_recv_packet byte_count %ld\n", byte_count);

      km_infox(KM_TRACE_NET,
               "km_net_recv_packet packet next offset %d snap length %d tp length %d offset to mac "
               "%d offset to net %d",
               t3_hdr->tp_next_offset,
               t3_hdr->tp_snaplen,
               t3_hdr->tp_len,
               t3_hdr->tp_mac,
               t3_hdr->tp_net);
      byte_count = t3_hdr->tp_len;

      net_info.avail_packet_count--;
      net_info.current_offset += t3_hdr->tp_next_offset;

      if (net_info.avail_packet_count == 0) {
         block_header_t* bh =
             (block_header_t*)net_info.block_wait[net_info.current_block_index].iov_base;
         bh->block_hdr.block_status = TP_STATUS_KERNEL;
         net_info.current_block_index = (net_info.current_block_index + 1) % net_info.tr3.tp_block_nr;
      }
   }

   return byte_count;
}

uint64_t km_net_send_packet(km_vcpu_t* vcpu, void* buf, size_t count)
{
   ssize_t byte_count;
   if ((byte_count = send(net_info.sock_fd, buf, count, 0)) == -1) {
      byte_count = -errno;
   } else {
      net_info.send_packets++;
      net_info.send_bytes += byte_count;
   }
   return byte_count;
}

uint64_t km_net_ifname(km_vcpu_t* vcpu, void* buf, size_t count)
{
   struct ifreq* ifr = (struct ifreq*)buf;
   size_t len = count < IFNAMSIZ ? count : IFNAMSIZ;
   memset(ifr, 0, len);
   strncpy(ifr->ifr_name, net_info.if_name, len - 1);
   return strlen(buf);
}
