#pragma once

#include <kernel/types.h>

/*
 * Boot memory ABI (docs/disk-layout.md): stage 2 captures the E820 SMAP
 * into low RAM before it drops the BIOS:
 *   0x600 : u64 entry count (0 if capture failed)
 *   0x608 : entries, 24 B each: u64 base, u64 len, u32 type, u32 pad
 * Fixed addresses on purpose - M3 tolerates a constant ABI; the whole
 * boot ABI gets a struct when the image header lands.
 */

#define E820_MAX_ENTRIES 32

int  memmap_count(void);   /* 0 = capture failed */
int  memmap_get(int i, u64 *base, u64 *len, u32 *type);

void memmap_boot_init(void);   /* print the map over serial (gate evidence) */
