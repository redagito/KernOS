#pragma once

#include <kernel/types.h>

/*
 * Demand paging (M3.3): 4 KiB anonymous pages are created on first
 * touch inside a fixed demand region above the 1 GiB identity map.
 * Frames come from pmm_alloc(); paging structures are read/written
 * through the identity map (PA == VA), CR3 is reloaded to flush.
 * Faults outside the region (or present-bit faults) are declined -
 * those fall back to dump-and-park.
 */

#define PAGE_DEMAND_BASE  0x40000000ULL        /* 1 GiB  */
#define PAGE_DEMAND_SPAN  (128 * 1024 * 1024)  /* 128 MiB */

void pf_init(void);   /* register the #PF handler */

void pf_stats(u64 *faults, u64 *pd_pages, u64 *pt_pages, u64 *data_pages);
