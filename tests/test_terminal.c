#include <stdio.h>
#include <kernel/types.h>
#include "arch/x86_64/terminal.h"
static u32 cells[8][12];
static int failed, checks, marked;
u32 fbcon_cols(void) { return 12; }
int focus_band(int tid, int *row, int *height)
{ if (tid != 3 && tid != 4) return 0; *row = tid == 3 ? 4 : 0; *height = 4; return 1; }
void fbcon_cell_draw(u32 col, u32 row, u32 cp)
{ if (row >= 8 || col >= 12) { ++failed; return; } cells[row][col] = cp; }
u32 fbcon_cell_get(u32 col, u32 row) { return cells[row][col]; }
void fbcon_cell_mark(u32 col, u32 row) { marked = row * 12 + col; }
void fbcon_cell_unmark(u32 col, u32 row) { (void)col; (void)row; }
void serial_putc(char c) { (void)c; }
void console_putc(char c) { (void)c; }
void console_user_surfaces(void) {}
static void check(int good, const char *what)
{ ++checks; if (!good) ++failed; printf("%s %s\n", good ? "PASS" : "FAIL", what); }
int main(void)
{
    terminal_reset(3); terminal_reset(4);
    terminal_putc(4, 3, 'E'); terminal_putc(3, 0, 'C');
    check(cells[0][0] == 'E' && cells[4][0] == 'C', "independent output origins");
    terminal_putc(5, 3, '!');
    check(cells[4][1] == '!', "child output uses console surface");
    for (int i = 0; i < 10; ++i) { terminal_putc(3, 0, 'a' + i); terminal_putc(3, 0, '\n'); }
    check(cells[0][0] == 'E', "console scrolling preserves editor band");
    check(cells[6][0] == 'j' && cells[7][0] == 0, "scroll keeps newest line and clears last row");
    check(terminal_control(4, 0, 1, 3, 2) == 0, "cursor positioning accepted inside own band");
    terminal_putc(4, 0, 'X');
    check(cells[2][3] == 'X', "position is relative to own band");
    terminal_control(4, 0, 3, 0, 0);
    check(marked == 2 * 12 + 4, "caret tracks next text position");
    check(terminal_control(4, 0, 1, 12, 0) < 0 && terminal_control(4, 0, 1, 0, 4) < 0,
          "cursor cannot escape band");
    terminal_control(4, 0, 0, 0, 0);
    check(cells[0][0] == 0 && cells[2][3] == 0 && cells[6][0] == 'j', "editor clear preserves console");
    printf("%d checks, %d failures\n", checks, failed);
    return failed != 0;
}
