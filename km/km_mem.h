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

#ifndef __KM_MEM_H__
#define __KM_MEM_H__

#include <assert.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <linux/kvm.h>
#include "km.h"
#include "km_proc.h"

#define KM_PAGE_SIZE 0x1000ul   // standard 4k page
static const uint64_t KM_PAGE_MASK = (~(KM_PAGE_SIZE - 1));
static const uint64_t KIB = 0x400ul;        // KByte
static const uint64_t MIB = 0x100000ul;     // MByte
static const uint64_t GIB = 0x40000000ul;   // GByte

static const int RSV_MEM_START = KM_PAGE_SIZE * 32;
static const int RSV_MEM_SIZE = KM_PAGE_SIZE * 32;
/*
 * Mandatory data structures in reserved area to enable 64 bit CPU.
 * These numbers are offsets from the start of reserved area
 */
static const int RSV_IDMAP_OFFSET = RSV_MEM_SIZE;   // next page after reserved area

/*
 * convert the above to guest physical offsets
 */
static inline uint64_t RSV_GUEST_PA(uint64_t x)
{
   return x + RSV_MEM_START;
}

// Special slots in machine.vm_mem_regs[]
static const int KM_RSRV_MEMSLOT = 0;
static const int KM_RSRV_VDSOSLOT = 41;
static const int KM_RSRV_KMGUESTMEM_SLOT = 42;

static const km_gva_t GUEST_MEM_START_VA = 2 * MIB;
static const km_gva_t GUEST_PRIVATE_MEM_START_VA = 512 * GIB;
// ceiling for guest virt. address. 2MB shift down to make it aligned on GB with physical address
#ifdef KM_HIGH_GVA
static const km_gva_t GUEST_MEM_TOP_VA = 128 * 1024 * GIB - GUEST_MEM_START_VA;
#else
static const km_gva_t GUEST_MEM_TOP_VA = (512 * GIB - GUEST_MEM_START_VA);
#endif

static const km_gva_t GUEST_VVAR_VDSO_BASE_VA = GUEST_PRIVATE_MEM_START_VA;
static const km_gpa_t GUEST_VVAR_VDSO_BASE_GPA = 0x7ffff00000;

static const km_gva_t GUEST_KMGUESTMEM_BASE_VA = GUEST_PRIVATE_MEM_START_VA + (32 * KIB);
static const km_gpa_t GUEST_KMGUESTMEM_BASE_GPA = 0x7ffff08000;

/*
 * There are 2 "zones" of VAs, one on the bottom and one on the top. The bottom has pva == gva, the
 * top one is shifted by this offset, i.e gva = pva + GUEST_VA_OFFSET
 */
#define GUEST_MEM_ZONE_SIZE_VA (machine.guest_max_physmem)

#define GUEST_VA_OFFSET (GUEST_MEM_TOP_VA + GUEST_MEM_START_VA - GUEST_MEM_ZONE_SIZE_VA)

// We currently only have code with 1 PML4 entry per "zone", so we can't support more than that
static const uint64_t GUEST_MAX_PHYSMEM_SUPPORTED = 512 * GIB;

/*
 * See "Virtual memory layout:" in km_cpu_init.c for details.
 */
/*
 * Talking about stacks, start refers to the lowest address, top is the highest,
 * in other words start and top are view from the memory regions allocation
 * point of view.
 *
 * RSP register initially is pointed to the top of the stack, and grows down
 * with flow of the program.
 */

static const uint64_t GUEST_STACK_SIZE = 2 * MIB;   // Single thread stack size
static const uint64_t GUEST_ARG_MAX = 32 * KM_PAGE_SIZE;

/*
 * The following exponential memory region sizes are to work around KVM limitations. Turns out large
 * memory regions take a long time to insert into the VM so starting with large memory makes inital
 * start slow. So we start with only 2MB at the bottom and 2MB at the top. On the other hand the
 * number of available physical memory slots is not that large, so we cannot linearly add memory.
 * Hence the exponential approach.
 *
 * Physical address space is made of regions with size exponentially increasing from 2MB until they
 * cross the middle of the space, and then region sizes are exponentially decreasing until they
 * drop to 2MB (last region size). E.g.:
 * // clang-format off
 *  base - end       size  idx   clz  clz(end)
 *   2MB -   4MB      2MB    1    42   n/a
 *   4MB -   8MB      4MB    2    41   ..
 *   8MB -  16MB      8MB    3    40
 *  16MB -  32MB     16MB    4    39
 *     ...
 *  128GB - 256GB    128GB   17   26
 *  256GB - 384GB    128GB   18   n/a  25
 *  384GB - 448GB    64GB    19   n/a  26
 *        ...
 *  510GB - 511GB      1GB   25   n/a  32
 * // clang-format on
 *
 * idx is number of the region, we compute it based on number of leading zeroes
 * in a given address (clz) or in "512GB - address" (clz(end)), using clzl instruction. 'base' is
 * address of the first byte in it. Note size equals base in the first half of the space
 *
 * Memory regions that become guest physical memory are allocated using mmap() with specified
 * address, so that contiguous guest physical memory becomes contiguous in KM space as well.
 *
 * We could randomly choose to start guest memory allocation from something like 0x100000000000,
 * which happens to be 16TB. It has an advantage of being numerically the guest physical addresses
 * with bit 0x100000000000 set, so it is easy to visually convert gva and gpa. km could be regular
 * static, dynamic, or PIE executable. Use -DKM_GPA_AT_16T=1
 *
 * On the other hand we could choose this to be 0x0, in which case gpa actually equals to kma. To
 * avoid collision KM needs to be moved out of the way, of course, which we achieve by making KM a
 * PIE, static or dynamic.
 *
 * Per https://stackoverflow.com/questions/61561331/why-does-linux-favor-0x7f-mappings,
 * Minimum address for ET_DYN, if we take a look at ELF_ET_DYN_BASE, we see that it is architecture
 * dependent and on x86-64 it evaluates to:
 * ((1ULL << 47) - (1 << 12)) / 3 * 2 == 0x555555554aaa
 */
#ifndef KM_GPA_AT_16T
static const km_kma_t KM_USER_MEM_BASE = (void*)0x0;
#else
static const km_kma_t KM_USER_MEM_BASE = (void*)0x100000000000ul;
#endif

/*
 * Knowing memory layout and how pml4 is set, convert between guest virtual address and km address.
 */
// memreg index for an addr in the bottom half of the PA (after that the geometry changes)
static inline int MEM_IDX(km_gva_t addr)
{
   km_assert(addr > 0);   // clzl fails when there are no 1's
   // 43 is "64 - __builtin_clzl(2*MIB)"
   return (43 - __builtin_clzl(addr));
}

// guest virtual address to guest physical address - adjust high gva down
static inline km_gva_t gva_to_gpa_nocheck(km_gva_t gva)
{
   if (gva > GUEST_VA_OFFSET) {
      gva -= GUEST_VA_OFFSET;
   }
   return gva;
}

static inline km_gva_t gva_to_gpa(km_gva_t gva)
{
   // gva must be in the bottom or top "max_physmem", but not in between
   km_assert((GUEST_MEM_START_VA - 1 <= gva && gva < GUEST_MEM_ZONE_SIZE_VA) ||
             (GUEST_VA_OFFSET <= gva && gva <= GUEST_MEM_TOP_VA));
   return gva_to_gpa_nocheck(gva);
}

// helper to convert physical to virtual in the top of VA
static inline km_gva_t gpa_to_upper_gva(uint64_t gpa)
{
   km_assert(GUEST_MEM_START_VA <= gpa && gpa < machine.guest_max_physmem);
   return gpa + GUEST_VA_OFFSET;
}

static inline int gva_to_memreg_idx(km_gva_t addr)
{
   addr = gva_to_gpa(addr);   // adjust for gva in the top part of VA space
   if (addr > machine.guest_mid_physmem) {
      return machine.last_mem_idx - MEM_IDX(machine.guest_max_physmem - addr - 1);
   }
   return MEM_IDX(addr);
}

static inline km_gva_t memreg_top(int idx);   // forward declaration

/* memreg_base() and memreg_top() return guest physical addresses */
static inline uint64_t memreg_base(int idx)
{
   if (idx <= machine.mid_mem_idx) {
      return MIB << idx;
   }
   return machine.guest_max_physmem - memreg_top(machine.last_mem_idx - idx);
}

static inline km_gva_t memreg_top(int idx)
{
   if (idx <= machine.mid_mem_idx) {
      return (MIB << 1) << idx;
   }
   return machine.guest_max_physmem - memreg_base(machine.last_mem_idx - idx);
}

static inline uint64_t memreg_size(int idx)
{
   if (idx <= machine.mid_mem_idx) {
      return MIB << idx;
   }
   return MIB << (machine.last_mem_idx - idx);
}

/*
 * Returns true if gva is in vdso
 */
static inline int km_vdso_gva(km_gva_t gva)
{
   return gva >= GUEST_VVAR_VDSO_BASE_VA && gva < GUEST_VVAR_VDSO_BASE_VA + km_vvar_vdso_size;
}

/*
 * Returns true if gva is in KM guest unikernel
 */
static inline int km_guestmem_gva(km_gva_t gva)
{
   return gva >= GUEST_KMGUESTMEM_BASE_VA &&
          gva < GUEST_KMGUESTMEM_BASE_VA + machine.vm_mem_regs[KM_RSRV_KMGUESTMEM_SLOT].memory_size;
}

/*
 * Translates guest virtual address to km address, assuming the guest address is valid.
 * To be used to make it obvious that gva is *known* to be valid.
 * @param gva Guest virtual address
 * @returns Address in KM
 */
static inline km_kma_t km_gva_to_kma_nocheck(km_gva_t gva)
{
   if (km_vdso_gva(gva) != 0) {
      return (km_kma_t)machine.vm_mem_regs[KM_RSRV_VDSOSLOT].userspace_addr +
             (gva - GUEST_VVAR_VDSO_BASE_VA);
   }
   if (km_guestmem_gva(gva) != 0) {
      return (km_kma_t)machine.vm_mem_regs[KM_RSRV_KMGUESTMEM_SLOT].userspace_addr +
             (gva - GUEST_KMGUESTMEM_BASE_VA);
   }
   return KM_USER_MEM_BASE + gva_to_gpa_nocheck(gva);
}

/*
 * Translates guest virtual address to km address, checking for validity.
 * @param gva Guest virtual address
 * @returns Address in KM. returns NULL if guest VA is invalid.
 *
 * While brk and tbrk are maintained to byte granularity Linux kernel really enforces memory
 * protection to page granularity.There are situations when memory is "donated" into malloc heap
 * (i.e by dynlink), and malloc code treats the whole page as available even as brk points in the
 * middle. So we relax the checks here to allow for that, and behave like Linux kernel
 */
static inline km_kma_t km_gva_to_kma(km_gva_t gva)
{
   if (gva < GUEST_MEM_START_VA || gva >= GUEST_MEM_TOP_VA ||
       (roundup(machine.brk, KM_PAGE_SIZE) <= gva && gva < rounddown(machine.tbrk, KM_PAGE_SIZE) &&
        !(gva >= GUEST_VVAR_VDSO_BASE_VA && gva < GUEST_VVAR_VDSO_BASE_VA + km_vvar_vdso_size) &&
        !(gva >= GUEST_KMGUESTMEM_BASE_VA &&
          gva < GUEST_KMGUESTMEM_BASE_VA + machine.vm_mem_regs[KM_RSRV_KMGUESTMEM_SLOT].memory_size))) {
      return NULL;
   }
   return km_gva_to_kma_nocheck(gva);
}

// if prot_write passed to mprotect, adjusted to prot_read and prot_write to allow for proper mimic
// of linux working of memory permissions (prot_write implies prot_read)
static inline int protection_adjust(int prot)
{
   if ((prot & PROT_WRITE) == PROT_WRITE) {
      prot |= PROT_READ;
   }
   return prot;
}

void km_mem_init(km_machine_init_params_t* params);
void km_mem_fini(void);
void km_guest_page_free(km_gva_t addr, size_t size);
void km_guest_mmap_init(void);
void km_guest_mmap_fini(void);
km_gva_t km_mem_brk(km_gva_t brk);
km_gva_t km_mem_tbrk(km_gva_t tbrk);
km_gva_t km_guest_mmap_simple(size_t stack_size);
km_gva_t km_guest_mmap_simple_monitor(size_t stack_size);
km_gva_t km_guest_mmap(km_gva_t addr, size_t length, int prot, int flags, int fd, off_t offset);
int km_guest_munmap(km_vcpu_t* vcpu, km_gva_t addr, size_t length);
void km_delayed_munmap(km_vcpu_t* vcpu);
km_gva_t km_guest_mremap(km_gva_t old_address, size_t old_size, size_t new_size, int flags, ...);
int km_guest_mprotect(km_gva_t addr, size_t size, int prot);
int km_guest_madvise(km_gva_t addr, size_t size, int advise);
int km_guest_msync(km_gva_t addr, size_t size, int flag);
int km_is_gva_accessable(km_gva_t addr, size_t size, int prot);
int km_monitor_pages_in_guest(km_gva_t gva, size_t size, int protection, char* tag);
void km_mmap_set_recovery_mode(int mode);
void km_mmap_set_filename(km_gva_t base, km_gva_t limit, char* filename);

#endif /* #ifndef __KM_MEM_H__ */
