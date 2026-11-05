#pragma once

#include <kernel/types.h>

/*
 * console (M5.9): the ONE boot log. Every kernel print goes through
 * here and always reaches serial (the evidence channel the gates and
 * drivers read), PLUS whatever visible face the machine can offer:
 * VGA text from the first printed byte until the PSF2 framebuffer
 * console takes over at the M5.7 gate; serial only if the layer is
 * never init-ed (call sites that run before kernel_main's first
 * line - trap dumps stay honest through the same path, not lost).
 *
 * Backends are existing modules (vga.c, fbcon.c) - this layer owns
 * routing and accounting only: one mirrored-byte counter (the face
 * swapped once, so the log itself names what the reader saw) and a
 * face query for the boot gate. No buffers, no locks: pixels and
 * UART bytes are each written the way their module already does.
 *
 * Mirroring begins at console_init: earlier prints were serial-only
 * and stay re-read-able from the log, named by the switch line.
 */

void console_init(void);      /* mirror live from here; face = VGA */
void console_fbcon(void);     /* face switch at the M5.7 gate: fbcon
                                 takes over (LFB cleared, mirror
                                 continues mid-log, named)          */

void console_putc(char c);
void console_puts(const char *s);
void console_puts_hex(u64 v);   /* 16 hex digits, no 0x prefix      */

u64  console_mirrored(void);    /* mirrored bytes since console_init */
