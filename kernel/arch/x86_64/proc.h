#pragma once

#include <kernel/types.h>

/*
 * Process model (M5.3): real address spaces built from real binaries.
 *
 * Layout decision: user code+data lives at 0x400000 inside the
 * identity 2-MiB slot i2=2; the user stack (64 KiB) ends at 0x600000,
 * the top of the same slot. The per-process CR3 reuses the boot
 * hierarchy (clone PML4 -> clone PDPT -> clone PD) with one patch: PD
 * slot 2 turns the 2-MiB identity leaf into a PT of 4-KiB user pages.
 * Everything outside that window stays supervisor-only, so CPL3 code
 * can touch exactly what this module mapped - which is why segments
 * outside the window are a loader refusal, not a mapping.
 *
 * Known debt (documented, do not "fix" silently):
 *  - demand-region page tables (>= 0x40000000) are shared with the
 *    boot space via the cloned PML4; a user process never uses them
 *    (U bits absent at every level), but a post-fork demand allocation
 *    in the shared region would fault on some CR3s. No fork yet; no
 *    user demand usage ever; revisit only when forking exists.
 *  - user->kernel switches are only made through the scheduler paths
 *    in sched.c; nothing else may load a process CR3. The scheduler
 *    record rules that own those paths are the RECORD DOCTRINE,
 *    normative in sched.h since M5.13 - no record field is added, and
 *    no path may write one, outside its writer/owner rules.
 *  - no W^X split yet (no NX bits, no fault-on-write tracks). The
 *    ELF loader maps everything P|U|W; KCC/M6 work revisits.
 */

#define USER_WINDOW_LO  0x400000ULL   /* first mapped VA (cloned PD i2=2) */
#define USER_STACK_VA   0x600000ULL   /* stack top (grows down)           */
#define USER_STACK_SZ   (64 * 1024)
#define USER_MAX_PAGES  480           /* compiler code, data, and arena */

/* Frame ledger cap: page tables + bounded program span + stack. */
#define MAX_PFRAMES (4 + USER_MAX_PAGES + USER_STACK_SZ / 4096)

/* Build one user address space from a resident ELF64 image (validated
   by the portable elf64_plan module): boot-table clones, span pages
   copied/zeroed per the PT_LOAD plan, zeroed 64-KiB stack. Returns
   the root PML4 PA (0 = refusal of any kind) and fills frames[] /
   entry_out; the entry point comes from the Ehdr and is checked to
   live inside the mapped span. Source buffer must identity-mapped
   (kernel VA space) but may live anywhere. */
u64  proc_build_space_elf(const void *elf, u64 elf_len,
                          u64 *frames, int *nframes, u64 *entry_out);

/* Return all frames of a dead space to the pmm. Call only from a CR3
   whose identity map covers the whole free RAM (< 1 GiB boot map), NOT
   from inside the dead space (frame PAs may sit inside the unmapped
   4..6 MiB window). sched.c's exit path does this correctly. */
void proc_space_destroy(const u64 *frames, int nframes);

/* First "return" of a user thread: from kernel context, load the CPL3
   data selectors and iretq to (entry, ss 0x3B, stack top, rflags IF).
   CR3 must already be this thread's space (scheduler guarantees it).
   Never returns into C: the thread exits via syscall 3 from userland. */
void proc_user_entry(u64 entry) __attribute__((noreturn));
