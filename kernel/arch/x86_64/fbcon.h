#pragma once

#include <kernel/types.h>

/*
 * fbcon (M5.7): the PSF2 framebuffer console. The embedded font blob
 * (font.s, tools/mkfont.c) is validated at init - magic, header
 * coherence, charsize - and never trusted beyond that; a refusal is a
 * named failure the caller turns into evidence. Cells are the font's
 * own width x height (8x16 for the shipped font), stripe-free: draw
 * paints EVERY pixel of the cell (fg ink or bg), so what you put on
 * screen is exactly the glyph bitmap.
 *
 * The module demands an fb LFB (fb_present(), fb map done) and speaks
 * only pixels through the fb API; with no LFB the kernel keeps its VGA
 * text console - that is named absence, not a fallback lie.
 *
 * No input echo, no cursor, no control chars beyond \n \r: the M5.7
 * console reports, it doesn't chat.
 */

/* Validate the embedded font + latch geometry. 0 = ok, -1 = the blob
   is not a coherent PSF2 (caller must print the named refusal and
   keep whatever console it had). */
int  fbcon_init(void);

void fbcon_putc(char c);
void fbcon_puts(const char *s);
void fbcon_clear(void);

/* Geometry the caller can reason about (0 before init). */
u32  fbcon_cols(void);      /* text columns     = fb_width / cw  */
u32  fbcon_rows(void);      /* text rows        = fb_height/ch   */
u64  fbcon_scrolls(void);   /* hard scrolls since init           */
u32  fbcon_fg(void);        /* the one static fg color           */

/* M6.5: the glyph height in pixels - the focus hit-test's single
   piece of geometry (cursor pixel-y -> claim-band row). 0 before
   init, like the rest of the getters. */
u32  fbcon_glyph_h(void);

/* M6.2: ink count of the glyph a CODEPOINT maps to under the cell
   map - the read side of the draw, so gates compare painted pixels
   against the font's own answer. 0 before init. */
u64  fbcon_map_ink(u32 cp);

/* M6.4: the console's own truth, addressable. The cell buffer keeps
   one codepoint per cell, kept exact by every draw/scroll/clear, so
   the compositor can ask "what BELONGS under the sprite" and get an
   exact repaint - the z-order repair that ends the stale-save crumb. */

/* Redraw every whole cell intersecting the pixel rect (x,y,w,h) from
   the buffer - the repaint-under primitive the compositor uses. */
void fbcon_cells_redraw(u32 x, u32 y, u32 w, u32 h);

/* Draw one cell directly and record it (the gate's and the
   compositor's controlled-content entry; cursor col/row untouched). */
void fbcon_cell_draw(u32 col, u32 row, u32 cp);

/* The buffer's truth for one cell (0 where blank or out of range). */
u32  fbcon_cell_get(u32 col, u32 row);
