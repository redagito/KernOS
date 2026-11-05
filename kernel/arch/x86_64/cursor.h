#pragma once

#include <kernel/types.h>
#include <kernel/input.h>

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

/* M6.4: the compositor's repair entries - the sprite re-composites
   over the CURRENT truth. cursor_scroll_repair re-draws the truth
   for every cell the sprite's pixels crossed (they rode a dy-px
   scroll), then re-lands; cursor_face_repair re-lands after a wipe;
   cursor_overlap_repair re-lands when console-side painting touched
   the sprite's slot (a console draw is a backdrop write exactly as
   much as a scroll is). All stamp the save with the live revision. */
void cursor_scroll_repair(u32 dy);
void cursor_face_repair(void);
void cursor_overlap_repair(u32 x, u32 y, u32 w, u32 h);

/* The sprite's top-left (the gate's handle on where repaints land). */
u32 cursor_x(void);
u32 cursor_y(void);
