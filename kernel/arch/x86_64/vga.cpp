#include <kernel/types.h>
#include "vga.h"

namespace kernos::vga {

namespace {

// Hardware reality constants; no runtime variation until we reimplement
// them on a framebuffer (see roadmap M6).
constexpr uptr BUFFER       = 0xB8000;
constexpr int  COLS         = 80;
constexpr int  ROWS         = 25;

size_t row  = 0;
size_t col  = 0;
u8     attr_current = attr(color::light_grey, color::black);

inline volatile u16& cell(int x, int y) {
    return reinterpret_cast<volatile u16*>(BUFFER)[y * COLS + x];
}

// Advance to the next line, scrolling if the last one is full.
// NOTE: this paints nothing — '\n' handling must never write a cell.
void newline() {
    col = 0;
    if (++row >= ROWS) {
        row = ROWS - 1;
        // Shift everything up by one row.
        for (int y = 1; y < ROWS; ++y)
            for (int x = 0; x < COLS; ++x)
                cell(x, y - 1) = cell(x, y);
        for (int x = 0; x < COLS; ++x)
            cell(x, ROWS - 1) = u16(' ') | u16(attr_current) << 8;
    }
}

} // namespace

void clear() {
    const u16 blank = u16(' ') | u16(attr_current) << 8;
    for (int y = 0; y < ROWS; ++y)
        for (int x = 0; x < COLS; ++x)
            cell(x, y) = blank;
    row = col = 0;
}

void set_attr(u8 new_attr) { attr_current = new_attr; }

void putc(char c) {
    if (c == '\n') {
        newline();
        return;
    }
    cell(int(col), int(row)) = u16(c) | u16(attr_current) << 8;
    if (++col >= COLS)
        newline();
}

void puts(const char* s) {
    while (*s)
        putc(*s++);
}

void puts_hex(u64 value) {
    static const char digits[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4)
        putc(digits[(value >> shift) & 0xF]);
}

} // namespace kernos::vga
