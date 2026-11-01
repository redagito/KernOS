#pragma once

// VGA text-mode console (IBM PC: buffer 0xB8000, 80x25 cells of
// {codepoint, attribute} u16 pairs, attribute = fg | (bg << 4) per
// the standard hardware legacy layout).
//
// From-scratch rules: newline scrolls; writing a '\n' must not also
// paint the newline character (the one latent bug the old KernOS
// carried); no bounds-check escape — callers can't get out of range.

#include <kernel/types.h>

namespace kernos::vga {

enum class color : u8 {
    black      = 0x0,
    blue       = 0x1,
    green      = 0x2,
    cyan       = 0x3,
    red        = 0x4,
    magenta    = 0x5,
    brown      = 0x6,
    light_grey = 0x7,
    dark_grey  = 0x8,
    light_blue = 0x9,
    white      = 0xF,
};

// Display cell attribute = foreground | background << 4.
constexpr u8 attr(color fg, color bg) { return u8(fg) | u8(bg) << 4; }

void clear();
void set_attr(u8 new_attr);
void putc(char c);
void puts(const char* s);
void puts_hex(u64 value);   // 16 hex digits, no 0x prefix

} // namespace kernos::vga
