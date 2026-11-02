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
 */

typedef void (*thread_fn)(void *);

void sched_init(void);

/* Spawn on a free slot (max 5); returns id or -1. Must run before the
   scheduler preempts (sti lives in main after spawning). */
int  thread_spawn(const char *name, thread_fn entry, void *arg);

/* Spawn a user process from a resident ELF64 image (validated +
   loaded by proc_build_space_elf). Same preemption-free assumptions
   as thread_spawn. */
int  thread_spawn_user(const char *name, const void *elf, u64 elf_len);

/* Call from the tick handler (IRQ context, IF=0). */
void schedule(void);

/* Exit the calling thread forever: from a syscall gate (IF off). Marks
   this thread ZOMBIE, picks the next runnable thread, frees this
   space if any, and switches away - never returns. */
void sched_exit_now(void) __attribute__((noreturn));

u64  sched_switches(void);

/* Thread id of the calling thread (v0 syscall ABI: pid == tid). */
u64  sched_tid(void);
