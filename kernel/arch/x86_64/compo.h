#pragma once

#include <kernel/types.h>

/*
 * compo (M6.4): the compositor - the module that owns z-order.
 *
 * Layers today: the fbcon console face (the backdrop) and the pointer
 * sprite (always on top). The compositor's ONE law: any time the
 * backdrop repaints, the sprite re-composites over the NEW truth -
 * never over a memory of it. That is the structural end of the M6.3
 * priced crumb: fbcon announces scroll/clear here, the revision
 * advances, and the cursor's save buffer is only ever trusted while
 * the revision it was taken under still stands.
 */

/* Monotonic backdrop revision. The sprite stamps its save with it. */
u64  compo_revision(void);

/* fbcon scrolled dy pixels: the sprite re-composites (its pixels rode
   the scroll; the cells it crossed are re-drawn from the buffer). */
void compo_backdrop_scroll(u32 dy);

/* fbcon wiped the face: the sprite simply re-lands on top. */
void compo_face_cleared(void);

/* The console painted truth pixels in a rect ( one codepoint cell
   or a planted cell set): the sprite re-lands only if the paint
   touched its slot; every other paint just keeps the revision
   honest. M6.4's lesson - a console-side draw is a backdrop write
   exactly as much as a scroll is. */
void compo_cells_painted(u32 x, u32 y, u32 w, u32 h);
