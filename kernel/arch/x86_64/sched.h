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
 *
 * ============================================================================
 * THE RECORD DOCTRINE (M5.13, normative - round-three review's structural
 * verdict). The wake/park/spawn machinery below has NO LOCKS, and it MUST
 * NOT get any until this document is superseded in writing. Its defense is
 * threefold and every item below is true of sched.c as it stands (each one
 * verifiable at the named site):
 *
 *  1. ONE CPU, IF OFF. Every mutation of a thread record runs on the kernel
 *     stack of the thread that is cur (tick, gate, exit, wake) or on the
 *     constructor's stack during spawn - with IF off (sched.wake_all's
 *     comment names it; sched_block's hlt windows are the only place IF is
 *     open, and no record is mutated there). A second writer arrives only
 *     through a path that first passes rules 2-5.
 *
 *  2. EVERY FIELD HAS OWNERS. For each field of struct thread: who may
 *     write it, when, and what its "absence" value is:
 *
 *       id       - written once at spawn (the record body's first write).
 *       name     - owned per-record: namebuf[17] since M5.12, set_name()
 *                  writes the bound copy. NEVER a pointer to a caller
 *                  frame or a shared static (the prog_name_store lesson:
 *                  one intern rewritten by the next tenant displays every
 *                  prior record as the newest program).
 *       ctx      - the parked thread owns its save slot; written by swtch
 *                  at every switch-out. The exit path parks in a static
 *                  slot because it never resumes.
 *       cr3/     - publish at spawn BEFORE state (they must be complete
 *       frames     for any reader that can pick the slot); at exit they
 *                  are zeroed BEFORE state=T_FREE - a reusable slot never
 *                  carries a live address space.
 *       ppid     - written once at spawn (parent = the caller); read by
 *                  collect + any_children + the reclaim. Never rewritten.
 *       nframes  - user spawn ONLY: initialized to 0 (M5.14, after
 *                  round-four rule audit) BEFORE the frames are handed
 *                  to proc_build_space_elf, so the ledger describes the
 *                  dead space exactly; kernel spawns initialize it too -
 *                  a failed user build leaves the released count behind,
 *                  and init-to-absence is a claim-time rule (rule 2),
 *                  not a success-time one.
 *       waitq    - meaningful ONLY while T_WAITING; spent (0) on EVERY
 *                  resume path - no armed wake without a parked record
 *                  (the M5.10 honesty rule). Armed second at block time:
 *                  no wake can interleave before it (IF off, rule 1).
 *                  PRICED ENCODING NOTE (M5.14 audit): the spent value
 *                  0 IS the id of SCHED_WQ_KBD - safe only because no
 *                  WAITING record can exist unarmed (armed before the
 *                  state flip, spent on every resume); superseding this
 *                  in M6 means giving the spent state its own value
 *                  outside the queue id space, not leaning on the
 *                  structural guarantee.
 *       state    - THE load-bearing field; see rule 3.
 *
 *  3. STATE IS THE LAST WRITE OF EVERY PUBLISH. Spawn publishes the
 *     record body and only then state=T_RUNNABLE (no half-built slot is
 *     schedulable); T_WAITING means "not on any CPU" - so its every
 *     settling ends at a rescan, not a parked-while-tagged shape
 *     (sched_block's rewritten park loop); the exit path sets T_ZOMBIE
 *     while still cur (safe: IF off, this path switches away and never
 *     returns, no switch() picks ZOMBIE), and T_FREE only after the
 *     space is destroyed. Readers pick slots by state; anything they can
 *     pick must satisfy the invariants of rules 2-4.
 *
 *  4. WAKES ARE FLIPS, NOT DISPATCHES. sched_wake_all flips WAITING ->
 *     RUNNABLE, keyed by waitq, and never switches. The woken thread
 *     re-checks its own condition on resume (spurious wakes are the ABI).
 *     This is what lets an IRQ hand wake a record: the flip is the ONLY
 *     write an IRQ path may ever make to a thread record.
 *
 *  5. CROSS-RECORD TABLES ARE KEYED BY TID AND LOCAL TO IT.
 *     exited[]/exit_codes[] are the per-tid exit ledger: publish (exit),
 *     forfeit (ledger_reclaim at any claim - wait(2)-style, M5.11), and
 *     take-once (collect_child, keyed on ppid so a recycled slot's
 *     result can never be adopted by a wrong parent). Nobody indexes
 *     them by anything but the slot's own id.
 *
 *  CHECKLIST FOR ANY M6 WORKER/GUI FIELD (add to the table in rule 2 or
 *  the field does not go in):
 *    a. name its writer(s) - one writer per field per lifetime, or the
 *       field needs the wake/ledger treatment (named here first);
 *    b. its init-to-absence value must precede the state publish;
 *    c. it must not dilute "T_WAITING == not on any CPU";
 *    d. if an IRQ touches it, the touch is rule 4's flip and nothing
 *       else;
 *    e. any new wait queue gets a SCHED_WQ_* name and relies on the
 *       re-check, never a lock;
 *    f. no field is "temporarily" shared across records (cf. rule 2,
 *       name).
 * ============================================================================
 */

typedef void (*thread_fn)(void *);


/* wait queues (waitq field of a T_WAITING thread) */
#define SCHED_WQ_KBD    0
#define SCHED_WQ_CHILD  1
/* M6.5 commit 1: the unfocused reader's park. The read gate blocks
   here BEFORE popping anything, and EVERY focus transition (claim,
   re-home, owner exit) wakes the queue - readers re-check the
   allow-question at their loop head per doctrine rule e; nobody
   trusts a wake, and a mid-park owner change cannot land a byte in
   the wrong reader's hands. */
#define SCHED_WQ_FOCUS  2

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

