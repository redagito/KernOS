#pragma once

#include <kernel/types.h>

/*
 * Kernel heap (M3.4): bump allocator inside the upper half of the
 * demand region. Pages materialize by the #PF demand pager as the
 * caller first touches them - no eager mapping in kmalloc itself.
 * Requires pf_init() (handler armed) and pmm_init() first.
 * kfree() is a no-op: nothing frees from the kernel heap yet; a real
 * free-list arrives when userland heap (malloc) demands it.
 */

void *kmalloc(u64 size);    /* NULL on size==0 or heap exhaustion */
void  kfree(void *p);       /* no-op in M3.4 */
