#include <stdio.h>
#include <kernel/types.h>
#include <kernel/ps2_mouse.h>
#include "arch/x86_64/cursor.h"

static u32 pixels[768][1024];
static int checks, failures;
u32 fb_present(void) { return 1024; }
u32 fb_width(void) { return 1024; }
u32 fb_height(void) { return 768; }
u64 fb_map(void) { return 1; }
u64 compo_revision(void) { return 0; }
void fbcon_cells_redraw(u32 x, u32 y, u32 w, u32 h)
{ (void)x; (void)y; (void)w; (void)h; }
u32 fb_pixel_get(u32 x, u32 y)
{ return x < 1024 && y < 768 ? pixels[y][x] : 0; }
void fb_pixel_set(u32 x, u32 y, u32 rgb)
{ if (x < 1024 && y < 768) pixels[y][x] = rgb; }

static void check(int good, const char *what)
{
    ++checks;
    if (!good) ++failures;
    printf("%s %s\n", good ? "PASS" : "FAIL", what);
}

static void motion(i16 dx, i16 dy)
{
    struct input_event events[2] = {
        {IN_EVT_RELX, 0, dx, 0}, {IN_EVT_RELY, 0, dy, 1}
    };
    cursor_apply(events, 2);
}

int main(void)
{
    i16 dx, dy;
    const u8 up[3] = {0x08, 10, 20};
    const u8 down[3] = {0x38, 246, 236};
    const u8 extremes[3] = {0x28, 255, 0};
    check(cursor_boot_probe() == 0, "cursor boot save/restore remains exact");
    u32 x = cursor_x(), y = cursor_y();
    ps2_mouse_motion(up, &dx, &dy);
    check(dx == 10 && dy == -20, "positive-up PS/2 motion becomes negative screen Y");
    motion(dx, dy);
    check(cursor_x() == x + 10 && cursor_y() == y - 20,
          "physical upward motion moves the pointer upward");
    ps2_mouse_motion(down, &dx, &dy);
    check(dx == -10 && dy == 20, "negative PS/2 motion decodes all nine sign bits");
    motion(dx, dy);
    check(cursor_x() == x && cursor_y() == y,
          "physical downward motion moves the pointer downward");
    ps2_mouse_motion(extremes, &dx, &dy);
    check(dx == 255 && dy == 256, "full nine-bit movement range is retained");
    motion(-32768, -32768);
    check(cursor_x() == 0 && cursor_y() == 0,
          "crossing the top-left edge clamps without wrapping to the opposite side");
    motion(32767, 32767);
    check(cursor_x() == 1024 - CUR_W && cursor_y() == 768 - CUR_H,
          "crossing the bottom-right edge clamps the entire sprite");
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
