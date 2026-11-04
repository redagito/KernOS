#include <kernel/types.h>
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
    return 0;
}

/* One glyph row: paint every pixel, fg where ink, bg where not. */
static void draw_cell(u32 cx, u32 cy, unsigned char c)
{
    const unsigned char *g;
    u32 r, x;

    if (c >= nglyphs) c = (unsigned char)0;   /* known-absent: blank    */
    g = glyphs + (u32)c * charsize;
    for (r = 0; r < gh; ++r) {
        unsigned char bits = g[r];
        for (x = 0; x < gw; ++x)
            fb_pixel_set(cx * gw + x, cy * gh + r,
                         (bits & (0x80 >> x)) ? fg : 0);
    }
}

/* Hard scroll: everything moves up one glyph row, the last row blanks. */
static void scroll(void)
{
    u32 y, x;

    for (y = gh; y < fb_height(); ++y)
        for (x = 0; x < fb_width(); ++x)
            fb_pixel_set(x, y - gh, fb_pixel_get(x, y));
    for (y = fb_height() - gh; y < fb_height(); ++y)
        for (x = 0; x < fb_width(); ++x)
            fb_pixel_set(x, y, 0);
    ++scrolls;
}

void fbcon_clear(void)
{
    u32 y, x;

    for (y = 0; y < fb_height(); ++y)
        for (x = 0; x < fb_width(); ++x)
            fb_pixel_set(x, y, 0);
    col = 0;
    row = 0;
}

void fbcon_putc(char c)
{
    if (cols == 0) return;                    /* un-inited console      */
    if (c == '\r') { col = 0; return; }
    if (c == '\n') {
        col = 0;
        if (++row >= rows) {
            row = rows - 1;
            scroll();
        }
        return;
    }
    draw_cell(col, row, (unsigned char)c);
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
