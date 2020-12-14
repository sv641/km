/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
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
 * Start a thread and then send it a cancellation request.
 * Two cases - PTHREAD_CANCEL_DISABLE and PTHREAD_CANCEL_ENABLE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#include "greatest/greatest.h"

#define handle_error_en(en, msg)                                                                   \
   do {                                                                                            \
      errno = en;                                                                                  \
      perror(msg);                                                                                 \
      exit(EXIT_FAILURE);                                                                          \
   } while (0)

struct timeval start;

void print_msg(char* m)
{
   struct timeval now;
   long d;

   if (greatest_get_verbosity() != 0) {
      gettimeofday(&now, NULL);
      d = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_usec - start.tv_usec) / 1000;
      printf("%ld.%03ld: %s", d / 1000, d % 1000, m);
   }
}

struct {
   pthread_mutex_t child_cancel_set_mutex;
   pthread_cond_t child_cancel_set_cv;

   pthread_mutex_t send_cancel_mutex;
   pthread_cond_t send_cancel_cv;

   bool test_result;
} ptc_test;

typedef enum {
	DISABLE_CANCEL_TEST = 0,
	ASYNC_CANCEL_TEST,
	DEFERRED_CANCEL_TEST
} test_type_t;

static long pthread_cancel_child(test_type_t arg)
{
   int s;

   if (arg == DISABLE_CANCEL_TEST) {
      s = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
      ASSERT_EQ(0, s);
   } else {
      s = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
      ASSERT_EQ(0, s);

      s = pthread_setcanceltype(arg == DEFERRED_CANCEL_TEST ? PTHREAD_CANCEL_DEFERRED
                                                            : PTHREAD_CANCEL_ASYNCHRONOUS,
                                NULL);
      ASSERT_EQ(0, s);

      if (arg == ASYNC_CANCEL_TEST) {
         ptc_test.test_result = true;
      }
   }

   print_msg("pthread_cancel_child(): Completed setting cancel state and type.\n");

   /*
    * notify parent, child started with desired cancel state
    */
   pthread_mutex_lock(&ptc_test.child_cancel_set_mutex);
   pthread_cond_broadcast(&ptc_test.child_cancel_set_cv);
   pthread_mutex_unlock(&ptc_test.child_cancel_set_mutex);

   if (arg == DISABLE_CANCEL_TEST) {
      /*
       * wait for parent to send cancel
       */
      pthread_mutex_lock(&ptc_test.send_cancel_mutex);
      pthread_cond_wait(&ptc_test.send_cancel_cv, &ptc_test.send_cancel_mutex);
      pthread_mutex_unlock(&ptc_test.send_cancel_mutex);

      print_msg("pthread_cancel_child(): Parent sent cancel\n");

      s = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
      ASSERT_EQ(0, s);

      ptc_test.test_result = true;

      /*
       * cancellation point, should not return from this call
       */
      sleep(2);

      ptc_test.test_result = false;
   } else if (arg == ASYNC_CANCEL_TEST) {
      /*
       * async is set cancel can be any time
       * test pass as long as does not reach timeout
       * and pthread join returns PTHREAD_CANCELED
       */
   } else if (arg == DEFERRED_CANCEL_TEST) {
      /*
       * wait for parent to send cancel
       * pthread_cond_wait is cancellation point
       */
      pthread_mutex_lock(&ptc_test.send_cancel_mutex);
      pthread_cond_wait(&ptc_test.send_cancel_cv, &ptc_test.send_cancel_mutex);
      pthread_mutex_unlock(&ptc_test.send_cancel_mutex);

      /*
       * no cancellations points should be here
       * no log messages
       */

      ptc_test.test_result = true;

      /*
       * cancellation point, should not return from this call
       */
      sleep(2);

      ptc_test.test_result = false;
   } else {
	   ASSERT(0);
   }

   print_msg("pthread_cancel_child(): before usleep\n");

   while (1) {
      usleep(1000);
   }

   if (arg == ASYNC_CANCEL_TEST) {
      ptc_test.test_result = false;
   }

   pthread_exit((void*)0x17);   // Should never get here - 0x17 is a marker that we did get here
}

TEST pthread_cancel_test(test_type_t test_type)
{
   pthread_t thr;
   void* res;
   int s;

   switch(test_type) {
	   case DISABLE_CANCEL_TEST:
		   print_msg("Starting DISABLE_CANCEL_TEST\n");
		   break;
	   case ASYNC_CANCEL_TEST:
		   print_msg("Starting ASYNC_CANCEL_TEST\n");
		   break;
	   case DEFERRED_CANCEL_TEST:
		   print_msg("Starting DEFERRED_CANCEL_TEST\n");
		   break;
   }

   pthread_mutex_init(&ptc_test.child_cancel_set_mutex, NULL);
   pthread_cond_init(&ptc_test.child_cancel_set_cv, NULL);

   pthread_mutex_init(&ptc_test.send_cancel_mutex, NULL);
   pthread_cond_init(&ptc_test.send_cancel_cv, NULL);

   ptc_test.test_result = false;

   gettimeofday(&start, NULL);

   /* Start a thread and then send it a cancellation request while it is canceldisable */

   if ((s = pthread_create(&thr, NULL, (void* (*)(void*)) & pthread_cancel_child, (void *)test_type)) != 0) {
      handle_error_en(s, "pthread_create");
   }

   /*
    * wait for child to set proper cancel disposition
    */
   pthread_mutex_lock(&ptc_test.child_cancel_set_mutex);
   pthread_cond_wait(&ptc_test.child_cancel_set_cv, &ptc_test.child_cancel_set_mutex);
   pthread_mutex_unlock(&ptc_test.child_cancel_set_mutex);

   print_msg("pthread_cancel_test(): Child thread started with correct cancel state\n");

   s = pthread_cancel(thr);
   ASSERT(s == 0 || s == ESRCH);

   if ((test_type == DISABLE_CANCEL_TEST) || (test_type == DEFERRED_CANCEL_TEST)) {
      /*
       * inform child cancel is done
       */
      pthread_mutex_lock(&ptc_test.send_cancel_mutex);
      pthread_cond_broadcast(&ptc_test.send_cancel_cv);
      pthread_mutex_unlock(&ptc_test.send_cancel_mutex);
   }

   print_msg("pthread_cancel_test(): Waiting for Child status\n");

   /*
    * Collect child thread exit status
    */
   s = pthread_join(thr, &res);
   ASSERT_EQ(0, s);
   ASSERT_EQ_FMT(PTHREAD_CANCELED, res, "%p");

   ASSERT_EQ(true, ptc_test.test_result);

   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();   // init & parse command-line args

   RUN_TESTp(pthread_cancel_test, DISABLE_CANCEL_TEST);
   RUN_TESTp(pthread_cancel_test, ASYNC_CANCEL_TEST);
   RUN_TESTp(pthread_cancel_test, DEFERRED_CANCEL_TEST);

   GREATEST_PRINT_REPORT();
   return greatest_info.failed;   // return count of errors (or 0 if all is good)
}
