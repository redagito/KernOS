#pragma once

#include <kernel/types.h>

/*
 * cursor (M6.3): the pointer sprite over the M5.6 LFB - the GUI's
 * first always-visible user object.
 *
 * One WRITER per position lifetime, by construction: position moves
 * ONLY inside the input ring's single consumer (syscall nr 7's
 * handler, IF off on its own kernel stack) plus the one boot probe
 * before userland exists - no IRQ path touches the module, the
 * doctrine's checklist holds (chk a-d: named writers, painted state
 * before the first external read of the screen, nothing dilutes
 * WAITING semantics, no IRQ access at all).
 *
 * Rendering rides the M6.1 rect substrate: the module SAVES the
 * pixels under the sprite before painting, so every move RESTORES
 * the last save first - a moving cursor leaves no crumbs, and the
 * boot probe proves the cycle round-trips bit-for-bit.
 */

#define CUR_W 8
#define CUR_H 12

/* Map the LFB (idempotent through fb_map), paint the arrow at rest
   position, and run the save/erase/restore round-trip proof at the
   parked spot. 0 = ok, -1 = no LFB (caller prints the named line and
   the kernel keeps console life; pointers check nothing). */
int  cursor_boot_probe(void);

/* Feed a pulled batch of input events (the SINGLE consumer's post-
   pull step): REL deltas move the clamped position, a move repaints
   via restore+save+paint. Absence (no LFB) is a silent no-op. */
void cursor_apply(const struct input_event *ev, int n);
