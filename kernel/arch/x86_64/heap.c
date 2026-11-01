#include <kernel/types.h>
#include "heap.h"
#include "page.h"

/*
 * Bump pointer, 16-byte aligned, over the half of the demand region
 * at DEMAND_BASE + 64 MiB .. +128 MiB (so the page tables of the
 * first demand self-tests and the heap never share PT pages).
 * Pages appear on first touch through the armed #PF handler: kmalloc
 * hands out virtual addresses the pager will service.
 */

#define HEAP_BASE  (PAGE_DEMAND_BASE + 64ULL * 1024 * 1024)
#define HEAP_SPAN  (64ULL * 1024 * 1024)

static u64 brk = HEAP_BASE;

static u64 round_up16(u64 v) { return (v + 15) & ~15ULL; }

void *kmalloc(u64 size)
{
    if (size == 0)
        return 0;

    u64 sz = round_up16(size);
    if (brk >= HEAP_BASE + HEAP_SPAN || sz > HEAP_BASE + HEAP_SPAN - brk)
        return 0;

    void *p = (void *)brk;
    brk += sz;
    return p;
}

void kfree(void *p)
{
    (void)p;    /* bump allocator: no reclamation in M3.4 */
}
