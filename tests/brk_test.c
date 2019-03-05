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
 * Test brk() with different values
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "syscall.h"

static void const* __39_bit_mem = (void*)(512 * 0x40000000ul);   // 512GB

static void const* high_addr = (void*)0x30000000ul;   // 768 MB
static void const* very_high_addr = (void*)(512 * 0x40000000ul);

void* SYS_break(void const* addr)
{
   return (void*)syscall(SYS_brk, addr);
}

int main()
{
   void *ptr, *ptr1;

   printf("break is %p\n", ptr = SYS_break(NULL));
   SYS_break(high_addr);
   printf("break is %p, expected %p\n", ptr1 = SYS_break(NULL), high_addr);
   assert(ptr1 == high_addr);

   ptr1 -= 20;
   strcpy(ptr1, "Hello, world");
   printf("%s from far up the memory %p\n", (char*)ptr1, ptr1);

   if (SYS_break(very_high_addr) != very_high_addr) {
      perror("Unable to set brk that high (expected)");
      assert(very_high_addr >= __39_bit_mem);
   } else {
      printf("break is %p\n", ptr1 = SYS_break(NULL));

      ptr1 -= 20;
      strcpy(ptr1, "Hello, world");
      printf("%s from even farer up the memory %p\n", (char*)ptr1, ptr1);

      assert(very_high_addr < __39_bit_mem);
   }

   SYS_break((void*)ptr);
   assert(ptr == SYS_break(NULL));
   printf("break is %p , expected %p\n", SYS_break(NULL), ptr);

   exit(0);
}