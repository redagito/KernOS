#include "vga.h"
#include "string.h"

namespace kernos
{

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

size_t terminal_row = 0;
size_t terminal_column = 0;
vga_color_t terminal_color = 0;
uint16_t* terminal_buffer = 0;

vga_color_t vga_entry_color(VGA_COLOR fg, VGA_COLOR bg)
{
	return fg | bg << 4;
}

uint16_t vga_entry(unsigned char uc, vga_color_t color) 
{
	return (uint16_t) uc | (uint16_t) color << 8;
}

void terminal_initialize(void) 
{
	terminal_row = 0;
	terminal_column = 0;
	terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    // VGA buffer starts at 0xB8000
	terminal_buffer = (uint16_t*) 0xB8000;
	for (size_t y = 0; y < VGA_HEIGHT; y++) 
    {
		for (size_t x = 0; x < VGA_WIDTH; x++) 
        {
			const size_t index = y * VGA_WIDTH + x;
			terminal_buffer[index] = vga_entry(' ', terminal_color);
		}
	}
}

void terminal_setcolor(vga_color_t color) 
{
	terminal_color = color;
}

void terminal_putentryat(char c, vga_color_t color, size_t x, size_t y) 
{
	const size_t index = y * VGA_WIDTH + x;
	terminal_buffer[index] = vga_entry(c, color);
}

void terminal_newline()
{
    ++terminal_row;
    terminal_column = 0;
}

void terminal_putchar(char c) 
{
	// Control characters
	if (c == '\n')
	{
		terminal_newline();
	}

	terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
	if (++terminal_column == VGA_WIDTH) 
    {
		terminal_column = 0;
		if (++terminal_row == VGA_HEIGHT)
        {
			terminal_row = 0;
        }
	}
}

// Write a string with size to terminal
void terminal_write(const char* data, size_t size) 
{
	for (size_t i = 0; i < size; i++)
    {
	    terminal_putchar(data[i]);
    }
}

// Write a string to terminal
void terminal_writestring(const char* data) 
{
	terminal_write(data, string_length(data));
}

}