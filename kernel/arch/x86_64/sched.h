#pragma once

#include <kernel/types.h>

/*
 * Preemptive round-robin scheduler (M2.4): kernel-space threads only,
 * no user pages, no per-thread address space. Threads are switched
 * from the PIT tick through swtch(): each thread's stack carries its
 * whole state (C frames + saved context +, when preempted, the IRQ
 * frame irq_common pushed), so a resume simply unwinds that stack and
 * iretq's back into the thread's own rip. Thread 0 is kernel_main.
 */

typedef void (*thread_fn)(void *);

void sched_init(void);

/* Spawn on a free slot (up to 3); returns id or -1. Must run before
   the scheduler ever preempts ( sti lives in main after spawning). */
int  thread_spawn(const char *name, thread_fn entry, void *arg);

/* Call from the tick handler (IRQ context, IF=0). Switches to the
   next runnable thread; no-op if only one exists. */
void schedule(void);

u64  sched_switches(void);
