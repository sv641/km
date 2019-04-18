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
 *
 * Simple test to validate exit_grp - starts PCOUNT threads (half slow, half fast), join fast and
 * then do exit()
 */

#include <err.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "greatest/greatest.h"

#define PCOUNT 10   // total count of threads to start
#define EXIT_GRP_GENERATE_DEADLOCK 1

// run thread. arg defines how soon the thread will exit (larger arg - longer life)
void* run_thr(void* arg)
{
   uint64_t ret;
   for (int i = 0; i < 1024 * 1024 * 5 * (uint64_t)arg; i++) {
      volatile int r = 1024 * rand();
      r /= 22;
   }
   ret = rand();
#ifdef EXIT_GRP_GENERATE_DEADLOCK
   /*
    * This will generate potential deadlock when the main() thread would wait on this one to join,
    * and this one will be waiting on main thread to confirm pause() ater handling exit grp.
    * So enabling this checks this deadlock prevention.
    */
   if (ret % (PCOUNT) == 0) {
      printf("Exit(11) from from a non-main thread\n");
      exit(11);   // check that we can exit from here too. main() will be running !
   }
#endif
   pthread_exit((void*)ret);
}

// let's use a couple of TESTS, just to have more that 1 test in the file
static pthread_t thr[PCOUNT];

TEST tests_in_flight(void)
{
   for (int i = 0; i < PCOUNT - 1; i++) {
      char buf[64];
      int ret = pthread_create(&thr[i], NULL, run_thr, (void*)(i % 2 == 0 ? 1ul : 4096ul * 1024));   // odd run longer
      sprintf(buf, "Started %d (0x%lx)", i, thr[i]);
      ASSERT_EQm(buf, 0, ret);
      if (greatest_get_verbosity() > 0) {
         printf("%s\n", buf);
      }
   }
   PASSm("threads started\n");
}

TEST wait_for_some(void)
{
   void* retval;
   for (int i = 0; i < PCOUNT - 1; i += 2) {
      char buf[64];
      sprintf(buf, "Join %d (0x%lx)", i, thr[i]);
      int ret = pthread_join(thr[i], &retval);
      ASSERT_EQm(buf, 0, ret);
      if (greatest_get_verbosity() > 0) {
         printf("%s\n", buf);
      }
   }
   PASSm("joined and checked\n");
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();
   RUN_TEST(tests_in_flight);
   RUN_TEST(wait_for_some);
   GREATEST_PRINT_REPORT();
   printf("Exit(17) from main\n");
   exit(17);   // grp exit with some pre-defined code
}