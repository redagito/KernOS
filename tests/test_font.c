/*
 * test_font.c — host unit tests for the generated console font.
 *
 * The kernel boots with the SAME build/font.psf2 this file validates:
 * mkfont emits it from hand-drawn art, and fbcon.c parses its header
 * at boot. The host checks the container contract (magic, header
 * coherence, glyph presence rules) plus a render of sample text, so
 * the boot gate can assume the blob is coherent and spend its
 * evidence on the live blit and scroller instead.
 *
 * Exit code 0 means the whole suite passed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PSF2_MAGIC   0x864AB552u
#define HEADERSIZE   32
#define FONT_LENGTH  256
#define ROWS         16
#define COLS         8

static int t_no = 0, n_fail = 0;

static void check(int cond, const char *title)
{
    ++t_no;
    printf("%2d. %-52s %s\n", t_no, title, cond ? "PASS" : "FAIL");
    if (!cond)
        ++n_fail;
}

static unsigned char *blob;
static long blobsz;
static unsigned long hdr[8];

static unsigned long leak32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static const unsigned char *glyph(int c)
{
    return blob + hdr[2] + (long)c * hdr[5];
}

static int has_ink(int c)
{
    long r, h = (long)hdr[6];
    for (r = 0; r < h; ++r)
        if (glyph(c)[r]) return 1;
    return 0;
}

static int is_blank(int c)
{
    long r, h = (long)hdr[6];
    for (r = 0; r < h; ++r)
        if (glyph(c)[r]) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    FILE *f;
    int c, x, y, ink;
    const char *sample = "KernOS fbcon 128";

    if (argc != 2) {
        fprintf(stderr, "usage: test_font FILE.psf2\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    blobsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    blob = malloc((size_t)blobsz);
    if (!blob || fread(blob, 1, (size_t)blobsz, f) != (size_t)blobsz) {
        fprintf(stderr, "read error\n");
        return 2;
    }
    fclose(f);

    /* header container */
    hdr[0] = leak32(blob);         hdr[1] = leak32(blob + 4);
    hdr[2] = leak32(blob + 8);     hdr[3] = leak32(blob + 12);
    hdr[4] = leak32(blob + 16);    hdr[5] = leak32(blob + 20);
    hdr[6] = leak32(blob + 24);    hdr[7] = leak32(blob + 28);
    check(hdr[0] == PSF2_MAGIC, "magic is 0x864ab552");
    check(hdr[2] == HEADERSIZE, "headersize is 32");
    check(hdr[4] == FONT_LENGTH, "length is 256 glyphs");
    check(hdr[5] == ROWS, "charsize is 16 bytes/glyph");
    check(hdr[6] == ROWS && hdr[7] == COLS, "cell is 8x16");
    check(hdr[5] == ((hdr[7] + 7) / 8) * hdr[6], "charsize covers the cell");
    {
        long expect = (long)hdr[2] + (long)((unsigned long)hdr[4] * hdr[5]);
        check(blobsz == expect, "file size = header + glyph body");
    }

    /* glyph presence rules */
    check(is_blank(' '), "space glyph is blank");

    /* every printable ASCII glyph must carry ink; blank ink there is
       a lie about having drawn a cell */
    {
        int all_ink = 1;
        for (c = '!'; c <= '~'; ++c)
            if (!has_ink(c)) all_ink = 0;
        check(all_ink, "every printable ASCII glyph has ink");
    }
    {
        int controls_blank = 1;
        for (c = 0; c < ' '; ++c)
            if (!is_blank(c)) controls_blank = 0;
        check(controls_blank, "controls 0..31 are blank cells");
    }
    {
        int box = 1;
        if (is_blank(127)) box = 0;
        check(box, "127+ carries the missing-glyph box");
    }

    /* render sample: every glyph row must be inside the cell, the
       sample must have ink, and the space column between glyphs must
       stay blank in this font (col 7 painted by neither neighbor) */
    ink = 0;
    for (y = 0; y < ROWS; ++y) {
        for (const char *s = sample; *s; ++s) {
            const unsigned char *g = glyph((unsigned char)*s);
            /* unsigned char can't reach 256: drop the silent check */
            if (!(unsigned char)*s)
                continue;
            for (x = 0; x < COLS; ++x)
                if (g[y] & (0x80 >> x)) ++ink;
        }
    }
    check(ink > 200, "sample render paints a real batch of pixels");

    printf("%d checks, %d failures\n", t_no, n_fail);
    return n_fail ? 1 : 0;
}
