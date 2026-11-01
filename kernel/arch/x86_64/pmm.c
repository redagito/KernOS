#include <kernel/types.h>
#include "pmm.h"
#include "mem.h"
#include "serial.h"

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
   kernel_limit and is frame-contiguous, so index math is direct. */
static u32 in_stack[PMM_MAX_FRAMES / 32];

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
            if (top == PMM_MAX_FRAMES) break;    /* table full, drop rest */
            free_stack[top++] = f;
        }
        if (top == PMM_MAX_FRAMES) break;
    }

    serial_puts("pmm: frames=");
    serial_puts_hex(top);
    serial_puts("\n");
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
        serial_puts("pmm: refuse bad free ");
        serial_puts_hex(pa);
        serial_puts("\n");
        return;
    }
    if (top == PMM_MAX_FRAMES) return;           /* cannot happen: balanced */
    u64 idx = (pa - kernel_limit) / FRAME_SIZE;
    if (in_stack[idx / 32] & (1u << (idx % 32))) {
        serial_puts("pmm: double free ");
        serial_puts_hex(pa);
        serial_puts("\n");
        return;
    }
    in_stack[idx / 32] |= 1u << (idx % 32);
    free_stack[top++] = pa;
}

u64 pmm_free_frames(void) { return top; }

u64 __pmm_kernel_limit(void) { return kernel_limit; }
