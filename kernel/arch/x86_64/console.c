#include <kernel/types.h>
#include "console.h"
#include "serial.h"
#include "vga.h"
#include "fbcon.h"

/*
 * console.c — routing only: serial always, one visible face.
 *
 * The text face (VGA) is on from console_init; fbcon's LFB face
 * swaps in at the M5.7 fbcon gate (console_fbcon), which clears the
 * LFB and keeps the mirrored log going — the switch line printed
 * after the swap is the log's own continuity evidence. Serial gets
 * every byte regardless, so no face failure can cost the drivers
 * their gates. All paths go through console_putc so the mirror
 * counter counts exactly what any reader can see.
 */

#define FACE_SERIAL 0
#define FACE_TEXT   1
#define FACE_FBCON  2

static u8  face = FACE_SERIAL;
static u64 mirrored;              /* bytes handed to a visible face */

void console_init(void)
{
    mirrored = 0;
    face = FACE_TEXT;
}

void console_fbcon(void)
{
    fbcon_clear();                /* the LFB starts the mirror clean */
    face = FACE_FBCON;
}

u64  console_mirrored(void)
{
    return mirrored;
}

void console_putc(char c)
{
    serial_putc(c);
    if (face == FACE_TEXT) {
        vga_putc(c);
        mirrored++;
    } else if (face == FACE_FBCON) {
        fbcon_putc(c);
        mirrored++;
    }
    /* FACE_SERIAL (pre-init call sites, e.g. early trap dumps):
       serial got the byte, there is nothing visible to count. */
}

void console_puts(const char *s)
{
    while (*s) console_putc(*s++);
}

void console_puts_hex(u64 v)
{
    static const char digits[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4)
        console_putc(digits[(v >> shift) & 0xF]);
}
