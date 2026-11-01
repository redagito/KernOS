#pragma once

#include <kernel/types.h>

/*
 * Physical frame allocator (M3.2): the E820 type-1 runs become a
 * LIFO stack of free 4 KiB frames. Everything below 1 MiB stays out
 * (BIOS structures, stage 2, page tables that CR3 is still pointing
 * at) and everything through the kernel's .bss end (page-rounded) is
 * reserved, so allocation can never collide with boot or kernel data.
 */

#define FRAME_SIZE 4096

void pmm_init(void);
u64  pmm_alloc(void);        /* physical address of a free frame, 0 = none */
void pmm_free(u64 pa);       /* must be a frame-aligned pmm_alloc frame    */
u64  pmm_free_frames(void);
u64  __pmm_kernel_limit(void);   /* first allocatable frame (diagnostics) */
