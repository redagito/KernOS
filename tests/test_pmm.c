/*
 * test_pmm.c - host unit tests for the frame stack's honesty (M5.11).
 *
 * The kernel object compiles HERE AS-IS (M3.2's own file): the E820
 * map arrives from a stub, console chatter is swallowed, __bss_end is
 * a real stub array so kernel_limit is a known frame boundary. A
 * deliberately small map - one 8-frame type-1 run over an 8 KiB
 * kernel reserve - drives every rule to its boundary.
 *
 * The M2 strike this suite pins: pmm_init pushed the initial stack
 * WITHOUT marking those frames in in_stack[], so the O(1) double-free
 * guard was blind for the entire initial stack; and the tracked
 * universe had no upper bound (a free outside it read/wrote past the
 * bitmap), while hole frames (not in any type-1 run) were silently
 * adoptable as "freed". Three determinstic red detectors below:
 *   - freeing a frame still on the free stack re-pushed it (count
 *     grew, and the drain later handed the same frame to two callers)
 *   - freeing an untracked hole frame was adopted
 *   - freeing beyond the universe passed no refuse check at all
 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <kernel/types.h>
#include "pmm.h"

/* ---- stubs the kernel object expects -------------------------------- */

u8 __bss_end[0x2000];                     /* 8 KiB kernel reserve        */

static struct { u64 base, len; u32 type; } fake_runs[4];
static int n_fake_runs;

int memmap_count(void) { return n_fake_runs; }
int memmap_get(int i, u64 *base, u64 *len, u32 *type)
{
    if (i < 0 || i >= n_fake_runs) return -1;
    *base = fake_runs[i].base;
    *len  = fake_runs[i].len;
    *type = fake_runs[i].type;
    return 0;
}
void memmap_boot_init(void) {}
void console_puts(const char *s)   { (void)s; }
void console_puts_hex(u64 v)       { (void)v; }

/* ---- the suite ------------------------------------------------------- */

/* Map base sits just above the aligned stub-limit so the whole fake
   universe truthfully fits PMM_MAX_FRAMES frames from kernel_limit -
   an out-of-range base would make even pmm_init index past the very
   bitmap this suite exists to police (found while writing the suite:
   pmm_init had no idx guard either - now fixed in the object). */
#define LIMIT_STOP_ ((u64)__bss_end)
#define BASE     (((LIMIT_STOP_ + 0xFFF) & ~(u64)0xFFF) + 0x10000ULL)
#define FRAMES   8

static int t_no = 0, n_fail = 0;
static void check(int cond, const char *title)
{
    ++t_no;
    printf("%2d. %-52s %s\n", t_no, title, cond ? "PASS" : "FAIL");
    if (!cond) ++n_fail;
}

int main(void)
{
    fake_runs[0].base = BASE;
    fake_runs[0].len  = FRAMES * (u64)FRAME_SIZE;
    fake_runs[0].type = 1;
    n_fake_runs = 1;

    pmm_init();
    check(pmm_free_frames() == FRAMES, "one usable run pushes exactly FRAMES");
    check(__pmm_kernel_limit() <= BASE,
          "kernel_limit (stub bss) sits below the fake universe");

    /* M2 red #1: a frame still on the free stack is NOT a leak the
       caller may re-free - it must be refused, never re-pushed */
    pmm_free(BASE + 3 * (u64)FRAME_SIZE);
    check(pmm_free_frames() == FRAMES,
          "freeing an on-stack frame is refused (no duplicate push)");

    /* M2 red #2: hole frame - realigned, in bit-universe range, but in
       NO type-1 run: untracked, must be refused */
    pmm_free(BASE + (FRAMES + 1) * (u64)FRAME_SIZE);
    check(pmm_free_frames() == FRAMES,
          "freeing an untracked hole frame is refused");

    /* the drain: honest total, frame uniqueness (the duplicate that
       two callers would eventually receive lands here) */
    {
        u64 seen[32] = { 0 };
        int drained = 0, uniq = 1;
        for (;;) {
            u64 pa = pmm_alloc();
            if (!pa) break;
            if (drained < 32)
                for (int i = 0; i < drained; ++i)
                    if (seen[i] == pa) uniq = 0;
            if (drained < 32) seen[drained] = pa;
            ++drained;
        }
        check(drained == FRAMES, "every frame drains exactly once");
        check(uniq, "no frame ever handed to two callers");
        check(pmm_free_frames() == 0, "the stack drains to empty");
    }

    /* refund: each drained frame returns exactly once */
    for (u64 k = 0; k < FRAMES; ++k)
        pmm_free(BASE + k * (u64)FRAME_SIZE);
    check(pmm_free_frames() == FRAMES, "every drained frame returns once");

    /* the existing guard, still true post-fix: an ALLOCATED frame can
       be freed once; the second free is named and refused */
    {
        u64 a = pmm_alloc();
        check(a != 0 && pmm_free_frames() == FRAMES - 1,
              "alloc pops one, count drops by one");
        pmm_free(a);
        check(pmm_free_frames() == FRAMES, "legal free returns the frame");
        pmm_free(a);
        check(pmm_free_frames() == FRAMES,
              "double free of a freed-then-pushed frame is refused");
        pmm_free(a);                      /* it stays refused             */
        check(pmm_free_frames() == FRAMES,
              "triple free also refused");
    }

    /* malformed frees: alignment, below-limit, beyond the universe */
    pmm_free(BASE + FRAME_SIZE + 1);      /* misaligned                   */
    check(pmm_free_frames() == FRAMES, "misaligned free is refused");
    pmm_free(0x1000);                     /* below kernel_limit           */
    check(pmm_free_frames() == FRAMES, "sub-reserve free is refused");
    pmm_free(0x0000ULL);
    check(pmm_free_frames() == FRAMES, "NULL free is refused");
    /* 0x80000000: aligned, but past the tracked universe entirely - no
       bitmap membership, no refuse label today. Post-fix: refused. */
    pmm_free(0x80000000ULL);
    check(pmm_free_frames() == FRAMES, "beyond-universe free is refused");

    printf("%d checks, %d failures\n", t_no, n_fail);
    return n_fail ? 1 : 0;
}
