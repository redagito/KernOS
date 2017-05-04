#pragma once

#include "vga.h"

namespace kernos
{

/**
 * Print with foreground color on black background
 */
void println_color(const char* str, VGA_COLOR color);

void print_info(const char* str);
void print_debug(const char* str);

void print_ok(const char* str);
void print_error(const char* str);

}