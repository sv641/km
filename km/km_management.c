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
 * KM Management/Control Plane.
 *
 * There is a management thread responsible for listening on a UNIX domain socket
 * for management requests.
 */

#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "km_coredump.h"
#include "km_filesys.h"
#include "km_management.h"
#include "km_snapshot.h"
#include "libkontain_mgmt.h"

static int sock = -1;
static struct sockaddr_un addr = {.sun_family = AF_UNIX};
pthread_t thread;
int kill_thread = 0;
char* km_mgtdir = NULL;

static void* mgt_main(void* arg)
{
   ssize_t br;
   mgmtrequest_t mgmtrequest;
   mgmtreply_t mgmtreply;
   int needunblock;
   ssize_t bw;

   /*
    * First implementation is really dumb. Listen on a socket. When a connect
    * happens, snapshot the guest. This has the advantage of not doing anything
    * about message formats and the like for now.
    */
   while (kill_thread == 0) {
      int nfd = km_mgt_accept(sock, NULL, NULL);
      km_tracex("Connection accepted");
      if (nfd < 0) {
         // Probably fini closed the listening socket, wait for kill_thread to drop us out of the loop.
         continue;
      }

      // Read the request.
      br = recv(nfd, &mgmtrequest, sizeof(mgmtrequest), 0);
      if (br < 0) {
         km_warn("recv mgmt request failed");
         close(nfd);
         continue;
      }
      if (br < 2 * sizeof(int)) {
         km_warnx("mgmt request is too short, %ld bytes", br);
         close(nfd);
         continue;
      }

      needunblock = 0;
      if (km_vcpus_are_started != 0) {
         switch (mgmtrequest.opcode) {
            case KM_MGMT_REQ_SNAPSHOT:
               if ((mgmtreply.request_status = km_snapshot_block(NULL)) == 0) {
                  mgmtreply.request_status =
                      km_snapshot_create(NULL,
                                         mgmtrequest.requests.snapshot_req.label,
                                         mgmtrequest.requests.snapshot_req.description,
                                         mgmtrequest.requests.snapshot_req.snapshot_path);
                  if (mgmtreply.request_status == 0 && mgmtrequest.requests.snapshot_req.live == 0) {
                     machine.exit_group = 1;
                  }
                  needunblock = 1;
               }
               break;
            default:
               km_warnx("Unknown mgmt request %d, length %d", mgmtrequest.opcode, mgmtrequest.length);
               mgmtreply.request_status = EINVAL;
               break;
         }
      } else {
         // The payload has not started, can't do the request.
         // This probably should be on a per request type basis, not all requests.
         mgmtreply.request_status = EAGAIN;
         km_warnx("Payload not running, failing management request %d", mgmtrequest.opcode);
      }

      // let them know what happened.
      bw = send(nfd, &mgmtreply, sizeof(mgmtreply), MSG_NOSIGNAL);
      if (bw != sizeof(mgmtreply)) {
         km_warn("send mgmt reply failed, byteswritten %ld, expected %d", bw, sizeof(mgmtreply));
      }
      close(nfd);
      if (needunblock != 0) {
         // We need to send the reply before potentially shutting down the payload threads.
         km_snapshot_unblock();
      }
      if (mgmtreply.request_status == 0 && mgmtrequest.requests.snapshot_req.live == 0) {
         // Payload threads are terminating, this thread doesn't need to receive any more mgmt requests.
         break;
      }
   }
   return NULL;
}

void km_mgt_fini(void)
{
   kill_thread = 1;
   unlink(addr.sun_path);
   if (sock >= 0) {
      close(sock);
      sock = -1;
   }
   free(km_mgtdir);
   km_mgtdir = NULL;
   return;
}

void km_mgt_init(char* path)
{
   char pipename[128];
   if (km_mgtdir != NULL) {
      /*
       * If KM_MGTDIR is in the environment we ignore the pipe name supplied on the
       * command line or the KM_MGTPIPE env var.  Instead we use self generated names
       * in the directory supplied with KM_MGTDIR.
       */
      struct stat sb;
      int rc = stat(km_mgtdir, &sb);
      if (rc != 0) {
         km_warn("KM_MGTDIR directory <%s> is not accessible", km_mgtdir);
         goto err;
      }
      snprintf(pipename, sizeof(pipename), "%s/kmpipe.%s.%d", km_mgtdir, basename(km_payload_name), getpid());
      path = pipename;
   } else {
      if (path == NULL) {
         return;
      }
   }
   if (strlen(path) + 1 > sizeof(addr.sun_path)) {
      km_errx(2, "mgmt path <%s> too long, max length %d", path, sizeof(addr.sun_path));
   }
   strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

   km_infox(KM_TRACE_SNAPSHOT, "snapshot pipe name is %s", path);

   if ((sock = km_mgt_listen(AF_UNIX, SOCK_STREAM, 0)) < 0) {
      km_warn("mgt socket %s", addr.sun_path);
      goto err;
   }
   if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      km_warn("bind failure: %s", path);
      goto err;
   }
   if (listen(sock, 1) < 0) {
      km_warn("mgt listen %s", addr.sun_path);
      goto err;
   }
   kill_thread = 0;
   if (pthread_create(&thread, NULL, mgt_main, NULL) != 0) {
      km_warn("phtread_create mgt_main() failure");
      goto err;
   }
   // Ensure the mgt pipe is deleted if km calls km_err()
   atexit(km_mgt_fini);
   return;
err:
   km_mgt_fini();
   return;
}
