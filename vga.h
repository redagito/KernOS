#pragma once

#include "types.h"

namespace kernos
{

// VGA Color constants
enum VGA_COLOR 
{
	VGA_COLOR_BLACK = 0,
	VGA_COLOR_BLUE = 1,
	VGA_COLOR_GREEN = 2,
	VGA_COLOR_CYAN = 3,
	VGA_COLOR_RED = 4,
	VGA_COLOR_MAGENTA = 5,
	VGA_COLOR_BROWN = 6,
	VGA_COLOR_LIGHT_GREY = 7,
	VGA_COLOR_DARK_GREY = 8,
	VGA_COLOR_LIGHT_BLUE = 9,
	VGA_COLOR_LIGHT_GREEN = 10,
	VGA_COLOR_LIGHT_CYAN = 11,
	VGA_COLOR_LIGHT_RED = 12,
	VGA_COLOR_LIGHT_MAGENTA = 13,
	VGA_COLOR_LIGHT_BROWN = 14,
	VGA_COLOR_WHITE = 15,
};

// Stores foreground and background color
typedef uint8_t vga_color_t;

// Creates packed vga color
vga_color_t vga_entry_color(VGA_COLOR fg, VGA_COLOR bg);

// Call first
void terminal_initialize(void);

void terminal_setcolor(vga_color_t color);
void terminal_putentryat(char c, vga_color_t color, size_t x, size_t y);
void terminal_newline();
void terminal_putchar(char c);

// Write a string with size to terminal
void terminal_write(const char* data, size_t size);

// Write a string to terminal
void terminal_writestring(const char* data);

}