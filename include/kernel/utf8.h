#pragma once

#include <kernel/types.h>

/*
 * kernel/utf8.h - strict streaming UTF-8 decode (M6.2), portable C so
 * the host suite compiles the very object the kernel and console run.
 *
 * "Text is UTF-8 from the start" needs one decoder shared by every
 * byte-stream consumer (the console faces first); it lives here so
 * there is exactly one mind to audit (overlongs, surrogates and
 * >U+10FFFF are refused, not rendered).
 */

struct utf8_state {
    u32 acc;                  /* shifted-in payload so far   */
    u32 need;                 /* continuation bytes still owed */
    u32 len;                  /* total bytes of the sequence  */
};

/* back to the clean state (a begin, or after any refusal) */
void utf8_reset(struct utf8_state *st);

/*
 * Feed ONE byte.
 *
 *   1 - a codepoint is out in *cp; the state is clean again.
 *   0 - the byte is accepted into a pending sequence; keep feeding.
 *   -1 - the byte cannot continue whatever is pending (a stray
 *        continuation, an overlong lead, or plain garbage): the state
 *        RESETS and the byte is NOT consumed as text. The caller
 *        re-feeds the same byte once, from the now-clean state - that
 *        is how a lead followed by plain ASCII loses nothing ("C3 41"
 *        is a refused lead PLUS 'A', not a swallowed byte). ASCII
 *        re-feeds emit; true garbage -1s again and draws nothing.
 */
int utf8_step(struct utf8_state *st, unsigned char b, u32 *cp);
