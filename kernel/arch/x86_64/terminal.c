#include "terminal.h"
#include "focus.h"
#include "fbcon.h"
#include "console.h"
#include "serial.h"
#include <kernel/utf8.h>

struct terminal { u32 col, row; int caret; struct utf8_state utf8; };
static struct terminal terms[FOCUS_MAX_TIDS];

static int locate(int tid, int parent, int *first, int *height)
{
    if (focus_band(tid, first, height)) return tid;
    if (focus_band(parent, first, height)) return parent;
    return -1;
}

void terminal_reset(int tid)
{
    if (tid < 1 || tid >= FOCUS_MAX_TIDS) return;
    terms[tid].col = terms[tid].row = 0;
    terms[tid].caret = 0;
    utf8_reset(&terms[tid].utf8);
    console_user_surfaces();
}

static void clear_band(int first, int height)
{
    for (int r = first; r < first + height; ++r)
        for (u32 c = 0; c < fbcon_cols(); ++c) fbcon_cell_draw(c, r, 0);
}

static void newline(struct terminal *t, int first, int height)
{
    t->col = 0;
    if (++t->row < (u32)height) return;
    t->row = height - 1;
    for (int r = first; r < first + height - 1; ++r)
        for (u32 c = 0; c < fbcon_cols(); ++c)
            fbcon_cell_draw(c, r, fbcon_cell_get(c, r + 1));
    for (u32 c = 0; c < fbcon_cols(); ++c)
        fbcon_cell_draw(c, first + height - 1, 0);
}

static void putc_impl(int tid, int parent, char c, int mirror)
{
    int first, height;
    int id = locate(tid, parent, &first, &height);
    if (id < 0 || !fbcon_cols()) { console_putc(c); return; }
    if (mirror) serial_putc(c);
    struct terminal *t = &terms[id];
    if (c == '\n') { newline(t, first, height); return; }
    if (c == '\r') { t->col = 0; return; }
    if (c == '\b') { if (t->col) --t->col; return; }
    if (c == '\t') {
        int n = 4 - t->col % 4;
        while (n--) putc_impl(tid, parent, ' ', 0);
        return;
    }
    u32 cp;
    int n = utf8_step(&t->utf8, (u8)c, &cp);
    if (n < 0) n = utf8_step(&t->utf8, (u8)c, &cp);
    if (n <= 0) return;
    fbcon_cell_draw(t->col, first + t->row, cp);
    if (++t->col >= fbcon_cols()) newline(t, first, height);
}

void terminal_putc(int tid, int parent, char c) { putc_impl(tid, parent, c, 1); }
void terminal_paintc(int tid, int parent, char c) { putc_impl(tid, parent, c, 0); }

long terminal_control(int tid, int parent, long op, long col, long row)
{
    int first, height;
    int id = locate(tid, parent, &first, &height);
    if (id < 0) return -1;
    struct terminal *t = &terms[id];
    if (op == 2) return ((long)height << 16) | fbcon_cols();
    if (op == 3) { fbcon_cell_mark(t->col, first + t->row); t->caret = 1; return 0; }
    if (op == 0) { clear_band(first, height); terminal_reset(id); return 0; }
    if (op != 1 || col < 0 || col >= fbcon_cols() || row < 0 || row >= height)
        return -1;
    if (t->caret) fbcon_cell_unmark(t->col, first + t->row);
    t->caret = 0;
    t->col = col; t->row = row;
    utf8_reset(&t->utf8);
    return 0;
}
