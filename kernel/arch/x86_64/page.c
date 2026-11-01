#include <kernel/types.h>
#include "page.h"
#include "pmm.h"
#include "trap.h"
#include "serial.h"

/*
 * The #PF handler with a real recover path: an anonymous page materializes
 * the first time someone touches it. Walking (and building) the stage-2
 * tables virtually through the identity map - PA == VA down there, so a
 * P word IS its target's address.
 *
 * One fault fixes everything it can: fresh PDPT slot -> new page-directory
 * page, fresh PD slot -> new PT page, fresh PT slot -> new zeroed 4 KiB
 * data frame. One CR3 reload flushes it all; the faulting store/load
 * retries under iretq.
 */

static u64 stat_faults, stat_pd, stat_pt, stat_data;

static u64 cr3_read(void)
{
    u64 v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static void cr3_flush(void)
{
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax");
}

static void zero_frame(u64 pa)
{
    volatile u64 *p = (volatile u64 *)pa;   /* identity map: below 1 GiB */
    for (u32 i = 0; i < FRAME_SIZE / sizeof(u64); ++i)
        p[i] = 0;
}

/* Shared walk: create/zero missing PD and PT pages for va; returns the
   PT holding va's leaf entry. 0 = ok. */
static int ensure_levels(u64 va, u64 **pt_out, u64 *leaf_idx_out)
{
    u64 i4 = (va >> 39) & 0x1FF;
    u64 i3 = (va >> 30) & 0x1FF;
    u64 i2 = (va >> 21) & 0x1FF;
    u64 i1 = (va >> 12) & 0x1FF;

    u64 *pml4 = (u64 *)(cr3_read() & ~0xFFFULL);
    if (!(pml4[i4] & 1))
        return -1;                       /* no PDPT image for this slot */

    u64 *pdpt = (u64 *)(pml4[i4] & ~0xFFFULL);
    if (!(pdpt[i3] & 1)) {
        u64 pa = pmm_alloc();
        if (!pa) return -1;
        zero_frame(pa);
        pdpt[i3] = pa | 0x03;            /* P | W, supervisor */
        stat_pd++;
    }

    u64 *pd = (u64 *)(pdpt[i3] & ~0xFFFULL);
    if (pd[i2] & 0x80)
        return -1;                       /* 2 MiB leaf inside property region: conflict */
    if (!(pd[i2] & 1)) {
        u64 pa = pmm_alloc();
        if (!pa) return -1;
        zero_frame(pa);
        pd[i2] = pa | 0x03;
        stat_pt++;
    }

    *pt_out = &((u64 *)(pd[i2] & ~0xFFFULL))[0];
    *leaf_idx_out = i1;
    return 0;
}

/* Map va (4 KiB page granularity) into the demand region. 0 = ok. */
int page_demand_map(u64 va)
{
    if (va < PAGE_DEMAND_BASE || va >= PAGE_DEMAND_BASE + PAGE_DEMAND_SPAN)
        return -1;

    u64 *pt; u64 i1;
    if (ensure_levels(va, &pt, &i1) < 0)
        return -1;

    if (pt[i1] & 1)
        return -1;                       /* already present: not a pure miss */

    u64 pa = pmm_alloc();
    if (!pa) return -1;
    zero_frame(pa);
    pt[i1] = pa | 0x03;
    stat_data++;

    cr3_flush();
    return 0;
}

/* Fixed-PA mapping into the demand region (device MMIO, buffers).
   One CR3 flush at the end. Does NOT zero the target (it is not RAM). */
int page_map_at(u64 va, u64 pa, u32 npages)
{
    for (u32 n = 0; n < npages; ++n) {
        u64 *pt; u64 i1;
        if (ensure_levels(va, &pt, &i1) < 0)
            return -1;
        if (pt[i1] & 1)
            return -1;                   /* overwrite refusal */
        pt[i1] = (pa & ~0xFFFULL) | 0x03;
        va += FRAME_SIZE;
        pa += FRAME_SIZE;
    }
    cr3_flush();
    return 0;
}

static int pf_handler(struct trapframe *tf)
{
    if (tf->err & 1)                     /* present-bit fault: not a miss */
        return 0;

    u64 va;
    __asm__ volatile("mov %%cr2, %0" : "=r"(va));

    if (page_demand_map(va & ~0xFFFULL) < 0)
        return 0;

    stat_faults++;
    return 1;                            /* fixed: isr.s will iretq, retry */
}

void pf_init(void)
{
    stat_faults = stat_pd = stat_pt = stat_data = 0;
    exc_set_handler(14, pf_handler);
    serial_puts("pf: demand pager armed\n");
}

void pf_stats(u64 *faults, u64 *pd_pages, u64 *pt_pages, u64 *data_pages)
{
    if (faults)     *faults     = stat_faults;
    if (pd_pages)   *pd_pages   = stat_pd;
    if (pt_pages)   *pt_pages   = stat_pt;
    if (data_pages) *data_pages = stat_data;
}
