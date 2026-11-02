#pragma once

#include <kernel/types.h>

/*
 * Process model (M5.2): first real address spaces.
 *
 * Layout decision (frozen): user code+data lives at 0x400000 inside the
 * identity 2-MiB slot i2=2; the user stack (16 KiB) ends at 0x600000,
 * the top of the same slot. The per-process CR3 reuses the boot
 * hierarchy (clone PML4 -> clone PDPT -> clone PD) with one patch: PD
 * slot 2 turns the 2-MiB identity leaf into a PT of 4-KiB user pages.
 * Everything outside that window stays supervisor-only, so CPL3 code
 * can touch exactly what this module mapped: its code pages and its
 * stack - which is the whole point of per-process spaces.
 *
 * Known debt (documented, do not "fix" silently):
 *  - demand-region page tables (>= 0x40000000) are shared with the
 *    boot space via the cloned PML4; a user process never uses them
 *    (U bits absent at every level), but a post-fork demand allocation
 *    in the shared region would fault on some CR3s. No fork yet; no
 *    user demand usage ever; revisit only when forking exists.
 *  - user->kernel switches are only made through the scheduler paths
 *    in sched.c; nothing else may load a process CR3.
 */

#define USER_ENTRY_VA   0x400000ULL   /* entry = first byte of code     */
#define USER_STACK_VA   0x600000ULL   /* stack top (grows down)         */
#define USER_STACK_SZ   (16 * 1024)
#define USER_MAX_BLOB   8192          /* code pages budget: 2 frames    */

/* Frame ledger cap: 4 tables + 2 code pages + 4 stack pages. */
#define MAX_PFRAMES 10

/*
 * Build one user address space: boot-table clones + code pages copied
 * from blob + zeroed 16-KiB stack. Returns the root PML4 PA (0 = no
 * memory), and hands back every frame it allocated through frames[]
 * (up to 10) so exit can hand them back to the pmm deliberately.
 */
u64  proc_build_space(const void *blob, u64 blob_len,
                      u64 *frames, int *nframes);

/* Return all frames of a dead space to the pmm. Call only from a CR3
   whose identity map covers the whole free RAM (< 1 GiB boot map), NOT
   from inside the dead space (frame PAs may sit inside the unmapped
   4..6 MiB window). sched.c's exit path does this correctly. */
void proc_space_destroy(const u64 *frames, int nframes);

/* First "return" of a user thread: from kernel context, load the CPL3
   data selectors and iretq into (0x400000, ss 0x3B, stack, rflags IF).
   CR3 must already be this thread's space (scheduler guarantees it).
   Never returns into C: the thread exits via syscall 3 from userland. */
void proc_user_entry(void) __attribute__((noreturn));
