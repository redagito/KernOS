#include <kernel/types.h>
#include "vga.h"

static u64 row;
static u64 col;
static u8 attr_current = (u8)VGA_LIGHT_GREY;   /* light grey on black (0x07) */

/* Hardware reality constants; no runtime variation until we reimplement
   them on a framebuffer (see roadmap M6). */
#define VGA_BUFFER      0xB8000
#define VGA_COLS        80
#define VGA_ROWS        25

static volatile u16 *cell(int x, int y)
{
    return (volatile u16 *)(VGA_BUFFER) + (u64)y * VGA_COLS + x;
}

/* Advance to the next line, scrolling if the last one is full.
   NOTE: this paints nothing - '\n' handling must never write a cell. */
static void newline(void)
{
    col = 0;
    if (++row >= VGA_ROWS) {
        row = VGA_ROWS - 1;
        /* Shift everything up by one row. */
        for (int y = 1; y < VGA_ROWS; ++y)
            for (int x = 0; x < VGA_COLS; ++x)
                *cell(x, y - 1) = *cell(x, y);
        for (int x = 0; x < VGA_COLS; ++x)
            *cell(x, VGA_ROWS - 1) = (u16)' ' | (u16)attr_current << 8;
    }
}

void vga_clear(void)
{
    const u16 blank = (u16)' ' | (u16)attr_current << 8;
    for (int y = 0; y < VGA_ROWS; ++y)
        for (int x = 0; x < VGA_COLS; ++x)
            *cell(x, y) = blank;
    row = col = 0;
}

void vga_set_attr(u8 new_attr)
{
    attr_current = new_attr;
}

void vga_putc(char c)
{
    if (c == '\n') {
        newline();
        return;
    }
    *cell((int)col, (int)row) = (u16)c | (u16)attr_current << 8;
    if (++col >= VGA_COLS)
        newline();
}

void vga_puts(const char *s)
{
    while (*s)
        vga_putc(*s++);
}

void vga_puts_hex(u64 value)
{
    static const char digits[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4)
        vga_putc(digits[(value >> shift) & 0xF]);
}
