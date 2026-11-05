#pragma once

#include <kernel/types.h>

/*
 * ELF64 loader plan (M5.3) - the portable half of the ELF loader.
 *
 * This module PARSES AND VALIDATES an ELF64 executable image resident
 * in a buffer, and hands back its load plan: the entry point and the
 * PT_LOAD segments only. It knows nothing about address spaces, page
 * tables, or windows (that is the arch side's job: check segments
 * against its own layout, allocate frames, copy, map); and it prints
 * nothing - callers turn failures into evidence, like every portable
 * module here.
 *
 * Validation is strict on purpose: an OS executable loader accepts
 * exactly what its kernel can defend against, i.e.
 *   - ELF64, little-endian, x86-64, ET_EXEC
 *   - program headers sane and inside the image
 *   - every PT_LOAD: file bytes inside the image, memsz >= filesz
 *   - at least one, ELF_MAX_SEGS at most loadable segments
 * Anything else is refused, negative, before any byte is trusted.
 */

#define ELF_MAX_SEGS 8

struct elf_seg {
    u64 vaddr;               /* p_vaddr: where the segment will live  */
    u64 foff;                /* p_offset: source bytes in the image    */
    u64 filesz;              /* bytes to copy from the image           */
    u64 memsz;               /* bytes of memory; tail (bss) is zeroed  */
};

/* Parse buf, validate, fill entry/segs (PT_LOAD only, in phdr order).
   0 = ok; negative = refuse (see elf64_errname for naming). */
int  elf64_plan(const void *buf, u64 len, u64 *entry,
                struct elf_seg *segs, int *nsegs);

/* Human-readable name for a plan() failure class. */
const char *elf64_errname(int code);
