#include <kernel/types.h>
#include "compo.h"
#include "cursor.h"

/* compo.c - M6.4. The whole module is this small ON PURPOSE: the
   z-order law lives in three names (revision, backdrop_scroll,
   face_cleared) and every repaint path in the tree routes through
   them. When M6.5 adds windows, they become the third layer on the
   same calls - the sprite's re-composite ordering is fixed HERE, so
   no future writer can re-land a sprite over stale pixels by
   accident. */

static u64 rev;

u64 compo_revision(void)
{
    return rev;
}

void compo_backdrop_scroll(u32 dy)
{
    ++rev;
    cursor_scroll_repair(dy);
}

void compo_face_cleared(void)
{
    ++rev;
    cursor_face_repair();
}

void compo_cells_painted(u32 x, u32 y, u32 w, u32 h)
{
    ++rev;
    cursor_overlap_repair(x, y, w, h);
}
