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
