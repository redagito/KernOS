#pragma once

#include <kernel/types.h>

/*
 * Preemptive round-robin scheduler with a process model (M5.2):
 * slots 0/1/2 are kernel threads (boot thread, workers), 3/4 carry
 * real address spaces (CPL3 userland). All threads are switched from
 * the PIT tick through swtch(); when a switch involves userland, the
 * scheduler also loads the address space (CR3) and points TSS.rsp0 at
 * the kernel stack the gate/IRQ frames should arrive on. Userland
 * exits by syscall 3, which never returns to the zombie's stack.
 *
 * M5.5: the substrate invariant finally pays off - threads can BLOCK
 * inside the syscall gate. The gate runs with IF off on the blocked
 * thread's own kernel stack; the full trapframe sits parked above the
 * swtch save area, so wake+reschedule resumes the gate mid-dispatch
 * and the stub's iretq restores the caller's IF. Two wait queues:
 * keyboard input (woken by the IRQ handler) and child exit (woken by
 * the exit path).
 */

typedef void (*thread_fn)(void *);

/* wait queues (waitq field of a T_WAITING thread) */
#define SCHED_WQ_KBD    0
#define SCHED_WQ_CHILD  1

void sched_init(void);

/* Spawn on a free slot (max 5); returns id or -1. Must run before the
   scheduler preempts (sti lives in main after spawning). */
int  thread_spawn(const char *name, thread_fn entry, void *arg);

/* Spawn a user process from a resident ELF64 image (validated +
   loaded by proc_build_space_elf). Same preemption-free assumptions
   as thread_spawn. The parent is the caller (sched_tid()); that link
   is what SYSCALL_WAIT collects. */
int  thread_spawn_user(const char *name, const void *elf, u64 elf_len);

/* Call from the tick handler (IRQ context, IF=0). */
void schedule(void);

/* Exit the calling thread forever: from a syscall gate (IF off). Marks
   this thread ZOMBIE, remembers (code) in the per-tid exit tables,
   wakes child-waiters, frees this space if any, and switches away -
   never returns. */
void sched_exit_now(u64 exit_code) __attribute__((noreturn));

/* Block the calling thread on wait queue wq until sched_wake_all(wq).
   Caller must run with IF off; on resume IF is STILL OFF (a gate
   caller continues its dispatch, a kernel caller must sti itself). */
void sched_block(int wq);

/* Wake every T_WAITING thread parked on wq (state flip to RUNNABLE;
   wakers re-check their condition). IRQ-safe. */
void sched_wake_all(int wq);

/* Kernel-side join (main.c uses this instead of a syscall because
   kernel threads don't go through the gate): block until threads[tid]
   has exited, return its exit code (-1 if tid was never real). On
   resume IF is re-enabled before returning. */
int  sched_join(int tid);

/* WAIT-syscall support over the per-tid exit tables. */
int  sched_collect_child(u64 *code);   /* 1 + one child's code consumed */
int  sched_any_children(void);         /* any live/uncollected child?   */

/* M5.11: permanent named boot evidence for the ledger's slot-reuse
   discipline - a dead child's uncollected record is forfeited when a
   spawn claims the slot again (wait(2)-style). Prints
   "sched: ledger reuse ok at claim" or a FAIL line; 0 = ok. */
int  sched_ledger_boot_check(void);

u64  sched_switches(void);

/* Thread id of the calling thread (v0 syscall ABI: pid == tid). */
u64  sched_tid(void);

