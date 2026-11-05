#include <kernel/types.h>
#include <kernel/utf8.h>
#include "fbcon.h"
#include "fb.h"

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

u32 fbcon_fg(void) { return fg; }
u32 fbcon_cols(void) { return cols; }
u32 fbcon_rows(void) { return rows; }
u64 fbcon_scrolls(void) { return scrolls; }

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
    col = 0;
    row = 0;
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
                         (bits & (0x80 >> x)) ? fg : 0);
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
}

void fbcon_clear(void)
{
    fb_rect_fill(0, 0, fb_width(), fb_height(), 0);
    col = 0;
    row = 0;
}

void fbcon_putc(char c)
{
    unsigned char b = (unsigned char)c;
    u32 cp;
    int r;

    if (cols == 0) return;                    /* un-inited console      */
    if (b == '\r') { col = 0; return; }
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
    if (++col >= cols) {
        col = 0;
        if (++row >= rows) {
            row = rows - 1;
            scroll();
        }
    }
}

void fbcon_puts(const char *s)
{
    while (*s) fbcon_putc(*s++);
}
