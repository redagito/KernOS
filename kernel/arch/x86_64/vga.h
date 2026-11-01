#pragma once

/*
 * VGA text-mode console (IBM PC: buffer 0xB8000, 80x25 cells of
 * {codepoint, attribute} u16 pairs, attribute = fg | (bg << 4) per the
 * standard hardware legacy layout).
 *
 * From-scratch rules: newline scrolls; writing a '\n' must not also
 * paint the newline character; no bounds-check escape - callers cannot
 * get out of range.
 */

#include <kernel/types.h>

enum vga_color {
    VGA_BLACK      = 0x0,
    VGA_BLUE       = 0x1,
    VGA_GREEN      = 0x2,
    VGA_CYAN       = 0x3,
    VGA_RED        = 0x4,
    VGA_MAGENTA    = 0x5,
    VGA_BROWN      = 0x6,
    VGA_LIGHT_GREY = 0x7,
    VGA_DARK_GREY  = 0x8,
    VGA_LIGHT_BLUE = 0x9,
    VGA_WHITE      = 0xF,
};

/* Display cell attribute = foreground | background << 4. */
static inline u8 vga_attr(enum vga_color fg, enum vga_color bg)
{
    return (u8)fg | ((u8)bg << 4);
}

void vga_clear(void);
void vga_set_attr(u8 new_attr);
void vga_putc(char c);
void vga_puts(const char *s);
void vga_puts_hex(u64 value);   /* 16 hex digits, no 0x prefix */
