#include "desktop.h"
#include "console.h"
#include "cursor.h"
#include "fat32.h"
#include "fb.h"
#include "fbcon.h"
#include "heap.h"
#include "kbd.h"
#include "sched.h"
#include "serial.h"
#include <kernel/utf8.h>

/* Single CPU, IF-off syscall ownership. IRQs only enqueue input and wake.
   Each app has independent cells/events; the compositor owns all pixels.
   The back buffer is allocated and touched before cloning user CR3s. */
#define NC 160
#define NR 64
#define NQ 512
#define WALL 0x23384f
#define WALL_TOP 0x2d4862
#define PAPER 0xfaf9f5
#define PANEL 0xe6edf0
#define INK 0x263c4c
#define MUTED 0x526b7b
#define TEAL 0x257d84
#define LINE 0xb4c7cf
#define NIGHT 0x142535
#define PALE 0xdce9ec
struct window {
    int tid, x, y, w, h, visible, max, ox, oy, ow, oh;
    int cols, rows, col, row, caret, selected, dialog_selected;
    u32 cells[NC * NR];
    u8 marks[NC * NR];
    struct utf8_state utf8;
    struct desktop_event queue[NQ];
    unsigned head, tail;
    struct desktop_info info;
    struct desktop_dialog dialog;
};
static struct window wins[3]; /* Notepad, Console, My files */
static int order[3] = {0, 1, 2}, active = -1, enabled;
static int sw, sh, dx0, dy0, dx1, dy1, dirty;
static u32 *pixels;
static int dragging = -1, resizing, dragx, dragy, down, selecting;
static int menu, applications, files_page, nfiles;
static struct fat_entry files[96];

static int min(int a, int b) { return a < b ? a : b; }
static int max(int a, int b) { return a > b ? a : b; }
static int length(const char *s) {
    int n = 0;
    while (s[n])
        ++n;
    return n;
}
static void bytes(void *dst, const void *src, int n) {
    volatile u8 *d = dst;
    const u8 *s = src;
    while (n--)
        *d++ = *s++;
}
static void copy(char *d, const char *s, int n) {
    int i = 0;
    while (i < n - 1 && s[i]) {
        d[i] = s[i];
        ++i;
    }
    d[i] = 0;
}
static int same(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static void damage(int x, int y, int w, int h) {
    int x1 = min(sw, x + w), y1 = min(sh, y + h);
    x = max(0, x);
    y = max(0, y);
    if (x1 <= x || y1 <= y)
        return;
    if (!dirty) {
        dx0 = x;
        dy0 = y;
        dx1 = x1;
        dy1 = y1;
        dirty = 1;
    } else {
        dx0 = min(dx0, x);
        dy0 = min(dy0, y);
        dx1 = max(dx1, x1);
        dy1 = max(dy1, y1);
    }
}
static void all(void) { damage(0, 0, sw, sh); }
static void rect(int x, int y, int w, int h, u32 color) {
    int x1 = min(dx1, x + w), y1 = min(dy1, y + h);
    x = max(dx0, x);
    y = max(dy0, y);
    for (int yy = y; yy < y1; ++yy)
        for (int xx = x; xx < x1; ++xx)
            pixels[yy * sw + xx] = color;
}
static void rounded(int x, int y, int w, int h, u32 color) {
    rect(x + 5, y, w - 10, h, color);
    rect(x, y + 5, w, h - 10, color);
    rect(x + 2, y + 2, w - 4, h - 4, color);
}
static void glyph(int x, int y, u32 cp, u32 fg, u32 bg) {
    if (x + 8 <= dx0 || x >= dx1 || y + 16 <= dy0 || y >= dy1)
        return;
    rect(x, y, 8, 16, bg);
    for (int r = 0; r < 16; ++r) {
        u8 bits = fbcon_glyph_bits(cp, r);
        for (int c = 0; c < 8; ++c)
            if (bits & (0x80 >> c))
                rect(x + c, y + r, 1, 1, fg);
    }
}
static void text(int x, int y, const char *s, u32 fg, u32 bg, int limit) {
    for (int i = 0; s[i] && i < limit; ++i)
        glyph(x + i * 8, y, (u8)s[i], fg, bg);
}
static void number(int x, int y, unsigned value, u32 fg, u32 bg) {
    char b[12];
    int i = 11;
    b[i] = 0;
    do {
        b[--i] = '0' + value % 10;
        value /= 10;
    } while (value);
    text(x, y, b + i, fg, bg, 11);
}
static int body_y(int i) { return wins[i].y + (i == 0 ? 70 : 42); }
static void geometry(int i) {
    struct window *w = &wins[i];
    w->cols = min(NC, (w->w - 24) / 8);
    w->rows = min(NR, (w->h - (i == 0 ? 104 : 76)) / 16);
    w->col = min(w->col, w->cols - 1);
    w->row = min(w->row, w->rows - 1);
}
static int inside(int x, int y, int a, int b, int w, int h) {
    return x >= a && y >= b && x < a + w && y < b + h;
}
static void event(int i, int type, int key, int x, int y, const char *name) {
    if (i < 0 || i > 1 || !wins[i].tid)
        return;
    struct window *w = &wins[i];
    if (w->head != w->tail && (type == DESKTOP_DRAG || type == DESKTOP_RESIZE)) {
        struct desktop_event *last = &w->queue[(w->head + NQ - 1) % NQ];
        if (last->type == type) {
            last->x = x;
            last->y = y;
            return;
        }
    }
    unsigned next = (w->head + 1) % NQ;
    if (next == w->tail) {
        serial_puts("desktop: event queue full\n");
        return;
    }
    struct desktop_event *e = &w->queue[w->head];
    e->type = type;
    e->key = key;
    e->x = x;
    e->y = y;
    copy(e->name, name ? name : "", 16);
    w->head = next;
    sched_wake_all(SCHED_WQ_KBD);
    sched_wake_all(SCHED_WQ_FOCUS);
}
static void front(int i) {
    if (i < 0 || i > 2)
        return;
    if (active != i || !wins[i].visible)
        serial_puts(i == 0   ? "desktop: focus Notepad\n"
                    : i == 1 ? "desktop: focus Console\n"
                             : "desktop: focus My files\n");
    int k = 0;
    for (int j = 0; j < 3; ++j)
        if (order[j] != i)
            order[k++] = order[j];
    order[2] = i;
    active = i;
    wins[i].visible = 1;
    menu = applications = 0;
    all();
    sched_wake_all(SCHED_WQ_KBD);
    sched_wake_all(SCHED_WQ_FOCUS);
}
static void hide(int i) {
    serial_puts(i == 0   ? "desktop: hidden Notepad\n"
                : i == 1 ? "desktop: hidden Console\n"
                         : "desktop: hidden My files\n");
    wins[i].visible = 0;
    wins[i].dialog.kind = 0;
    menu = 0;
    if (active == i) {
        active = -1;
        for (int j = 2; j >= 0; --j)
            if (wins[order[j]].visible) {
                active = order[j];
                break;
            }
    }
    all();
    sched_wake_all(SCHED_WQ_KBD);
    sched_wake_all(SCHED_WQ_FOCUS);
}
static void refresh_files(void) {
    nfiles = fat_list_root(files, 96);
    if (nfiles < 0)
        nfiles = 0;
    files_page = 0;
}
static void open_file(const char *name) {
    front(0);
    if (wins[0].dialog.kind)
        return;
    event(0, DESKTOP_FILE, 0, 0, 0, name);
}
static void launch(int i) {
    if (i == 2)
        refresh_files();
    front(i);
}
static void icon(int x, int y, int kind, u32 bg) {
    if (kind == 0 || kind == 3) {
        rounded(x, y, 34, 34, kind == 0 ? 0x81c9ca : 0xacc8e1);
        rect(x + 9, y + 5, 19, 25, PAPER);
        rect(x + 7, y + 5, 3, 25, TEAL);
        for (int r = 0; r < 3; ++r)
            rect(x + 13, y + 11 + r * 5, 10, 1, 0x87a4b4);
    } else if (kind == 1) {
        rounded(x, y, 34, 34, 0x749bb9);
        rounded(x + 3, y + 6, 28, 23, NIGHT);
        text(x + 6, y + 9, ">_", PALE, NIGHT, 2);
    } else {
        rect(x + 3, y + 4, 15, 7, 0xe9c17b);
        rounded(x, y + 9, 34, 23, 0xdba75a);
        rect(x + 4, y + 14, 26, 2, 0xf4d69c);
    }
    (void)bg;
}
static void shortcut(int x, int kind, const char *label) {
    icon(x + 24, 24, kind, WALL_TOP);
    text(x + max(0, (80 - length(label) * 8) / 2), 65, label, 0xe8f0f3, WALL_TOP, 10);
}
static void controls(struct window *w) {
    int x = w->x + w->w - 93, y = w->y + 9;
    rect(x + 8, y + 12, 10, 2, MUTED);
    rect(x + 37, y + 6, 11, 10, MUTED);
    rect(x + 39, y + 8, 7, 6, PANEL);
    for (int j = 0; j < 10; ++j) {
        rect(x + 66 + j, y + 6 + j, 1, 1, MUTED);
        rect(x + 75 - j, y + 6 + j, 1, 1, MUTED);
    }
}
static const char *file_menu[] = {"New          Ctrl+N", "Open...      Ctrl+O",
                                  "Save         Ctrl+S", "Save as...", "Close        Ctrl+Q"};
static const char *edit_menu[] = {"Select all   Ctrl+A", "Cut          Ctrl+X",
                                  "Copy         Ctrl+C", "Paste        Ctrl+V"};
static void button(int x, int y, int width, const char *label, int selected) {
    rounded(x, y, width, 28, selected ? TEAL : PANEL);
    text(x + 12, y + 6, label, selected ? 0xffffff : INK, selected ? TEAL : PANEL,
         (width - 20) / 8);
}
static void dialog_draw(int i) {
    struct window *w = &wins[i];
    if (!w->dialog.kind)
        return;
    int width = min(370, w->w - 20), x = w->x + (w->w - width) / 2, y = w->y + (w->h - 162) / 2;
    rounded(x + 4, y + 5, width, 162, 0x8297a4);
    rounded(x, y, width, 162, PANEL);
    text(x + 18, y + 16, w->dialog.title, INK, PANEL, (width - 36) / 8);
    if (w->dialog.kind == 1) {
        text(x + 18, y + 44, "Filename (8.3)", MUTED, PANEL, 20);
        rect(x + 18, y + 68, width - 36, 28, PAPER);
        text(x + 26, y + 74, w->dialog.text, INK, w->dialog_selected ? 0xb7dddd : PAPER, 15);
        int len = length(w->dialog.text);
        rect(x + 26 + 8 * len, y + 75, 1, 15, TEAL);
        button(x + width - 180, y + 114, 76, "OK", 1);
        button(x + width - 96, y + 114, 78, "Cancel", 0);
    } else if (w->dialog.kind == 2) {
        text(x + 18, y + 50, "Save your changes first?", INK, PANEL, (width - 36) / 8);
        button(x + 18, y + 112, 78, "Save", 1);
        button(x + 104, y + 112, 102, "Discard", 0);
        button(x + 214, y + 112, 90, "Cancel", 0);
    } else {
        text(x + 18, y + 50, "A file with this name exists.", INK, PANEL, (width - 36) / 8);
        button(x + 18, y + 112, 102, "Replace", 1);
        button(x + 132, y + 112, 90, "Cancel", 0);
    }
}
static void draw_window(int i) {
    struct window *w = &wins[i];
    if (!w->visible)
        return;
    if (w->x + w->w + 6 <= dx0 || w->x >= dx1 || w->y + w->h + 6 <= dy0 || w->y >= dy1)
        return;
    rounded(w->x + 4, w->y + 6, w->w, w->h, 0x1d3043);
    rounded(w->x, w->y, w->w, w->h, PANEL);
    if (active == i)
        rect(w->x + 8, w->y + 8, 3, 21, TEAL);
    text(w->x + 20, w->y + 12,
         i == 0   ? w->info.title
         : i == 1 ? "Console"
                  : "My files",
         INK, PANEL, (w->w - 123) / 8);
    controls(w);
    int by = body_y(i);
    if (i == 0) {
        text(w->x + 14, w->y + 45, "File", INK, PANEL, 4);
        text(w->x + 70, w->y + 45, "Edit", INK, PANEL, 4);
        button(w->x + w->w - 91, w->y + 38, 76, "Save", 0);
    }
    u32 bg = i == 1 ? NIGHT : PAPER, fg = i == 1 ? PALE : INK;
    rect(w->x + 8, by - 2, w->w - 16, w->rows * 16 + 4, bg);
    if (i < 2) {
        for (int r = 0; r < w->rows; ++r)
            for (int c = 0; c < w->cols; ++c) {
                int n = r * NC + c;
                glyph(w->x + 12 + c * 8, by + r * 16, w->cells[n], fg, w->marks[n] ? 0xb7dddd : bg);
            }
        if (w->caret && active == i && !w->dialog.kind)
            rect(w->x + 12 + w->col * 8, by + w->row * 16, 1, 16, i == 1 ? 0x8bd3d0 : TEAL);
    } else {
        text(w->x + 18, by + 6, "KernOS disk / files", MUTED, PAPER, 30);
        int count = max(1, (w->rows * 16 - 66) / 30);
        for (int j = 0; j < count && files_page + j < nfiles; ++j) {
            int yy = by + 34 + j * 30;
            text(w->x + 22, yy, files[files_page + j].name, INK, PAPER, 13);
            number(w->x + 180, yy, files[files_page + j].size, MUTED, PAPER);
            text(w->x + w->w - 90, yy, "Open", TEAL, PAPER, 4);
        }
        button(w->x + 16, w->y + w->h - 60, 88, "Prev", 0);
        button(w->x + 112, w->y + w->h - 60, 88, "Next", 0);
    }
    int sy = w->y + w->h - 25;
    rect(w->x + 8, sy - 4, w->w - 16, 1, LINE);
    text(w->x + 13, sy,
         i == 0   ? w->info.status
         : i == 1 ? "KernOS / local console"
                  : "Click a file to open in Notepad",
         MUTED, PANEL, (w->w - 28) / 8);
    rect(w->x + w->w - 11, w->y + w->h - 9, 5, 1, MUTED);
    rect(w->x + w->w - 8, w->y + w->h - 12, 1, 4, MUTED);
    if (i == 0 && menu) {
        int mx = w->x + (menu == 1 ? 8 : 64), my = w->y + 66;
        int count = menu == 1 ? 5 : 4;
        rounded(mx + 3, my + 4, 210, count * 30 + 8, 0x8297a4);
        rounded(mx, my, 210, count * 30 + 8, PANEL);
        for (int j = 0; j < count; ++j)
            text(mx + 12, my + 10 + j * 30, menu == 1 ? file_menu[j] : edit_menu[j], INK, PANEL,
                 24);
    }
    dialog_draw(i);
}
void desktop_flush(void) {
    if (!enabled || !dirty)
        return;
    rect(dx0, dy0, dx1 - dx0, dy1 - dy0, WALL);
    rect(0, 0, sw, 103, WALL_TOP);
    shortcut(18, 0, "Notepad");
    shortcut(114, 1, "Console");
    shortcut(210, 2, "My files");
    shortcut(306, 3, "HELLO.C");
    text(sw - 160, sh - 119, "KernOS", 0x8da7bb, WALL, 10);
    text(sw - 160, sh - 95, "Clean desktop", 0x8da7bb, WALL, 14);
    for (int j = 0; j < 3; ++j)
        draw_window(order[j]);
    int dockx = (sw - 410) / 2, docky = sh - 59;
    rounded(dockx + 3, docky + 4, 410, 47, 0x172a3b);
    rounded(dockx, docky, 410, 47, PANEL);
    button(dockx + 8, docky + 9, 98, "Apps", applications);
    button(dockx + 114, docky + 9, 98, "Notepad", active == 0 && wins[0].visible);
    button(dockx + 220, docky + 9, 98, "Console", active == 1 && wins[1].visible);
    button(dockx + 326, docky + 9, 76, "Files", active == 2 && wins[2].visible);
    if (applications) {
        rounded(dockx, docky - 145, 210, 136, PANEL);
        text(dockx + 16, docky - 129, "Applications", MUTED, PANEL, 18);
        for (int j = 0; j < 3; ++j)
            text(dockx + 18, docky - 98 + j * 28,
                 j == 0   ? "Notepad"
                 : j == 1 ? "Console"
                          : "My files",
                 INK, PANEL, 18);
    }
    cursor_suspend();
    /* Only the damage rectangle is copied to the hardware framebuffer. */
    fb_rect_blit(dx0, dy0, dx1 - dx0, dy1 - dy0, pixels + dy0 * sw + dx0, sw);
    cursor_face_repair();
    dirty = 0;
}
int desktop_active(void) { return enabled; }
void desktop_init(void) {
    if (!fb_present() || fbcon_glyph_h() != 16 || fbcon_cols() * 8 != fb_width())
        return;
    sw = fb_width();
    sh = fb_height();
    if (sw < 640 || sh < 480 || sw > 1280 || sh > 1024)
        return;
    pixels = kmalloc((u64)sw * sh * 4);
    if (!pixels)
        return;
    for (int i = 0; i < sw * sh; ++i)
        pixels[i] = WALL; /* prewarm shared page tables */
    wins[0].x = 28;
    wins[0].y = 123;
    wins[0].w = min(688, sw - 76);
    wins[0].h = min(426, sh - 212);
    wins[1].w = min(584, sw - 64);
    wins[1].h = min(278, sh - 185);
    wins[1].x = sw - wins[1].w - 25;
    wins[1].y = sh - wins[1].h - 89;
    wins[2].x = (sw - 460) / 2;
    wins[2].y = 145;
    wins[2].w = 460;
    wins[2].h = min(390, sh - 230);
    for (int i = 0; i < 3; ++i)
        geometry(i);
    copy(wins[0].info.title, "HELLO.C - Notepad", 40);
    copy(wins[0].info.status, "Ready", 96);
    enabled = 1;
    console_user_surfaces();
    all();
    desktop_flush();
    serial_puts("desktop: Clean ready palette=harbor\n");
}
static int find(int tid, int parent) {
    for (int i = 0; i < 2; ++i)
        if (wins[i].tid && (wins[i].tid == tid || wins[i].tid == parent))
            return i;
    return -1;
}
int desktop_has_window(int tid, int parent) { return enabled && find(tid, parent) >= 0; }
int desktop_owner(int tid, int parent) {
    int i = find(tid, parent);
    return i >= 0 && active == i && wins[i].visible;
}
int desktop_read(int tid, char *out, int cap) {
    int i = find(tid, sched_parent_tid());
    if (i < 0 || !desktop_owner(tid, sched_parent_tid()))
        return 0;
    struct window *w = &wins[i];
    int n = 0;
    while (n < cap && w->tail != w->head) {
        struct desktop_event *e = &w->queue[w->tail];
        if (e->type == DESKTOP_KEY)
            out[n++] = e->key;
        w->tail = (w->tail + 1) % NQ;
    }
    return n;
}
static void terminal_damage(int i, int c, int r) {
    damage(wins[i].x + 12 + c * 8, body_y(i) + r * 16, 8, 16);
}
static void newline(int i) {
    struct window *w = &wins[i];
    w->col = 0;
    if (++w->row < w->rows)
        return;
    w->row = w->rows - 1;
    for (int r = 0; r < w->rows - 1; ++r)
        for (int c = 0; c < NC; ++c) {
            w->cells[r * NC + c] = w->cells[(r + 1) * NC + c];
            w->marks[r * NC + c] = w->marks[(r + 1) * NC + c];
        }
    for (int c = 0; c < NC; ++c) {
        w->cells[w->row * NC + c] = 0;
        w->marks[w->row * NC + c] = 0;
    }
    if (w->visible)
        damage(w->x, body_y(i), w->w, w->rows * 16);
}
void desktop_putc(int tid, int parent, char ch, int mirror) {
    int i = find(tid, parent);
    if (i < 0)
        return;
    if (mirror)
        serial_putc(ch);
    /* Application diagnostics belong to serial; editor uses PAINT. */
    if (i == 0 && mirror)
        return;
    struct window *w = &wins[i];
    terminal_damage(i, w->col, w->row);
    if (ch == '\n') {
        newline(i);
        return;
    }
    if (ch == '\r') {
        w->col = 0;
        return;
    }
    if (ch == '\b') {
        if (w->col)
            --w->col;
        return;
    }
    if (ch == '\t') {
        int n = 4 - w->col % 4;
        while (n--)
            desktop_putc(tid, parent, ' ', 0);
        return;
    }
    u32 cp;
    int n = utf8_step(&w->utf8, (u8)ch, &cp);
    if (n < 0)
        n = utf8_step(&w->utf8, (u8)ch, &cp);
    if (n <= 0)
        return;
    int at = w->row * NC + w->col;
    w->cells[at] = cp;
    w->marks[at] = w->selected;
    if (++w->col >= w->cols) {
        if (i == 0 && !mirror)
            w->col = w->cols - 1;
        else
            newline(i);
    }
    terminal_damage(i, w->col, w->row);
}
long desktop_term(int tid, int parent, long op, long col, long row) {
    int i = find(tid, parent);
    if (i < 0)
        return -1;
    struct window *w = &wins[i];
    if (op == 2)
        return ((long)w->rows << 16) | w->cols;
    if (op == 4) {
        w->selected = !!col;
        return 0;
    }
    if (op == 0) {
        for (int j = 0; j < NC * NR; ++j) {
            w->cells[j] = 0;
            w->marks[j] = 0;
        }
        w->col = w->row = w->caret = w->selected = 0;
        utf8_reset(&w->utf8);
        all();
        return 0;
    }
    if (op == 3) {
        w->caret = 1;
        terminal_damage(i, w->col, w->row);
        return 0;
    }
    if (op != 1 || col < 0 || row < 0 || col >= w->cols || row >= w->rows)
        return -1;
    terminal_damage(i, w->col, w->row);
    w->caret = 0;
    w->col = col;
    w->row = row;
    utf8_reset(&w->utf8);
    return 0;
}
static void maximize(int i) {
    struct window *w = &wins[i];
    if (!w->max) {
        w->ox = w->x;
        w->oy = w->y;
        w->ow = w->w;
        w->oh = w->h;
        w->x = 8;
        w->y = 8;
        w->w = sw - 16;
        w->h = sh - 80;
    } else {
        w->x = w->ox;
        w->y = w->oy;
        w->w = w->ow;
        w->h = w->oh;
    }
    w->max = !w->max;
    serial_puts(w->max ? "desktop: maximized window\n" : "desktop: restored window\n");
    geometry(i);
    event(i, DESKTOP_RESIZE, 0, 0, 0, 0);
    all();
}
static void accept_dialog(int i, int result) {
    struct window *w = &wins[i];
    int kind = w->dialog.kind;
    serial_puts((kind == 1 ? !result : result == 3) ? "desktop: dialog canceled\n"
                                                    : "desktop: dialog accepted\n");
    w->dialog.kind = 0;
    event(i, kind == 1 ? DESKTOP_FILE : DESKTOP_CONFIRM, result, 0, 0,
          kind == 1 && result ? w->dialog.text : "");
    all();
}
static void key_event(int key) {
    if (active < 0 || active > 1)
        return;
    struct window *w = &wins[active];
    if (w->dialog.kind) {
        if (key == 27) {
            accept_dialog(active, w->dialog.kind == 1 ? 0 : 3);
            return;
        }
        if (w->dialog.kind == 2) {
            if (key == 19 || key == 's' || key == '\n')
                accept_dialog(active, 1);
            else if (key == 'd')
                accept_dialog(active, 2);
            else if (key == 'c')
                accept_dialog(active, 3);
            return;
        }
        if (w->dialog.kind == 3) {
            if (key == '\n' || key == 'r')
                accept_dialog(active, 1);
            else if (key == 'c')
                accept_dialog(active, 3);
            return;
        }
        int n = length(w->dialog.text);
        if (key == 1)
            w->dialog_selected = 1;
        else if (key == '\n') {
            if (n)
                accept_dialog(active, 1);
        } else if (key == '\b') {
            if (w->dialog_selected)
                w->dialog.text[0] = 0;
            else if (n)
                w->dialog.text[n - 1] = 0;
            w->dialog_selected = 0;
        } else if (key >= 32 && key < 127 && (n < 12 || w->dialog_selected)) {
            if (w->dialog_selected)
                n = 0;
            w->dialog.text[n] = key;
            w->dialog.text[n + 1] = 0;
            w->dialog_selected = 0;
        }
        all();
        return;
    }
    if (key == 27 && (menu || applications)) {
        menu = applications = 0;
        all();
        return;
    }
    if (key == 27 && active != 0)
        return;
    event(active, DESKTOP_KEY, key, 0, 0, 0);
}
static void press(int x, int y) {
    int dockx = (sw - 410) / 2, docky = sh - 59;
    if (inside(x, y, dockx, docky, 410, 47)) {
        int k = x - dockx;
        if (k < 110) {
            applications = !applications;
            menu = 0;
            all();
        } else
            launch(k < 216 ? 0 : k < 322 ? 1 : 2);
        return;
    }
    if (applications && inside(x, y, dockx, docky - 145, 210, 136)) {
        int row = (y - (docky - 104)) / 28;
        if (row >= 0 && row < 3)
            launch(row);
        return;
    }
    applications = 0;
    for (int j = 2; j >= 0; --j) {
        int i = order[j];
        struct window *w = &wins[i];
        if (!w->visible || !inside(x, y, w->x, w->y, w->w, w->h))
            continue;
        int oldmenu = menu;
        front(i);
        menu = i == 0 ? oldmenu : 0;
        if (w->dialog.kind) {
            int width = min(370, w->w - 20), a = w->x + (w->w - width) / 2,
                b = w->y + (w->h - 162) / 2;
            if (w->dialog.kind == 1) {
                if (inside(x, y, a + width - 180, b + 114, 76, 28))
                    accept_dialog(i, 1);
                else if (inside(x, y, a + width - 96, b + 114, 78, 28))
                    accept_dialog(i, 0);
            } else if (w->dialog.kind == 3) {
                if (inside(x, y, a + 18, b + 112, 102, 28))
                    accept_dialog(i, 1);
                else if (inside(x, y, a + 132, b + 112, 90, 28))
                    accept_dialog(i, 3);
            } else if (y >= b + 112 && y < b + 140) {
                if (x >= a + 18 && x < a + 96)
                    accept_dialog(i, 1);
                else if (x >= a + 104 && x < a + 206)
                    accept_dialog(i, 2);
                else if (x >= a + 214 && x < a + 304)
                    accept_dialog(i, 3);
            }
            return;
        }
        if (i == 0 && menu) {
            int mx = w->x + (menu == 1 ? 8 : 64), my = w->y + 66, count = menu == 1 ? 5 : 4;
            if (inside(x, y, mx, my, 210, count * 30 + 8)) {
                int item = (y - my - 4) / 30;
                if (item >= 0 && item < count)
                    event(0, DESKTOP_COMMAND, (menu == 1 ? DESKTOP_NEW : DESKTOP_SELECT_ALL) + item,
                          0, 0, 0);
                menu = 0;
                all();
                return;
            }
        }
        menu = 0;
        if (y < w->y + 36) {
            if (x >= w->x + w->w - 31) {
                if (i == 0)
                    event(0, DESKTOP_COMMAND, DESKTOP_CLOSE, 0, 0, 0);
                else
                    hide(i);
                return;
            }
            if (x >= w->x + w->w - 62) {
                maximize(i);
                return;
            }
            if (x >= w->x + w->w - 93) {
                hide(i);
                return;
            }
            if (!w->max) {
                dragging = i;
                resizing = 0;
                dragx = x - w->x;
                dragy = y - w->y;
            }
            return;
        }
        if (x > w->x + w->w - 18 && y > w->y + w->h - 18 && !w->max) {
            dragging = i;
            resizing = 1;
            dragx = x - w->w;
            dragy = y - w->h;
            return;
        }
        if (i == 0 && y < w->y + 68) {
            if (x > w->x + w->w - 95)
                event(0, DESKTOP_COMMAND, DESKTOP_SAVE, 0, 0, 0);
            else {
                menu = x < w->x + 64 ? 1 : 2;
            }
            all();
            return;
        }
        if (i == 0 && y >= body_y(i) && y < body_y(i) + w->rows * 16) {
            selecting = 1;
            event(0, DESKTOP_CLICK, 0, max(0, (x - w->x - 12) / 8), max(0, (y - body_y(i)) / 16),
                  0);
        }
        if (i == 2) {
            int by = body_y(i), count = max(1, (w->rows * 16 - 66) / 30);
            if (y >= w->y + w->h - 60 && y < w->y + w->h - 32) {
                if (x < w->x + 110)
                    files_page = max(0, files_page - count);
                else if (files_page + count < nfiles)
                    files_page += count;
                all();
                return;
            }
            int row = (y - by - 34) / 30;
            if (y >= by + 34 && row >= 0 && row < count && files_page + row < nfiles)
                open_file(files[files_page + row].name);
        }
        return;
    }
    menu = 0;
    if (active < 0 || !wins[active].dialog.kind)
        active = -1;
    if (y >= 14 && y < 90 && x >= 18 && x < 402) {
        int i = (x - 18) / 96;
        if (i == 3)
            open_file("HELLO.C");
        else
            launch(i);
    }
    all();
}
void desktop_pump(void) {
    if (!enabled)
        return;
    struct input_event ev[64];
    int n;
    /* The event ring is the only desktop input source; drain its mirrored
       byte FIFO even when no app is focused, so keys never leak later. */
    char discarded[128];
    while (kbd_pull(discarded, 128)) {
    }
    while ((n = input_pull(ev, 64)) > 0)
        for (int j = 0; j < n; ++j) {
            struct input_event *e = &ev[j];
            cursor_apply(e, 1);
            int x = cursor_x(), y = cursor_y();
            if (e->type == IN_EVT_KEY)
                key_event(e->code);
            else if (e->type == IN_EVT_BTN && e->code == 0) {
                down = e->value;
                if (down)
                    press(x, y);
                else {
                    if (dragging >= 0)
                        serial_puts(resizing ? "desktop: resized window\n"
                                             : "desktop: moved window\n");
                    if (resizing && dragging >= 0)
                        event(dragging, DESKTOP_RESIZE, 0, 0, 0, 0);
                    dragging = -1;
                    resizing = selecting = 0;
                }
            } else if ((e->type == IN_EVT_RELX || e->type == IN_EVT_RELY) && down) {
                if (dragging >= 0) {
                    struct window *w = &wins[dragging];
                    if (resizing) {
                        w->w = max(dragging == 0 ? 360 : 320, min(sw - w->x - 8, x - dragx));
                        w->h = max(dragging == 0 ? 260 : 200, min(sh - w->y - 75, y - dragy));
                        geometry(dragging);
                    } else {
                        w->x = max(0, min(sw - w->w, x - dragx));
                        w->y = max(0, min(sh - w->h - 75, y - dragy));
                    }
                    all();
                } else if (selecting && active == 0)
                    event(0, DESKTOP_DRAG, 0,
                          max(0, min(wins[0].cols - 1, (x - wins[0].x - 12) / 8)),
                          max(0, min(wins[0].rows - 1, (y - body_y(0)) / 16)), 0);
            }
        }
    desktop_flush();
}
long desktop_call(int tid, long op, void *data, long arg) {
    if (!enabled)
        return -1;
    int i = find(tid, 0);
    if (op == DESKTOP_ATTACH) {
        if (arg != DESKTOP_EDITOR && arg != DESKTOP_CONSOLE)
            return -1;
        i = arg - 1;
        if (wins[i].tid && wins[i].tid != tid)
            return -1;
        wins[i].tid = tid;
        if (i == 1)
            wins[i].caret = 1;
        front(i);
        serial_puts(i == 0 ? "desktop: Notepad attached\n" : "desktop: Console attached\n");
        return desktop_term(tid, 0, 2, 0, 0);
    }
    if (i < 0)
        return -1;
    struct window *w = &wins[i];
    if (op == DESKTOP_WAIT) {
        desktop_pump();
        if (w->head == w->tail)
            return 0;
        *(struct desktop_event *)data = w->queue[w->tail];
        w->tail = (w->tail + 1) % NQ;
        return 1;
    }
    if (op == DESKTOP_INFO) {
        struct desktop_info info;
        bytes(&info, data, sizeof info);
        info.title[39] = 0;
        info.status[95] = 0;
        if (!same(w->info.title, info.title) || !same(w->info.status, info.status) ||
            w->info.dirty != info.dirty) {
            bytes(&w->info, &info, sizeof info);
            damage(w->x, w->y, w->w, 36);
            damage(w->x, w->y + w->h - 30, w->w, 30);
        }
        return 0;
    }
    if (op == DESKTOP_HIDE) {
        hide(i);
        return 0;
    }
    if (op == DESKTOP_OPEN) {
        open_file(data);
        return 0;
    }
    if (op == DESKTOP_DIALOG) {
        struct desktop_dialog d;
        bytes(&d, data, sizeof d);
        if (d.kind != 1 && d.kind != 2 && d.kind != 3)
            return -1;
        bytes(&w->dialog, &d, sizeof d);
        w->dialog.title[39] = 0;
        w->dialog.text[12] = 0;
        w->dialog_selected = 1;
        serial_puts(d.kind == 1   ? "desktop: dialog Filename\n"
                    : d.kind == 2 ? "desktop: dialog Save changes\n"
                                  : "desktop: dialog Replace\n");
        front(i);
        /* A keystroke can reach this app between its Open command and the
           dialog syscall. Hand that queued input to the new dialog rather
           than leaving it for a modal helper to ignore. The snapshot keeps
           events generated by accepting the dialog outside this replay. */
        unsigned stop = w->head;
        while (w->tail != stop) {
            struct desktop_event e = w->queue[w->tail];
            w->tail = (w->tail + 1) % NQ;
            if (e.type == DESKTOP_KEY)
                key_event(e.key);
            else if (e.type == DESKTOP_RESIZE)
                event(i, e.type, e.key, e.x, e.y, 0);
        }
        all();
        return 0;
    }
    return -1;
}
void desktop_exit(int tid) {
    for (int i = 0; i < 2; ++i)
        if (wins[i].tid == tid) {
            wins[i].tid = 0;
            hide(i);
        }
}
