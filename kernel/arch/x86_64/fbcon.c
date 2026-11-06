#include <kernel/types.h>
#include <kernel/utf8.h>
#include "fbcon.h"
#include "fb.h"
#include "compo.h"
#include "irq.h"

/*
 * fbcon.c — glyph console on the M5.6 linear framebuffer.
 *
 * The embedded PSF2 blob is parsed here (tools/mkfont.c doc header
 * carries the format fields). Everything the console draws is derived
 * from the validated header plus the fb handoff; no hardcoded screen
 * geometry beyond "cell = width x height glyph".
 */

extern const unsigned char font_psf2[];      /* font.s   */
extern const unsigned char font_psf2_end[];

/* PSF2 header field offsets (u32 LE), per tools/mkfont.c. */
#define H_MAGIC      0
#define H_VERSION    1
#define H_HEADERSIZE 2
#define H_FLAGS      3
#define H_LENGTH     4
#define H_CHARSIZE   5
#define H_HEIGHT     6
#define H_WIDTH      7
#define PSF2_MAGIC   0x864AB552u

static const u32 *hdr;                        /* header words           */
static const unsigned char *glyphs;           /* first glyph byte       */
static u32 nglyphs, charsize, gw, gh;
static u32 col, row, cols, rows;              /* cursor + screen in cells */
static struct utf8_state u8st;                /* M6.2 byte decode acc    */
static u64 scrolls;                           /* hard scroll counter    */
static u32 fg = 0x00AAAAAA;                   /* light grey on black    */

/* M6.4: one codepoint per cell - the console's own truth. Every
   draw, scroll and clear keeps it exact, so "what belongs under the
   sprite" is a fact, not a reconstruction. Codepoint 0 draws glyph 0
   (blank), so a zeroed buffer IS the blank screen. Capped statically:
   a screen over the cap is an init-time named refusal, not a guess. */
#define FBCON_MAX_CELLS 8192
static u32 cellbuf[FBCON_MAX_CELLS];
static u8 cellmark[FBCON_MAX_CELLS];

u32 fbcon_fg(void) { return fg; }
u32 fbcon_cols(void) { return cols; }
u32 fbcon_rows(void) { return rows; }
u64 fbcon_scrolls(void) { return scrolls; }
u32 fbcon_glyph_h(void) { return gh; }        /* M6.5: the hit test    */

int fbcon_init(void)
{
    u32 headersize;

    if (!fb_present()) return -1;

    /* the embedded blob's own header: 8 LE u32s per tools/mkfont.c.
       font_psf2 is a symbol (not a constant address), so plain
       volatile reads need no fixed-address asm dodge. */
    if (*(const volatile u32 *)(const volatile void *)font_psf2
        != PSF2_MAGIC) return -1;
    headersize = *(const volatile u32 *)
        ((const volatile void *)(font_psf2 + H_HEADERSIZE * 4));
    if (headersize < 32) return -1;
    if (font_psf2_end - font_psf2 <
        (long)headersize + (long)128 * 16) return -1; /* 128+ glyphs min */

    hdr      = (const u32 *)font_psf2;
    nglyphs  = hdr[H_LENGTH];
    charsize = hdr[H_CHARSIZE];
    gw       = hdr[H_WIDTH];
    gh       = hdr[H_HEIGHT];
    glyphs   = font_psf2 + headersize;

    /* incoherent font = refusal, not a guess at geometry */
    if (nglyphs < 128 || charsize == 0 || gw == 0 || gh == 0 ||
        charsize < (((unsigned long)gw + 7) / 8) * gh)
        return -1;

    cols = fb_width() / gw;
    rows = fb_height() / gh;
    if (cols == 0 || rows == 0) return -1;
    if ((u64)cols * rows > FBCON_MAX_CELLS) return -1;
    col = 0;
    row = 0;
    for (u32 i = 0; i < (u64)cols * rows; ++i) cellbuf[i] = 0;
    utf8_reset(&u8st);
    return 0;
}

/* Ink count of the glyph a codepoint maps to - the read side of the
   cell map, so the boot gate can compare a painted cell against the
   font's own answer (M6.2). Must follow fbcon_init's validation. */
u64 fbcon_map_ink(u32 cp)
{
    unsigned char c;
    const unsigned char *g;
    u32 r, x;
    u64 ink = 0;

    if (nglyphs == 0) return 0;
    c = cp < nglyphs ? (unsigned char)cp : (unsigned char)255;
    g = glyphs + (u32)c * charsize;
    for (r = 0; r < gh; ++r) {
        unsigned char bits = g[r];
        for (x = 0; x < gw; ++x)
            if (bits & (0x80 >> x)) ++ink;
    }
    return ink;
}

/* One glyph row: paint every pixel, fg where ink, bg where not.
   The CELL MAP (M6.2): a decoded codepoint below the font's glyph
   count paints its identity glyph (0..31 blank controls, 127..255
   the "missing" boxes mkfont drew); anything above the font's
   coverage paints glyph 255 - the placeholder box, VISIBLE honesty
   for undrawn text, never a dropped or wrap-cast byte. */
static void draw_cell(u32 cx, u32 cy, u32 cp)
{
    const unsigned char *g;
    u32 r, x;
    unsigned char c;

    c = cp < nglyphs ? (unsigned char)cp : (unsigned char)255;
    g = glyphs + (u32)c * charsize;
    for (r = 0; r < gh; ++r) {
        unsigned char bits = g[r];
        for (x = 0; x < gw; ++x)
            fb_pixel_set(cx * gw + x, cy * gh + r,
                         (bits & (0x80 >> x)) ? fg :
                         (cellmark[cy * cols + cx] ? 0x00336699 : 0));
    }
}

/* Hard scroll: everything moves up one glyph row, the last row blanks.
   M6.1: the moves are region copies now - the substrate's row walk,
   not height*width round-trips through the pixel pair. The scroll-echo
   gate in main.c still proves the copy bit-for-byte via stamps. */
static void scroll(void)
{
    fb_region_copy(0, gh, fb_width(), fb_height() - gh, 0, 0);
    fb_rect_fill(0, fb_height() - gh, fb_width(), gh, 0);
    ++scrolls;

    /* M6.4: the buffer scrolls EXACTLY with the pixels - one row up,
       zeroed tail - so any later "what belongs here" read is the
       scrolled truth, not the pre-scroll one. */
    for (u32 r2 = 1; r2 < rows; ++r2)
        for (u32 c2 = 0; c2 < cols; ++c2)
            cellbuf[(r2 - 1) * cols + c2] = cellbuf[r2 * cols + c2];
    for (u32 c2 = 0; c2 < cols; ++c2)
        cellbuf[(rows - 1) * cols + c2] = 0;

    /* the compositor re-composites the sprite over the new truth
       (the sprite's pixels rode this scroll; its slot is re-drawn
       from the buffer and the sprite re-lands) */
    compo_backdrop_scroll(gh);
}

/* M6.4-followup: the moment two printer threads race the console
   (worker A vs worker B, IF on, PIT-preemptible at ANY instruction
   of the draw loop), the print path is a CRITICAL SECTION. The face's
   shared statics (col, row, cellbuf, u8st) plus the ghxgw pixel
   stores of one cell are not atomic: a tick landing inside a writer's
   draw lets the sibling finish that same cell, and the resumed writer
   repaints only its RESIDUAL glyph rows over it - the observed
   intra-cell band mix (one cell = glyph A's top rows over glyph B's
   bottom rows, seam never font-aligned; serial stayed correct per
   byte-order since the byte left for the UART before any draw). The
   whole per-codepoint window (decode -> draw -> truth store ->
   advance, inclusive of a bottom-edge scroll call) is serialized
   with the irq_save/restore primitives, NOT a bare cli/sti pair:
   the syscall write gate already prints with IF off, and restoring
   exactly the caller's flags keeps the gate's IF-off contract.
   Latency precedent: that gate holds the IF off for whole lines. */
static void fbcon_clear_face(void)
{
    fb_rect_fill(0, 0, fb_width(), fb_height(), 0);
    col = 0;
    row = 0;
    for (u32 i = 0; i < (u64)cols * rows; ++i) cellbuf[i] = 0;
    compo_face_cleared();
}

void fbcon_clear(void)
{
    u64 flags = irq_save();
    fbcon_clear_face();
    irq_restore(flags);
}

static void fbcon_putc_face(char c)
{
    unsigned char b = (unsigned char)c;
    u32 cp;
    int r;

    if (cols == 0) return;                    /* un-inited console      */
    if (b == '\r') { col = 0; return; }
    if (b == '\b') {
        /* The editor's erase cycle (backspace + space + backspace)
           needs the "move back ONTO the dying char" first step, or
           nothing erases and the cursor eats cells to the right (the
           named red: 'ab\b \bx' must land 'ax' on the truth, painted
           glyph 8 used to blank the cell AFTER the caret and drift
           it three right). Move-only, and never up into the line
           above - the text face of old stepped exactly as far and
           no farther. */
        if (col) --col;
        return;
    }
    if (b == '\n') {
        col = 0;
        if (++row >= rows) {
            row = rows - 1;
            scroll();
        }
        return;
    }

    /* M6.2: every visible byte goes through the ONE decoder. Controls
       could not be continuation bytes (validated 80..BF), so routing
       them before the decode is safe and keeps \n semantics exact. */
    r = utf8_step(&u8st, b, &cp);
    if (r < 0) {
        /* refused: the byte is not consumed as text; re-feed it once
           from the clean state so plain-ASCII after a truncated lead
           is PAINTED, per utf8.h's contract. */
        r = utf8_step(&u8st, b, &cp);
        if (r < 0) return;                    /* true garbage: nothing */
    }
    if (r == 0) return;                       /* part of a sequence    */

    draw_cell(col, row, cp);                  /* one CELL per CODEPOINT */
    cellbuf[(u64)row * cols + col] = cp;
    compo_cells_painted(col * gw, row * gh, gw, gh);
    if (++col >= cols) {
        col = 0;
        if (++row >= rows) {
            row = rows - 1;
            scroll();
        }
    }
}

/* The shared face's door: one writer at a time, end of story. */
void fbcon_putc(char c)
{
    u64 flags;

    if (cols == 0) return;                    /* un-inited console      */
    flags = irq_save();
    fbcon_putc_face(c);
    irq_restore(flags);
}

void fbcon_puts(const char *s)
{
    while (*s) fbcon_putc(*s++);
}

/* M6.4 compositor's read-back entry: repaint the whole cells under a
   pixel rect from the buffer. The sprite's move and the scroll
   repair both land here - the ONE repaint-under primitive. Pure:
   col/row and the decoder state are untouched. */
void fbcon_cells_redraw(u32 x, u32 y, u32 w, u32 h)
{
    u32 cx0, cy0, cx1, cy1;

    if (cols == 0 || w == 0 || h == 0) return;
    if (x >= fb_width() || y >= fb_height()) return;
    if (w - 1 > fb_width() - 1 - x) w = fb_width() - x;
    if (h - 1 > fb_height() - 1 - y) h = fb_height() - y;

    cx0 = x / gw;
    cy0 = y / gh;
    cx1 = (x + w - 1) / gw;
    cy1 = (y + h - 1) / gh;
    for (u32 r = cy0; r <= cy1; ++r)
        for (u32 c = cx0; c <= cx1; ++c)
            draw_cell(c, r, cellbuf[(u64)r * cols + c]);
}

void fbcon_cell_draw(u32 c, u32 r, u32 cp)
{
    if (cols == 0 || c >= cols || r >= rows) return;
    cellmark[(u64)r * cols + c] = 0;
    cellbuf[(u64)r * cols + c] = cp;
    draw_cell(c, r, cp);
    compo_cells_painted(c * gw, r * gh, gw, gh);
}

void fbcon_cell_mark(u32 c, u32 r)
{
    if (c >= cols || r >= rows) return;
    cellmark[(u64)r * cols + c] = 1;
    draw_cell(c, r, cellbuf[(u64)r * cols + c]);
    compo_cells_painted(c * gw, r * gh, gw, gh);
}

void fbcon_cell_unmark(u32 c, u32 r)
{
    if (c >= cols || r >= rows) return;
    cellmark[(u64)r * cols + c] = 0;
    draw_cell(c, r, cellbuf[(u64)r * cols + c]);
    compo_cells_painted(c * gw, r * gh, gw, gh);
}

u32 fbcon_cell_get(u32 c, u32 r)
{
    if (cols == 0 || c >= cols || r >= rows) return 0;
    return cellbuf[(u64)r * cols + c];
}
