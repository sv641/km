/*
 * Copyright © 2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

/*
 * Simple test to exercise setitimer() and getitimer().
 */

#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

void signal_itimer_real(int signo, siginfo_t* sinfo, void* ucontext)
{
   char message[] = "In SIGALRM signal handler\n";
   write(1, message, strlen(message));
}

int main(int argc, char* argv[])
{
   // Setup signal handler.
   struct sigaction sa;
   sa.sa_sigaction = signal_itimer_real;
   sa.sa_flags = SA_SIGINFO;
   sigemptyset(&sa.sa_mask);
   if (sigaction(SIGALRM, &sa, NULL) < 0) {
      fprintf(stderr, "sigaction( SIGALRM ) failed, %s\n", strerror(errno));
      return 1;
   }

   // Setup and ITIMER_REAL interval timer
   struct itimerval new = {
       { 0, 0 },
       { 0, 20000 }
   };
   if (setitimer(ITIMER_REAL, &new, NULL) < 0) {
      fprintf(stderr, "setitimer( ITIMER_REAL ) failed, %s\n", strerror(errno));
      return 1;
   }

   // Read back the interval timer
   struct itimerval current;
   if (getitimer(ITIMER_REAL, &current) < 0) {
      fprintf(stderr, "getitimer( ITIMER_REAL ) failed, %s\n", strerror(errno));
      return 1;
   }
   fprintf(stdout, "it_interval = { %ld.%06ld sec }, it_value = { %ld.%06ld sec }\n",
           current.it_interval.tv_sec, current.it_interval.tv_usec,
           current.it_value.tv_sec, current.it_value.tv_usec);

   // wait for the timer to fire
   sigset_t blockme;
   sigemptyset(&blockme);
   sigsuspend(&blockme);
   return 0;
}