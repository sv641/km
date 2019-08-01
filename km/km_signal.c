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
 *
 * Signal-related wrappers for KM threads/KVM vcpu runs.
 */

#define _GNU_SOURCE
#include <err.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>

#include "bsd_queue.h"
#include "km.h"
#include "km_coredump.h"
#include "km_hcalls.h"
#include "km_mem.h"
#include "km_signal.h"

void km_install_sighandler(int signum, sa_action_t func)
{
   struct sigaction sa = {.sa_sigaction = func, .sa_flags = SA_SIGINFO};

   sigemptyset(&sa.sa_mask);
   if (sigaction(signum, &sa, NULL) < 0) {
      err(1, "Failed to set handler for signal %d", signum);
   }
}

/*
 * Make current thread block until signal is sent to it via pthread_kill().
 * Note that for the thread to not die on the signal it also needs
 * to block it, so to use in multi-threaded code other threads (or parent)
 * needs to block it or handle it too.
 */
void km_wait_for_signal(int signum)
{
   sigset_t signal_set;
   sigset_t old_signal_set;
   int received_signal;

   sigemptyset(&signal_set);
   sigaddset(&signal_set, signum);
   pthread_sigmask(SIG_BLOCK, &signal_set, &old_signal_set);
   sigwait(&signal_set, &received_signal);
   pthread_sigmask(SIG_SETMASK, &old_signal_set, NULL);
}

#define NSIGENTRY 8
static km_signal_t signal_entries[NSIGENTRY];

/*
 * Signal classification sets.
 * Source: https://www.gnu.org/software/libc/manual/html_node/Standard-Signals.html#Standard-Signals
 *
 * Note: most of these sigsets aren't used by the code yet, but I'm pretty sure
 *       they will come in handy later.
 */
static km_sigset_t perror_signals = 0;   // Program errors
static km_sigset_t term_signals = 0;
static km_sigset_t alarm_signals = 0;
static km_sigset_t aio_signals = 0;
static km_sigset_t jc_signals = 0;
static km_sigset_t oerror_signals = 0;
static km_sigset_t misc_signals = 0;

/*
 * signals that ignore blocking if generated by CPU fault
 * (ie. si_code == SI_KERNEL).
 * See NOTES in 'man 2 sigprocmask'. Man page says behavior is undefined.
 * We will define it to mean 'we don't ignore'.
 */
static km_sigset_t ign_block_signals = 0;
static km_sigset_t no_catch_signals = 0;   // signals that can't be caught, blocked, or ignored
static km_sigset_t def_ign_signals = 0;    // default ignore signals

void km_signal_init(void)
{
   for (int i = 0; i < NSIGENTRY; i++) {
      TAILQ_INSERT_TAIL(&machine.sigfree.head, &signal_entries[i], link);
   }

   // program error signals
   km_sigemptyset(&perror_signals);
   km_sigaddset(&perror_signals, SIGFPE);
   km_sigaddset(&perror_signals, SIGILL);
   km_sigaddset(&perror_signals, SIGSEGV);
   km_sigaddset(&perror_signals, SIGBUS);
   km_sigaddset(&perror_signals, SIGABRT);
   km_sigaddset(&perror_signals, SIGIOT);
   km_sigaddset(&perror_signals, SIGTRAP);
   km_sigaddset(&perror_signals, SIGSYS);

   // terminaltion signals
   km_sigemptyset(&term_signals);
   km_sigaddset(&term_signals, SIGTERM);
   km_sigaddset(&term_signals, SIGINT);
   km_sigaddset(&term_signals, SIGQUIT);   // Special case. Drops a core
   km_sigaddset(&term_signals, SIGKILL);
   km_sigaddset(&term_signals, SIGHUP);

   // alarm signals
   km_sigemptyset(&alarm_signals);
   km_sigaddset(&alarm_signals, SIGALRM);
   km_sigaddset(&alarm_signals, SIGVTALRM);
   km_sigaddset(&alarm_signals, SIGPROF);

   // async IO signals
   km_sigemptyset(&aio_signals);
   km_sigaddset(&aio_signals, SIGIO);
   km_sigaddset(&aio_signals, SIGURG);
   km_sigaddset(&aio_signals, SIGPOLL);

   // Job Control signals
   km_sigemptyset(&jc_signals);
   km_sigaddset(&jc_signals, SIGCHLD);
   km_sigaddset(&jc_signals, SIGCONT);
   km_sigaddset(&jc_signals, SIGSTOP);
   km_sigaddset(&jc_signals, SIGTTIN);
   km_sigaddset(&jc_signals, SIGTTOU);

   // Operation Error km_signals
   km_sigemptyset(&oerror_signals);
   km_sigaddset(&oerror_signals, SIGPIPE);
   km_sigaddset(&oerror_signals, SIGXCPU);
   km_sigaddset(&oerror_signals, SIGXFSZ);

   // Miscellaneous km_signals
   km_sigemptyset(&misc_signals);
   km_sigaddset(&misc_signals, SIGUSR1);
   km_sigaddset(&misc_signals, SIGUSR2);
   km_sigaddset(&misc_signals, SIGWINCH);

   km_sigemptyset(&ign_block_signals);
   km_sigaddset(&ign_block_signals, SIGBUS);
   km_sigaddset(&ign_block_signals, SIGFPE);
   km_sigaddset(&ign_block_signals, SIGILL);
   km_sigaddset(&ign_block_signals, SIGSEGV);
   /*
    * signals that cannot be caught, blocked, or ignored
    */
   km_sigemptyset(&no_catch_signals);
   km_sigaddset(&no_catch_signals, SIGKILL);
   km_sigaddset(&no_catch_signals, SIGSTOP);

   /*
    * Signals whose default action is ignore.
    * Source: http://man7.org/linux/man-pages/man7/signal.7.html
    */
   km_sigemptyset(&def_ign_signals);
   km_sigaddset(&def_ign_signals, SIGCHLD);
   km_sigaddset(&def_ign_signals, SIGURG);
   km_sigaddset(&def_ign_signals, SIGWINCH);
}

void km_signal_fini(void)
{
}

static inline void enqueue_signal(km_signal_list_t* slist, siginfo_t* info)
{
   km_signal_t* sig;

   km_signal_lock();
   if ((sig = TAILQ_FIRST(&machine.sigfree.head)) == NULL) {
      km_signal_unlock();
      err(1, "No free signal entries");
   }
   TAILQ_REMOVE(&machine.sigfree.head, sig, link);
   sig->info = *info;
   TAILQ_INSERT_TAIL(&slist->head, sig, link);
   km_signal_unlock();
}

static inline int sigpri(int signo)
{
   // program error signals come first
   if (km_sigismember(&perror_signals, signo) != 0) {
      return 0;
   }
   return -signo;
}

static inline int dequeue_signal(km_signal_list_t* slist, km_sigset_t* blocked, siginfo_t* info)
{
   int rc = 0;
   km_signal_t* chosen = NULL;
   km_signal_t* sig = NULL;

   km_signal_lock();
   TAILQ_FOREACH (sig, &slist->head, link) {
      if (km_sigismember(blocked, sig->info.si_signo) != 0) {
         /*
          * If this signal was generated by CPU fault and it is one of the
          * SIGILL, SIGFPE, SIGSEGV, or SIGBUS, then ignore the mask and
          * process it now.
          */
         if ((km_sigismember(&ign_block_signals, sig->info.si_signo) == 0) ||
             (sig->info.si_code != SI_KERNEL)) {
            continue;
         }
      }
      if (chosen == NULL || sigpri(sig->info.si_signo) > sigpri(chosen->info.si_signo)) {
         chosen = sig;
      }
   }
   if (chosen != NULL) {
      TAILQ_REMOVE(&slist->head, chosen, link);
      *info = chosen->info;
      rc = 1;
      TAILQ_INSERT_TAIL(&machine.sigfree.head, chosen, link);
   }
   km_signal_unlock();
   return rc;
}

static inline void get_pending_signals(km_vcpu_t* vcpu, km_sigset_t* set)
{
   km_signal_t* sig;

   km_sigemptyset(set);
   km_signal_lock();
   TAILQ_FOREACH (sig, &vcpu->sigpending.head, link) {
      km_sigaddset(set, sig->info.si_signo);
   }
   TAILQ_FOREACH (sig, &machine.sigpending.head, link) {
      km_sigaddset(set, sig->info.si_signo);
   }
   km_signal_unlock();
}

static inline int next_signal(km_vcpu_t* vcpu, siginfo_t* info)
{
   if (dequeue_signal(&vcpu->sigpending, &vcpu->sigmask, info) != 0) {
      return 1;
   }
   if (dequeue_signal(&machine.sigpending, &vcpu->sigmask, info) != 0) {
      return 1;
   }
   return 0;
}

/*
 * Determine whether this signal already pending.
 */
static inline int signal_pending(km_vcpu_t* vcpu, siginfo_t* info)
{
   km_signal_t* sig;

   km_signal_lock();
   if (vcpu != NULL) {
      TAILQ_FOREACH (sig, &vcpu->sigpending.head, link) {
         if (sig->info.si_signo == info->si_signo) {
            km_signal_unlock();
            return 1;
         }
      }
   }
   TAILQ_FOREACH (sig, &machine.sigpending.head, link) {
      if (sig->info.si_signo == info->si_signo) {
         km_signal_unlock();
         return 1;
      }
   }
   km_signal_unlock();
   return 0;
}

int km_signal_ready(km_vcpu_t* vcpu)
{
   km_signal_t* sig;
   km_signal_t* next_sig;

   km_signal_lock();
   TAILQ_FOREACH (sig, &vcpu->sigpending.head, link) {
      if (km_sigismember(&vcpu->sigmask, sig->info.si_signo) == 0) {
         km_signal_unlock();
         return sig->info.si_signo;
      }
   }
   TAILQ_FOREACH_SAFE (sig, &machine.sigpending.head, link, next_sig) {
      if (km_sigismember(&vcpu->sigmask, sig->info.si_signo) == 0) {
         // A process-wide signal can only be claimed by one thread.
         TAILQ_REMOVE(&machine.sigpending.head, sig, link);
         TAILQ_INSERT_TAIL(&vcpu->sigpending.head, sig, link);
         km_signal_unlock();
         return sig->info.si_signo;
      }
   }
   km_signal_unlock();
   return 0;
}

void km_post_signal(km_vcpu_t* vcpu, siginfo_t* info)
{
   /*
    * non-RT signals are consolidated in while pending.
    */
   if (info->si_signo < SIGRTMIN && signal_pending(vcpu, info)) {
      return;
   }
   if (vcpu == 0) {
      enqueue_signal(&machine.sigpending, info);
      return;
   }
   enqueue_signal(&vcpu->sigpending, info);
   pthread_kill(vcpu->vcpu_thread, KM_SIGVCPUSTOP);
}

/*
 * Signal handler caller stack frame. This is what RSP points at when a guest signal
 * handler is started.
 */
typedef struct km_signal_frame {
   uint64_t return_addr;   // return address for guest handler. See runtime/x86_sigaction.s
   km_hc_args_t hcargs;    // HC argument array for __km_sigreturn.
   kvm_regs_t regs;        // Saved registers
   siginfo_t info;         // Passed to guest signal handler
   ucontext_t ucontext;    // Passed to guest signal handler
} km_signal_frame_t;

#define RED_ZONE (128)

/*
 * Do the dirty-work to get a signal handler called in the guest.
 */
static inline void do_guest_handler(km_vcpu_t* vcpu, siginfo_t* info, km_sigaction_t* act)
{
   km_read_registers(vcpu);

   km_gva_t sframe_gva = vcpu->regs.rsp - RED_ZONE - sizeof(km_signal_frame_t);
   km_signal_frame_t* frame = km_gva_to_kma_nocheck(sframe_gva);

   frame->info = *info;
   frame->regs = vcpu->regs;
   frame->return_addr = km_guest.km_sigreturn;
   frame->ucontext.uc_mcontext.gregs[REG_RIP] = vcpu->regs.rip;
   memcpy(&frame->ucontext.uc_sigmask, &vcpu->sigmask, sizeof(vcpu->sigmask));
   if ((act->sa_flags & SA_SIGINFO) != 0) {
      vcpu->sigmask |= act->sa_mask;
   }
   // Defer this signal.
   km_sigaddset(&vcpu->sigmask, info->si_signo);

   vcpu->regs.rsp = sframe_gva;
   vcpu->regs.rip = act->handler;
   vcpu->regs.rdi = info->si_signo;
   vcpu->regs.rsi = sframe_gva + offsetof(km_signal_frame_t, info);
   vcpu->regs.rdx = sframe_gva + offsetof(km_signal_frame_t, ucontext);

   km_write_registers(vcpu);
}

/*
 * Deliver signal to guest. What delivery looks like depends on the signal disposition.
 * Ignored signals are ignored.
 * Default handler signals typically terminate, possobly with a core,
 * Handled signals result in the guest being setup to run the signal handler
 * on the next VM_RUN call.
 */
void km_deliver_signal(km_vcpu_t* vcpu)
{
   siginfo_t info;

   if (!next_signal(vcpu, &info)) {
      return;
   }

   km_sigaction_t* act = &machine.sigactions[km_sigindex(info.si_signo)];
   if (act->handler == (km_gva_t)SIG_IGN) {
      return;
   }
   if (act->handler == (km_gva_t)SIG_DFL) {
      if (km_sigismember(&def_ign_signals, info.si_signo != 0)) {
         return;
      }

      // Signals that get here terminate the process. The only question is: core or no core?
      int core_dumped = 0;
      assert(info.si_signo != SIGCHLD);   // KM does not support
      vcpu->is_paused = 1;
      machine.pause_requested = 1;
      km_vcpu_apply_all(km_vcpu_pause, 0);
      km_vcpu_wait_for_all_to_pause();
      if ((km_sigismember(&perror_signals, info.si_signo) != 0) || (info.si_signo == SIGQUIT)) {
         extern int debug_dump_on_err;
         km_dump_core(vcpu, NULL);
         if (debug_dump_on_err) {
            abort();
         }
         core_dumped = 1;
      }
      errx(info.si_signo, "guest: %s %s", strsignal(info.si_signo), (core_dumped) ? "(core dumped)" : "");
   }

   assert(act->handler != (km_gva_t)SIG_IGN);
   do_guest_handler(vcpu, &info, act);
}

void km_rt_sigreturn(km_vcpu_t* vcpu)
{
   km_read_registers(vcpu);
   /*
    * Return from handle moved rsp past the return address. Subtract size of
    * the address to account to it.
    * TODO: ensure everything we are copying from is really in guest memory, Don't want to
    *       leak information into guest.
    */
   km_signal_frame_t* frame = km_gva_to_kma_nocheck(vcpu->regs.rsp - sizeof(km_gva_t));
   memcpy(&vcpu->sigmask, &frame->ucontext.uc_sigmask, sizeof(vcpu->sigmask));
   vcpu->regs = frame->regs;
   vcpu->regs.rip = frame->ucontext.uc_mcontext.gregs[REG_RIP];
   km_write_registers(vcpu);
}

uint64_t
km_rt_sigprocmask(km_vcpu_t* vcpu, int how, km_sigset_t* set, km_sigset_t* oldset, size_t sigsetsize)
{
   if (sigsetsize != sizeof(km_sigset_t)) {
      return -EINVAL;
   }
   /*
    * No locking required here because this syscall will only change the mask for the current
    * thread. Single threaded by nature.
    */
   if (oldset != NULL) {
      *oldset = vcpu->sigmask;
   }
   if (set != NULL) {
      switch (how) {
         case SIG_BLOCK:
            for (int signo = 1; signo < _NSIG; signo++) {
               if (km_sigismember(set, signo) != 0) {
                  km_sigaddset(&vcpu->sigmask, signo);
               }
            }
            break;
         case SIG_UNBLOCK:
            for (int signo = 1; signo < _NSIG; signo++) {
               if (km_sigismember(set, signo) != 0) {
                  km_sigdelset(&vcpu->sigmask, signo);
               }
            }
            break;
         case SIG_SETMASK:
            vcpu->sigmask = *set;
            break;
         default:
            return -EINVAL;
      }
   }
   return 0;
}

uint64_t
km_rt_sigaction(km_vcpu_t* vcpu, int signo, km_sigaction_t* act, km_sigaction_t* oldact, size_t sigsetsize)
{
   if (sigsetsize != sizeof(km_sigset_t)) {
      return -EINVAL;
   }
   if (signo < 1 || signo >= NSIG) {
      return -EINVAL;
   }
   if (km_sigismember(&no_catch_signals, signo) != 0) {
      return -EINVAL;
   }
   /*
    * sigactions are process-wide, so need the lock.
    */
   km_signal_lock();
   if (oldact != NULL) {
      *oldact = machine.sigactions[km_sigindex(signo)];
   }
   if (act != NULL) {
      machine.sigactions[km_sigindex(signo)] = *act;
      if (act->handler == (km_gva_t)SIG_IGN) {
         // TODO: Purge pending signals on ignore.
      }
   }
   km_signal_unlock();
   return 0;
}

uint64_t km_kill(km_vcpu_t* vcpu, pid_t pid, int signo)
{
   if (pid != 0) {
      return -EINVAL;
   }
   if (signo < 1 || signo >= NSIG) {
      return -EINVAL;
   }

   // Process-wide signal.
   siginfo_t info = {.si_signo = signo, .si_code = SI_USER};
   km_post_signal(NULL, &info);
   return 0;
}

uint64_t km_tkill(km_vcpu_t* vcpu, pid_t tid, int signo)
{
   km_vcpu_t* target_vcpu = km_vcpu_fetch_by_tid(tid);

   if (target_vcpu == NULL || signo < 1 || signo >= NSIG) {
      return -EINVAL;
   }

   // Thread-targeted signal.
   siginfo_t info = {.si_signo = signo, .si_code = SI_USER};
   km_post_signal(target_vcpu, &info);
   return 0;
}

uint64_t km_rt_sigpending(km_vcpu_t* vcpu, km_sigset_t* set, size_t sigsetsize)
{
   if (sigsetsize != sizeof(km_sigset_t)) {
      return -EINVAL;
   }
   get_pending_signals(vcpu, set);
   return 0;
}