#include <kernel/utf8.h>

/*
 * kernel/utf8.c - strict streaming UTF-8 (M6.2). One mind for the
 * whole tree:
 *
 *   - leads: 00..7F ASCII self-contained; C2..DF len 2; E0..EF len 3;
 *     F0..F4 len 4. C0/C1 (overlong-2 leads), F5..FF and stray
 *     continuation bytes are refused at arrival.
 *   - continuations: strictly 80..BF, shifted in 6 at a time.
 *   - the completed sequence is validated against ITS OWN length's
 *     floor (E0 80 beats the 0x800 floor only at the end), against
 *     the surrogate range D800..DFFF, and against the U+10FFFF
 *     ceiling - each refusal is a -1 + reset, never a render.
 */

static const u32 utf8_floor[5] = { 0, 0, 0x80, 0x800, 0x10000 };

void utf8_reset(struct utf8_state *st)
{
    st->acc  = 0;
    st->need = 0;
    st->len  = 0;
}

int utf8_step(struct utf8_state *st, unsigned char b, u32 *cp)
{
    if (st->need == 0) {
        if (b < 0x80) {
            *cp = b;
            return 1;
        }
        if (b >= 0xC2 && b <= 0xDF) {          /* 2-byte lead */
            st->acc  = b & 0x1F;
            st->len  = 2;
            st->need = 1;
            return 0;
        }
        if (b >= 0xE0 && b <= 0xEF) {          /* 3-byte lead */
            st->acc  = b & 0x0F;
            st->len  = 3;
            st->need = 2;
            return 0;
        }
        if (b >= 0xF0 && b <= 0xF4) {          /* 4-byte lead */
            st->acc  = b & 0x07;
            st->len  = 4;
            st->need = 3;
            return 0;
        }
        return -1;      /* C0/C1, F5..FF, or a stray continuation */
    }

    if (b < 0x80 || b > 0xBF) {
        utf8_reset(st);
        return -1;
    }
    st->acc = (st->acc << 6) | (u32)(b & 0x3F);
    --st->need;
    if (st->need) return 0;

    /* completed: validate against this sequence's own rules */
    {
        u32 v = st->acc;
        int ok = (v >= utf8_floor[st->len]) &&
                 !(v >= 0xD800 && v <= 0xDFFF) &&
                 (v <= 0x10FFFF);
        utf8_reset(st);
        if (!ok) return -1;
        *cp = v;
    }
    return 1;
}
