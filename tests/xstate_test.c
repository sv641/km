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
 * Start a lot of threads
 * wait for sync points so context switch is guaranteed between setting and verifying registers
 *
 * stating a lots of thread with guaranteed context switches,
 * and validates that registeres are properly saved and restored
 *
 * signal test
 * verifies state is saved and restored across signal handler
 */

#define _GNU_SOURCE
#include <cpuid.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#include "greatest/greatest.h"

#define XSTATE_ITER_COUNT (16)

#define MAX_THREADS (32)

#define MAIN_PATTERN (0xF00D)
#define SIGNAL_PATTERN (0xDEAD)

typedef struct {
   int index;
   int failed_count;
   pthread_t thread_id;
   void* thread_exit_value;
} child_t;

typedef struct {
   pthread_mutex_t cv_mutex;
   pthread_cond_t cv;
   atomic_uint thread_started_count;
   atomic_uint thread_set_count;
   atomic_uint thread_verify_count;
   u_int64_t features;
   child_t child[MAX_THREADS];
} test_t;

test_t test;

#define MMX_REG_COUNT (8)
#define XMM_REG_COUNT (16)
typedef struct {
   u_int64_t lo;
   u_int64_t hi;
} xmm_t;

/*
 * intel SDM volume 3A section 2.6 XCR0 bit definition
 */
#define X87 (0x1)
#define SSE (0x2)
#define AVX (0x4)
#define BNDREG (0x8)
#define BNDCSR (0x10)
#define OPMASK (0x20)
#define ZMM_HI256 (0x40)
#define HI16_ZMM (0x80)
/* bit 8 unused */
#define PKRU (0x200)

void get_xtended_features()
{
   u_int32_t eax, edx;
   u_int32_t ecx = 0;

   asm volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(ecx));
   test.features = ((u_int64_t)edx << 32) | eax;
}

u_int64_t get_value(u_int32_t index, u_int32_t value)
{
   return ((u_int64_t)index << 32) | value;
}

void set_xtended_registers(child_t* cptr)
{
   if (test.features & X87) {
      /* Floating point and MMX */
      u_int64_t mmx_values[MMX_REG_COUNT];

      for (int index = 0; index < MMX_REG_COUNT; index++) {
         mmx_values[index] = get_value(cptr->index, index);
      }
      __asm__ volatile("movq (%0), %%mm0\n"
                       "movq 0x8(%0), %%mm1\n"
                       "movq 0x10(%0), %%mm2\n"
                       "movq 0x18(%0), %%mm3\n"
                       "movq 0x20(%0), %%mm4\n"
                       "movq 0x28(%0), %%mm5\n"
                       "movq 0x30(%0), %%mm6\n"
                       "movq 0x38(%0), %%mm7\n"
                       :
                       : "r"(mmx_values));
   } else {
      printf("info: FPU/MMX save/restore is not supported, no need to test\n");
   }

   if (test.features & SSE) {
      /* SSE (XMM registers) */
      xmm_t xmm_values[XMM_REG_COUNT] __attribute__((aligned(16)));
      for (int index = 0; index < XMM_REG_COUNT; index++) {
         xmm_values[index].hi = xmm_values[index].lo = get_value(cptr->index, index);
      }
      __asm__ volatile("movdqa (%0), %%xmm0\n"
                       "movdqa 0x10(%0), %%xmm1\n"
                       "movdqa 0x20(%0), %%xmm2\n"
                       "movdqa 0x30(%0), %%xmm3\n"
                       "movdqa 0x40(%0), %%xmm4\n"
                       "movdqa 0x50(%0), %%xmm5\n"
                       "movdqa 0x60(%0), %%xmm6\n"
                       "movdqa 0x70(%0), %%xmm7\n"
                       "movdqa 0x80(%0), %%xmm8\n"
                       "movdqa 0x90(%0), %%xmm9\n"
                       "movdqa 0xa0(%0), %%xmm10\n"
                       "movdqa 0xb0(%0), %%xmm11\n"
                       "movdqa 0xc0(%0), %%xmm12\n"
                       "movdqa 0xd0(%0), %%xmm13\n"
                       "movdqa 0xe0(%0), %%xmm14\n"
                       "movdqa 0xf0(%0), %%xmm15\n"
                       :
                       : "r"(xmm_values));
   } else {
      printf("info: SSE save/restore is not supported, no need to test\n");
   }
   /* TODO add rest of the features when time permits */
}

void verify_xtended_registers(child_t* cptr)
{
   if (test.features & X87) {
      u_int64_t mmx_values[MMX_REG_COUNT];

      __asm__ volatile("movq %%mm0, (%0)\n"
                       "movq %%mm1, 0x8(%0)\n"
                       "movq %%mm2, 0x10(%0)\n"
                       "movq %%mm3, 0x18(%0)\n"
                       "movq %%mm4, 0x20(%0)\n"
                       "movq %%mm5, 0x28(%0)\n"
                       "movq %%mm6, 0x30(%0)\n"
                       "movq %%mm7, 0x38(%0)\n"
                       :
                       : "r"(mmx_values));
      for (int index = 0; index < MMX_REG_COUNT; index++) {
         u_int64_t expected = get_value(cptr->index, index);
         if (mmx_values[index] != expected) {
            cptr->failed_count++;
         }
      }
   }

   if (test.features & SSE) {
      /* SSE (XMM registers) */
      xmm_t xmm_values[XMM_REG_COUNT] __attribute__((aligned(16)));
      __asm__ volatile("movdqa %%xmm0, (%0)\n"
                       "movdqa %%xmm1, 0x10(%0)\n"
                       "movdqa %%xmm2, 0x20(%0)\n"
                       "movdqa %%xmm3, 0x30(%0)\n"
                       "movdqa %%xmm4, 0x40(%0)\n"
                       "movdqa %%xmm5, 0x50(%0)\n"
                       "movdqa %%xmm6, 0x60(%0)\n"
                       "movdqa %%xmm7, 0x70(%0)\n"
                       "movdqa %%xmm8, 0x80(%0)\n"
                       "movdqa %%xmm9, 0x90(%0)\n"
                       "movdqa %%xmm10, 0xa0(%0)\n"
                       "movdqa %%xmm11, 0xb0(%0)\n"
                       "movdqa %%xmm12, 0xc0(%0)\n"
                       "movdqa %%xmm13, 0xd0(%0)\n"
                       "movdqa %%xmm14, 0xe0(%0)\n"
                       "movdqa %%xmm15, 0xf0(%0)\n"
                       :
                       : "r"(xmm_values));
      for (int index = 0; index < XMM_REG_COUNT; index++) {
         u_int64_t expected = get_value(cptr->index, index);
         if ((xmm_values[index].hi != expected) || (xmm_values[index].lo != expected)) {
            cptr->failed_count++;
         }
      }
   }
}

void synchronize_threads(child_t* cptr, atomic_uint* ptr)
{
   atomic_uint value;

   pthread_mutex_lock(&test.cv_mutex);
   value = atomic_fetch_add(ptr, 1);
   if (value < (MAX_THREADS - 1)) {
      pthread_cond_wait(&test.cv, &test.cv_mutex);
   } else {
      pthread_cond_broadcast(&test.cv);
   }
   pthread_mutex_unlock(&test.cv_mutex);
}

void* child_thread(void* arg)
{
   child_t* cptr = (child_t*)arg;

   /* wait for all threads to start */
   synchronize_threads(cptr, &test.thread_started_count);

   set_xtended_registers(cptr);

   /* wait for all threads to complete register initialization */
   synchronize_threads(cptr, &test.thread_set_count);

   verify_xtended_registers(cptr);

   /* wait for all threads to complete verifcation */
   synchronize_threads(cptr, &test.thread_verify_count);

   return (void*)(u_int64_t)cptr->index;
}

TEST test_context_switch(void)
{
   pthread_mutex_init(&test.cv_mutex, NULL);
   pthread_cond_init(&test.cv, NULL);
   atomic_init(&test.thread_started_count, 0);
   atomic_init(&test.thread_set_count, 0);
   atomic_init(&test.thread_verify_count, 0);

   for (int i = 0; i < MAX_THREADS; i++) {
      int status;
      child_t* cptr = &test.child[i];

      cptr->index = i;
      cptr->failed_count = 0;

      status = pthread_create(&cptr->thread_id, NULL, child_thread, cptr);
      ASSERT_EQ(0, status);
   }

   for (int i = 0; i < MAX_THREADS; i++) {
      int status;
      child_t* cptr = &test.child[i];

      status = pthread_join(cptr->thread_id, &cptr->thread_exit_value);
      ASSERT_EQ(0, status);

      ASSERT_EQ(0, cptr->failed_count);
   }
   PASS();
}

void signal_handler(int signal)
{
   child_t child;

   child.index = SIGNAL_PATTERN;
   child.failed_count = 0;

   set_xtended_registers(&child);
}

TEST test_signal(void)
{
   child_t child;

   child.index = MAIN_PATTERN;
   child.failed_count = 0;

   set_xtended_registers(&child);

   signal(SIGTERM, signal_handler);

   kill(0, SIGTERM);

   verify_xtended_registers(&child);
   ASSERT_EQ(0, child.failed_count);

   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();   // init & parse command-line args

   memset(&test, 0, sizeof(test_t));

   get_xtended_features();

   for (int i = 0; i < XSTATE_ITER_COUNT; i++) {
      RUN_TEST(test_context_switch);
   }

   RUN_TEST(test_signal);

   GREATEST_PRINT_REPORT();
   return greatest_info.failed;   // return count of errors (or 0 if all is good)
}