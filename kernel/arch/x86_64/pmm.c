#include <kernel/types.h>
#include "pmm.h"
#include "mem.h"
#include "serial.h"
#include "console.h"

/*
 * LIFO frame stack over the E820 type-1 runs. Frames are pushed
 * ascending within a run, so alloc pops from the top of the run -
 * addresses descend; free() interleave makes the pattern arbitrary,
 * which is fine: the only invariant is uniqueness, checked below.
 */

#define PMM_MAX_FRAMES 32768        /* 128 MiB worth; 256 KiB of .bss */

static u64 free_stack[PMM_MAX_FRAMES];
static u64 top;                     /* == free frame count */
static u64 kernel_limit;            /* frames below this are boot/kernel */

/* In-stack marker: bit = free_stack currently holds that frame. Turns
   double-free detection O(1); the tracked range begins at
   kernel_limit and is frame-contiguous, so index math is direct.

   M5.11 (round-two review, M2): the marker was honest ONLY for frames
   that had been freed at least once - init pushed the whole initial
   stack with bits CLEAR, so a free of any frame still sitting on the
   stack passed the guard and re-pushed a duplicate (two allocations
   later, two callers aliased one frame), hole frames (unusable RAM,
   e.g. MMIO) were adoptable as "freed", and a pa past the tracked
   universe had NO check at all - it indexed past this very bitmap.
   Discipline now: init sets BOTH bits on every pushed frame (seen
   marks universe membership and stays set for the frame's lifetime),
   alloc clears in_stack, free refuses anything the universe never
   contained or that is already back on the stack. */
static u32 in_stack[PMM_MAX_FRAMES / 32];
static u32 in_universe[PMM_MAX_FRAMES / 32];

/* Linker symbols (kernel.ld) */
extern u8 __bss_end[];

static u64 round_up(u64 v, u64 a) { return (v + a - 1) & ~(a - 1); }

void pmm_init(void)
{
    kernel_limit = round_up((u64)__bss_end, FRAME_SIZE);

    int n = memmap_count();
    u64 base, len;
    u32 type;

    for (int i = 0; i < n; ++i) {
        if (memmap_get(i, &base, &len, &type) < 0) break;
        if (type != 1) continue;                 /* usable RAM only */

        u64 start = round_up(base, FRAME_SIZE);
        u64 end   = base + len;

        /* Boot estate: low 1 MiB + the kernel image itself. Reclaiming
           stage-2 territory (stack, page tables) waits for the kernel
           to own its own page tables - not this milestone. */
        if (start < kernel_limit) start = kernel_limit;
        if (end <= start) continue;

        for (u64 f = start; f + FRAME_SIZE <= end; f += FRAME_SIZE) {
            u64 idx = (f - kernel_limit) / FRAME_SIZE;
            /* M5.11: a sparse map (large non-usable hole) walks f past
               the universe while top is still small - top==cap never
               fires, and the old code indexed in_stack[] with an idx
               no bitmap table covers. Drop the rest of such runs. */
            if (idx >= PMM_MAX_FRAMES) {
                console_puts("pmm: frames past universe dropped\n");
                break;
            }
            if (top == PMM_MAX_FRAMES) break;    /* table full, drop rest */
            in_stack[idx / 32]    |= 1u << (idx % 32);
            in_universe[idx / 32] |= 1u << (idx % 32);
            free_stack[top++] = f;
        }
        if (top == PMM_MAX_FRAMES) break;
    }

    console_puts("pmm: frames=");
    console_puts_hex(top);
    console_puts("\n");
}

u64 pmm_alloc(void)
{
    if (top == 0) return 0;
    u64 pa = free_stack[--top];
    u64 idx = (pa - kernel_limit) / FRAME_SIZE;
    in_stack[idx / 32] &= ~(u32)(1u << (idx % 32));
    return pa;
}

void pmm_free(u64 pa)
{
    if ((pa & (FRAME_SIZE - 1)) != 0 || pa < kernel_limit) {
        console_puts("pmm: refuse bad free ");
        console_puts_hex(pa);
        console_puts("\n");
        return;
    }
    u64 idx = (pa - kernel_limit) / FRAME_SIZE;
    if (idx >= PMM_MAX_FRAMES) {
        /* M5.11: the old code indexed in_stack[] with ANY aligned pa
           INSIDE the universe only by luck bounds - everything past
           the last tracked frame read/wrote past this bitmap. */
        console_puts("pmm: refuse free past universe ");
        console_puts_hex(pa);
        console_puts("\n");
        return;
    }
    if (!(in_universe[idx / 32] & (1u << (idx % 32)))) {
        /* hole frame: not in any type-1 run, adoptable no longer  */
        console_puts("pmm: refuse untracked free ");
        console_puts_hex(pa);
        console_puts("\n");
        return;
    }
    if (top == PMM_MAX_FRAMES) return;           /* cannot happen: balanced */
    if (in_stack[idx / 32] & (1u << (idx % 32))) {
        console_puts("pmm: double free ");
        console_puts_hex(pa);
        console_puts("\n");
        return;
    }
    in_stack[idx / 32] |= 1u << (idx % 32);
    free_stack[top++] = pa;
}

u64 pmm_free_frames(void) { return top; }

u64 __pmm_kernel_limit(void) { return kernel_limit; }
