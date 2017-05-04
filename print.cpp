#include "print.h"
#include "vga.h"

namespace kernos
{

void println_color(const char* str, VGA_COLOR color)
{
    terminal_setcolor(vga_entry_color(color, VGA_COLOR_BLACK));
	terminal_writestring(str);
    terminal_newline();
}

void print_info(const char* str)
{
    println_color(str, VGA_COLOR_WHITE);
}

void print_error(const char* str)
{
    println_color(str, VGA_COLOR_RED);
}

void print_ok(const char* str)
{
    println_color(str, VGA_COLOR_GREEN);
}

void print_debug(const char* str)
{
    println_color(str, VGA_COLOR_BLUE);
}

}