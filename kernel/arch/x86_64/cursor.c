#include <kernel/types.h>
#include <kernel/input.h>
#include "fb.h"
#include "fbcon.h"
#include "compo.h"
#include "cursor.h"

/*
 * cursor.c - M6.3, the pointer sprite. Hand-drawn 8x12 arrow; white
 * on whatever the face holds. The sprite is the compositor's first
 * CLIENT: it composites in the exact restore->save->paint order the
 * window layer will use, so M6.4 can lift the pattern as-is.
 */

/* image data (MSB = leftmost). Rows 0..11. */
static const unsigned char art[CUR_H] = {
    0x80, 0xC0, 0xA0, 0x90, 0x88, 0x84,
    0xFC, 0x80, 0x88, 0x94, 0xC0, 0x00,
};

#define CUR_RGB   0xF8F8F8u           /* near-white ink */

static u32 cx, cy;                    /* sprite's top-left on screen */
static u32 save[CUR_W * CUR_H];       /* pixels beneath the sprite   */
static int  painted;                  /* sprite currently visible    */
static u64  saved_rev;                /* backdrop rev the save is from */

static void clamp_pos(void)
{
    u32 w = fb_width(), h = fb_height();
    u32 max_x = w >= CUR_W ? w - CUR_W : 0;
    u32 max_y = h >= CUR_H ? h - CUR_H : 0;
    if (cx > max_x) cx = max_x;
    if (cy > max_y) cy = max_y;
}

static void save_region(void)
{
    for (u32 r = 0; r < CUR_H; ++r)
        for (u32 c = 0; c < CUR_W; ++c)
            save[r * CUR_W + c] = fb_pixel_get(cx + c, cy + r);
    painted = 0;
}

static void paint_sprite(void)
{
    for (u32 r = 0; r < CUR_H; ++r)
        for (u32 c = 0; c < CUR_W; ++c)
            if (art[r] & (0x80u >> c))
                fb_pixel_set(cx + c, cy + r, CUR_RGB);
    painted = 1;
}

static void restore_region(void)
{
    if (!painted) return;
    for (u32 r = 0; r < CUR_H; ++r)
        for (u32 c = 0; c < CUR_W; ++c)
            fb_pixel_set(cx + c, cy + r, save[r * CUR_W + c]);
    painted = 0;
}

/* M6.4: the repaint-under path for a stale save. The save buffer is
   only trusted while the backdrop revision it was taken under still
   stands; once the console scrolled or cleared beneath us, the truth
   comes from the console's own cell buffer, never from memory. */
static void undo_by_truth(void)
{
    fbcon_cells_redraw(cx, cy, CUR_W, CUR_H);
    painted = 0;
}

/* One repaint cycle (M6.4, revision-aware): erase the old sprite
   (restore-from-save while the revision stands, repaint-from-truth
   once it has moved on), then save+paint the new and stamp the save
   with the live revision. Stale saves can NEVER be written back:
   the priced M6.3 crumb is dead by construction. */
static void move_to(u32 nx, u32 ny)
{
    if (painted) {
        if (saved_rev == compo_revision())
            restore_region();
        else
            undo_by_truth();
    }
    cx = nx;
    cy = ny;
    clamp_pos();
    save_region();
    paint_sprite();
    saved_rev = compo_revision();
}

int cursor_boot_probe(void)
{
    u32 w = fb_width(), h = fb_height();

    if (!fb_present() || !fb_map() || w < CUR_W || h < CUR_H)
        return -1;

    /* proven at the bottom-left glyph cell the console WILL repaint
       later - bounded, and any prior pixel content there is exact
       round-trip evidence, not noise */
    cx = 16;
    cy = h - 32;

    /* save -> paint -> erase(via restore) -> verify: the statistics
       the self-check keeps are the restore reading back what save
       read, bit for bit. */
    save_region();
    paint_sprite();
    restore_region();

    for (u32 r = 0; r < CUR_H; ++r) {
        for (u32 c = 0; c < CUR_W; ++c) {
            u32 now = fb_pixel_get(cx + c, cy + r);
            u32 was = save[r * CUR_W + c];
            if (now != was)
                return 1;                     /* restore lost a bit */
        }
    }

    /* leave the cursor visible at a sane parked position *without*
       disturbing the verified pixels: move (restore+save+paint) to
       the screen's lower-right guard corner-ish slot */
    move_to(w - CUR_W - 16, h - CUR_H - 16);
    return 0;
}

void cursor_apply(const struct input_event *ev, int n)
{
    if (!fb_present() || !fb_map() || n <= 0) return;

    u32 nx = cx, ny = cy;
    int dx = 0, dy = 0;
    for (int i = 0; i < n; ++i) {
        if (ev[i].type == IN_EVT_RELX) dx += ev[i].value;
        else if (ev[i].type == IN_EVT_RELY) dy += ev[i].value;
    }
    if (dx == 0 && dy == 0) return;
    long sx = (long)cx + dx, sy = (long)cy + dy;
    nx = sx < 0 ? 0 : (u32)sx;
    ny = sy < 0 ? 0 : (u32)sy;
    move_to(nx, ny);
}

/* M6.4: after a dy-px scroll the sprite's pixels rode the content -
   its rest position holds truth pixels now, and whatever pixels the
   sprite owned sit one row up. Redraw the truth for the whole span
   the sprite crossed (its slot plus the dy it rode), then re-land
   the sprite exactly where the compositor says it belongs. */
void cursor_scroll_repair(u32 dy)
{
    if (!fb_present() || !fb_map()) return;
    saved_rev = compo_revision();          /* any old save is dead    */
    if (painted) {
        u32 from = (cy >= dy) ? cy - dy : 0;
        fbcon_cells_redraw(cx, from, CUR_W, CUR_H + (cy - from));
        painted = 0;
    }
    save_region();
    paint_sprite();
    saved_rev = compo_revision();
}

/* M6.4: after a face wipe the sprite re-lands on the cleared truth. */
void cursor_face_repair(void)
{
    if (!fb_present() || !fb_map()) return;
    saved_rev = compo_revision();
    painted = 0;                           /* the wipe erased the pixels */
    save_region();
    paint_sprite();
    saved_rev = compo_revision();
}

u32 cursor_x(void) { return cx; }
u32 cursor_y(void) { return cy; }

/* M6.4: truth pixels changed in rect (x,y,w,h) - a console-side code
   draw (putc or a planted cell). Away from the sprite's slot there
   is nothing to repair: the save stays exactly valid, only the
   revision bookkeeping keeps current. Overlapping the slot: the
   paint went over (or under) the sprite, so the truth under it is
   redrawn and the sprite re-lands - the same day-one ride the plant
   gate caught, now impossible. */
void cursor_overlap_repair(u32 x, u32 y, u32 w, u32 h)
{
    if (!fb_present() || !fb_map() || w == 0 || h == 0) return;
    saved_rev = compo_revision();
    if (!painted) return;
    if (x + w <= cx || cx + CUR_W <= x || y + h <= cy || cy + CUR_H <= y)
        return;
    fbcon_cells_redraw(cx, cy, CUR_W, CUR_H);
    painted = 0;
    save_region();
    paint_sprite();
    saved_rev = compo_revision();
}
