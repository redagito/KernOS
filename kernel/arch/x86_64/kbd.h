#pragma once

#include <kernel/types.h>

/*
 * PS/2 keyboard via ports 0x60/0x64, IRQ1 (M2.3). Scancode set 1
 * decoded in the IRQ into a ring buffer of ASCII bytes; break codes
 * are consumed inside the driver, so the API never sees them.
 * The PS/2 mouse shares this controller's stat/data ports but emits
 * packets on IRQ12; not touched yet (M6 input layer).
 */

void kbd_init(void);

/* Nonblocking: returns the next decoded ASCII byte, 0 when empty. */
char kbd_getc(void);

/* Batch drain (M5.5): up to max decoded ASCII bytes into dst.
   Returns the count (0 = ring empty). Caller must run IF off. */
int  kbd_pull(char *dst, int max);

/* Total decoded key events since init (diagnostic). */
u64 kbd_events(void);
