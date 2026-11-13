/* Exercise the real compositor without a display, screenshots or VM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef min
#undef max
#include "arch/x86_64/desktop.c"
#include <kernel/types.h>

static int checks, failures, bad_pixel, parent_tid, pointer_x, pointer_y;
static u32 screen[1024 * 768];
u32 fb_width(void) { return 1024; }
u32 fb_height(void) { return 768; }
u32 fb_present(void) { return 1024; }
u32 fbcon_glyph_h(void) { return 16; }
u32 fbcon_cols(void) { return 128; }
u8 fbcon_glyph_bits(u32 cp, u32 row) { return cp && cp != ' ' && row == 7 ? 0xff : 0; }
void fb_pixel_set(u32 x, u32 y, u32 rgb) {
    if (x >= 1024 || y >= 768)
        ++bad_pixel;
    else
        screen[y * 1024 + x] = rgb;
}
void fb_rect_blit(u32 x, u32 y, u32 w, u32 h, const u32 *src, u32 stride) {
    for (u32 r = 0; r < h; ++r)
        for (u32 c = 0; c < w; ++c)
            fb_pixel_set(x + c, y + r, src[r * stride + c]);
}
void *kmalloc(u64 size) { return calloc(1, size); }
void cursor_suspend(void) {}
void cursor_face_repair(void) {}
u32 cursor_x(void) { return pointer_x; }
u32 cursor_y(void) { return pointer_y; }
void cursor_apply(const struct input_event *ev, int n) {
    for (int j = 0; j < n; ++j) {
        if (ev[j].type == IN_EVT_RELX)
            pointer_x += ev[j].value;
        if (ev[j].type == IN_EVT_RELY)
            pointer_y += ev[j].value;
    }
}
int kbd_pull(char *out, int count) {
    (void)out;
    (void)count;
    return 0;
}
int sched_parent_tid(void) { return parent_tid; }
void sched_wake_all(int queue) { (void)queue; }
void serial_putc(char c) { (void)c; }
void serial_puts(const char *s) { (void)s; }
void console_user_surfaces(void) {}
int fat_list_root(struct fat_entry *out, int count) {
    if (count < 1)
        return -1;
    strcpy(out[0].name, "HELLO.C");
    out[0].size = 27;
    return 1;
}
static void check(int good, const char *name) {
    ++checks;
    if (!good)
        ++failures;
    printf("%s %s\n", good ? "PASS" : "FAIL", name);
}
static void drain(int tid) {
    struct desktop_event ev;
    while (desktop_call(tid, DESKTOP_WAIT, &ev, 0) > 0) {
    }
}
int main(void) {
    desktop_init();
    check(desktop_active() && screen[700 * 1024 + 10] == WALL,
          "desktop initializes with Harbor wallpaper");
    check(desktop_call(3, DESKTOP_ATTACH, 0, DESKTOP_CONSOLE) > 0, "console attaches");
    check(desktop_call(4, DESKTOP_ATTACH, 0, DESKTOP_EDITOR) > 0, "Notepad attaches");
    check(desktop_call(5, DESKTOP_ATTACH, 0, DESKTOP_EDITOR) < 0,
          "singleton editor refuses duplicate owner");
    desktop_term(4, 0, 0, 0, 0);
    desktop_putc(4, 0, 'A', 0);
    desktop_putc(3, 0, 'C', 1);
    desktop_putc(5, 3, '!', 1);
    check(wins[0].cells[0] == 'A' && wins[1].cells[0] == 'C' && wins[1].cells[1] == '!',
          "independent contents and child console inheritance");
    desktop_term(4, 0, 1, wins[0].cols - 1, wins[0].rows - 1);
    desktop_putc(4, 0, 'Z', 0);
    check(wins[0].cells[0] == 'A' &&
              wins[0].cells[(wins[0].rows - 1) * NC + wins[0].cols - 1] == 'Z',
          "painting final editor cell does not scroll document");
    desktop_flush();
    front(0);
    desktop_flush();
    int px = wins[0].x + 12, py = body_y(0);
    check(screen[py * 1024 + px] == PAPER, "editor paper uses warm white");
    hide(0);
    desktop_flush();
    check(screen[py * 1024 + px] != PAPER && wins[0].cells[0] == 'A',
          "hiding removes pixels and retains contents");
    launch(0);
    desktop_flush();
    check(screen[py * 1024 + px] == PAPER, "restore reconstructs hidden window");
    key_event('x');
    char bytes[4];
    check(desktop_read(3, bytes, 4) == 0, "unfocused console cannot consume editor keys");
    struct desktop_event e;
    check(desktop_call(4, DESKTOP_WAIT, &e, 0) == 1 && e.type == DESKTOP_KEY && e.key == 'x',
          "focused editor receives its key event");
    press(wins[0].x + 20, wins[0].y + 48);
    press(wins[0].x + 30, wins[0].y + 66 + 10 + 2 * 30);
    check(desktop_call(4, DESKTOP_WAIT, &e, 0) == 1 && e.type == DESKTOP_COMMAND &&
              e.key == DESKTOP_SAVE,
          "File menu Save dispatches to userland");
    struct desktop_dialog d = {.kind = 1, .title = "Open file", .text = ""};
    desktop_call(4, DESKTOP_DIALOG, &d, 0);
    key_event('A');
    key_event('.');
    key_event('C');
    key_event('\n');
    check(desktop_call(4, DESKTOP_WAIT, &e, 0) == 1 && e.type == DESKTOP_FILE &&
              !strcmp(e.name, "A.C"),
          "filename dialog returns typed name");
    d.kind = 2;
    desktop_call(4, DESKTOP_DIALOG, &d, 0);
    press(1000, 110);
    check(active == 0, "background click retains modal keyboard focus");
    key_event(27);
    check(desktop_call(4, DESKTOP_WAIT, &e, 0) == 1 && e.type == DESKTOP_CONFIRM && e.key == 3,
          "Escape cancels dirty confirmation");
    for (int j = 0; j < 127; ++j)
        input_push_key('b');
    desktop_pump();
    int received = 0;
    while (desktop_call(4, DESKTOP_WAIT, &e, 0) > 0) {
        if (e.type == DESKTOP_KEY && e.key == 'b')
            ++received;
    }
    check(received == 127, "full input-ring key burst survives desktop routing");
    event(0, DESKTOP_KEY, 'Q', 0, 0, 0);
    event(0, DESKTOP_KEY, '.', 0, 0, 0);
    event(0, DESKTOP_KEY, 'C', 0, 0, 0);
    event(0, DESKTOP_KEY, '\n', 0, 0, 0);
    d.kind = 1;
    d.text[0] = 0;
    desktop_call(4, DESKTOP_DIALOG, &d, 0);
    check(desktop_call(4, DESKTOP_WAIT, &e, 0) == 1 && e.type == DESKTOP_FILE &&
              !strcmp(e.name, "Q.C"),
          "filename keystrokes queued before dialog creation are retained");
    key_event(27);
    d.kind = 3;
    desktop_call(4, DESKTOP_DIALOG, &d, 0);
    check(desktop_call(4, DESKTOP_WAIT, &e, 0) == 1 && e.type == DESKTOP_CONFIRM && e.key == 3,
          "Escape queued before an overwrite prompt cancels replacement");
    for (int j = 0; j < 700; ++j)
        event(0, DESKTOP_DRAG, 0, j % 30, 1, 0);
    check(desktop_call(4, DESKTOP_WAIT, &e, 0) == 1 && e.x == 699 % 30 &&
              wins[0].head == wins[0].tail,
          "drag flood coalesces to final position without filling key queue");
    launch(2);
    check(nfiles == 1, "My files reads actual directory provider");
    open_file("HELLO.C");
    check(desktop_call(4, DESKTOP_WAIT, &e, 0) == 1 && e.type == DESKTOP_FILE &&
              !strcmp(e.name, "HELLO.C"),
          "file icon restores Notepad and requests document");
    int ow = wins[0].w;
    maximize(0);
    check(wins[0].w == 1008 && wins[0].cols <= NC, "maximize uses bounded desktop geometry");
    maximize(0);
    check(wins[0].w == ow, "restore remembers original window geometry");
    drain(4);
    front(1);
    key_event('q');
    check(desktop_read(3, bytes, 4) == 1 && bytes[0] == 'q', "console gets focused key bytes");
    desktop_term(3, 0, 1, 0, 0);
    for (int j = 0; j < 40; ++j) {
        desktop_putc(3, 0, 't', 1);
        desktop_putc(3, 0, '\n', 1);
    }
    check(wins[0].cells[0] == 'A', "console scrolling preserves covered editor contents");
    desktop_flush();
    check(!bad_pixel, "all compositor writes stay inside framebuffer");
    check(desktop_term(4, 0, 1, NC, 0) < 0 && desktop_term(4, 0, 1, 0, NR) < 0,
          "terminal rejects out-of-window coordinates");
    desktop_exit(4);
    check(!wins[0].visible && !wins[0].tid, "process exit detaches window");
    printf("%d checks, %d failures\n", checks, failures);
    free(pixels);
    return failures != 0;
}
