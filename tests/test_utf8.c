/*
 * test_utf8.c - host unit tests for the strict streaming UTF-8 decoder.
 *
 * The kernel console (and the M6 terminal) feeds bytes through the
 * SAME kernel/utf8.c compiled here. The suite drives every contract
 * branch: ASCII passthrough, 2/3/4-byte sequences (incl. exactly the
 * floor and ceiling codepoints), the refusal classes (C0/C1 leads,
 * F5..FF, stray continuations, truncated-in-the-middle resync,
 * overlongs of every length, surrogates, above-ceiling), and the
 * documented re-feed semantics a decoder-consumer relies on.
 *
 * Exit code 0 means the whole suite passed.
 */
#include <stdio.h>
#include <kernel/utf8.h>

static int t_no = 0, n_fail = 0;

static void check(int cond, const char *title)
{
    ++t_no;
    printf("%2d. %-58s %s\n", t_no, title, cond ? "PASS" : "FAIL");
    if (!cond)
        ++n_fail;
}

/* Feed a byte string, collecting emitted codepoints; return the count.
   The last result (1 emitted / 0 pending / -1 refused) goes to *last. */
static int feed(struct utf8_state *st, const unsigned char *s, unsigned n,
                u32 out[], unsigned outcap, int *last)
{
    int emitted = 0;
    *last = 0;
    for (unsigned i = 0; i < n; ++i) {
        u32 cp = 0xDEADBEEF;
        int r = utf8_step(st, s[i], &cp);
        if (r == 1) {
            *last = 1;
            if (out && emitted < (int)outcap)
                out[emitted++] = cp;
        } else if (r == -1) {
            *last = -1;
        }
    }
    return emitted;
}

int main(void)
{
    struct utf8_state st;
    u32 out[8];
    int last;

    /* ASCII passthrough, byte at a time */
    utf8_reset(&st);
    check(feed(&st, (const unsigned char *)"A", 1, out, 8, &last) == 1 &&
          out[0] == 'A' && last == 1, "ASCII 'A' emits in one step");
    check(feed(&st, (const unsigned char *)"~", 1, out + 1, 8, &last) == 1 &&
          out[1] == 0x7E, "ASCII tilde emits 0x7E");

    /* the drawn boundaries: NUL emits, DEL emits (identity glyphs) */
    {
        unsigned char b0 = 0, del = 0x7F;
        utf8_reset(&st);
        feed(&st, &b0, 1, out + 2, 8, &last);
        check(out[2] == 0 && last == 1, "U+0000 emits (glyph 0: blank)");
        utf8_reset(&st);
        feed(&st, &del, 1, out + 3, 8, &last);
        check(out[3] == 0x7F && last == 1, "U+007F emits (identity placeholder row)");
    }

    /* 2-byte: U+00F1 (n with tilde), floor and top of its range */
    {
        const unsigned char f1[] = {0xC3, 0xB1};
        const unsigned char c8[] = {0xC2, 0x80};
        const unsigned char bf[] = {0xDF, 0xBF};
        utf8_reset(&st);
        check(feed(&st, f1, 2, out, 8, &last) == 1 && out[0] == 0xF1,
              "C3 B1 decodes to U+00F1");
        utf8_reset(&st);
        check(feed(&st, c8, 2, out + 1, 8, &last) == 1 && out[1] == 0x80,
              "C2 80 decodes to exactly U+0080 (the 2-byte floor)");
        utf8_reset(&st);
        check(feed(&st, bf, 2, out + 2, 8, &last) == 1 && out[2] == 0x7FF,
              "DF BF decodes to exactly U+07FF (the 2-byte ceiling)");
    }

    /* 3-byte: euro sign, 3-byte floor */
    {
        const unsigned char eu[] = {0xE2, 0x82, 0xAC};
        utf8_reset(&st);
        check(feed(&st, eu, 3, out, 8, &last) == 1 && out[0] == 0x20AC,
              "E2 82 AC decodes to U+20AC");
    }
    {
        const unsigned char f3[] = {0xE0, 0xA0, 0x80};
        utf8_reset(&st);
        check(feed(&st, f3, 3, out + 1, 8, &last) == 1 && out[1] == 0x800,
              "E0 A0 80 decodes to exactly U+0800 (3-byte floor)");
    }

    /* 4-byte: exactly U+10000 and exactly U+10FFFF */
    {
        const unsigned char f4[] = {0xF0, 0x90, 0x80, 0x80};
        const unsigned char mx[] = {0xF4, 0x8F, 0xBF, 0xBF};
        utf8_reset(&st);
        check(feed(&st, f4, 4, out, 8, &last) == 1 && out[0] == 0x10000,
              "F0 90 80 80 decodes to exactly U+10000 (4-byte floor)");
        utf8_reset(&st);
        check(feed(&st, mx, 4, out + 1, 8, &last) == 1 && out[1] == 0x10FFFF,
              "F4 8F BF BF decodes to exactly U+10FFFF (the ceiling)");
    }

    /* refused leads */
    {
        const unsigned char c0[]={0xC0,0x80}, c1[]={0xC1,0x9F}, f5[]={0xF5,0x80,0x80,0x80}, fe[]={0xFE};
        utf8_reset(&st);
        check(feed(&st, c0, 2, out, 8, &last) == 0 && last == -1,
              "C0 80 (overlong lead) emits nothing, ends refused");
        utf8_reset(&st);
        check(feed(&st, c1, 2, out, 8, &last) == 0 && last == -1,
              "C1 9F (overlong lead) ends refused");
        utf8_reset(&st);
        check(feed(&st, f5, 4, out, 8, &last) == 0 && last == -1,
              "F5 lead (beyond ceiling domain) refused at arrival");
        utf8_reset(&st);
        check(feed(&st, fe, 1, out, 8, &last) == 0 && last == -1,
              "FE lone byte refused at arrival");
    }

    /* stray continuation, and the refusal is not a swallow of the
       NEXT byte: C3 '0x41' resyncs - refused lead + re-fed 'A' */
    {
        const unsigned char stray[] = {0x80};
        utf8_reset(&st);
        check(feed(&st, stray, 1, out, 8, &last) == 0 && last == -1,
              "lone 0x80 refused, emits nothing");
        utf8_reset(&st);
        /* decoder-consumer protocol per utf8.h: on -1, re-feed ONCE */
        u32 cp1 = 0xDEADBEEF, cp2 = 0xDEADBEEF;
        int r1 = utf8_step(&st, 0xC3, &cp1);
        int r2 = utf8_step(&st, 0x41, &cp2);   /* 'A' is not a continuation */
        check(r1 >= 0 && r2 == -1, "C3 then 'A': 'A' refused as continuation");
        int r3 = utf8_step(&st, 0x41, &cp2);   /* the documented re-feed */
        check(r3 == 1 && cp2 == 'A', "re-feed of 'A' from the clean state emits");
    }

    /* overlong encodings of every length refuse at COMPLETION */
    {
        const unsigned char o2[] = {0xC0, 0xAF};
        const unsigned char o3[] = {0xE0, 0x80, 0xAF};
        const unsigned char o4[] = {0xF0, 0x80, 0x80, 0xAF};
        utf8_reset(&st);
        check(feed(&st, o2, 2, out, 8, &last) == 0 && last == -1,
              "C0 AF (overlong '/') refuses on completion");
        utf8_reset(&st);
        check(feed(&st, o3, 3, out, 8, &last) == 0 && last == -1,
              "E0 80 AF (overlong '/') refuses on completion");
        utf8_reset(&st);
        check(feed(&st, o4, 4, out, 8, &last) == 0 && last == -1,
              "F0 80 80 AF (overlong '/') refuses on completion");
    }

    /* surrogates and above-ceiling refuse */
    {
        const unsigned char sr[] = {0xED, 0xA0, 0x80};   /* U+D800 */
        const unsigned char oo[] = {0xF4, 0x90, 0x80, 0x80}; /* U+110000 */
        utf8_reset(&st);
        check(feed(&st, sr, 3, out, 8, &last) == 0 && last == -1,
              "ED A0 80 (surrogate U+D800) refuses");
        utf8_reset(&st);
        check(feed(&st, oo, 4, out, 8, &last) == 0 && last == -1,
              "F4 90 80 80 (U+110000) refuses");
    }

    /* a lead truncated by \n does not poison the newline: the CONSUMER
       routes controls before the decoder, but the decoder itself must
       refuse a control arriving mid-sequence and resync cleanly */
    {
        const unsigned char seq[] = {0xC2, 0x0A, 0xC2, 0xB5};
        utf8_reset(&st);
        int n = feed(&st, seq, 4, out, 8, &last);
        check(n == 1 && out[0] == 0xB5 && last == 1,
              "control mid-sequence: refusal, next lead decodes U+00B5");
    }

    /* state struct survives a full reset to a fresh decode */
    utf8_reset(&st);
    {
        const unsigned char s2[] = {0xC3, 0xA4};   /* a */
        check(feed(&st, s2, 2, out, 8, &last) == 1 && out[0] == 0xE4,
              "decoder reuses cleanly after reset (C3 A4 -> U+00E4)");
    }

    printf("utf8: %d checks, %d failures\n", t_no, n_fail);
    return n_fail == 0 ? 0 : 1;
}
