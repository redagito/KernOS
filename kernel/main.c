/*
 * KernOS kernel main. M2.4: preemptive round-robin scheduler over the
 * PIT tick. Two worker threads report over serial from their own
 * stacks; kernel_main (thread 0) waits for them and prints the
 * switch count. TRAP_SELFTEST=1 adds an int3 gate.
 */

#include "arch/x86_64/vga.h"
#include "arch/x86_64/console.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/trap.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/irq.h"
#include "arch/x86_64/pit.h"
#include "arch/x86_64/mem.h"
#include "arch/x86_64/pmm.h"
#include "arch/x86_64/page.h"
#include "arch/x86_64/heap.h"
#include "arch/x86_64/pci.h"
#include "arch/x86_64/ahci.h"
#include "fat32.h"
#include <kernel/bench.h>
#include <kernel/input.h>
#include <kernel/block.h>
#include "arch/x86_64/syscall.h"
#include "arch/x86_64/sched.h"
#include "arch/x86_64/kbd.h"
#include "arch/x86_64/mouse.h"
#include "arch/x86_64/fb.h"
#include "arch/x86_64/fbcon.h"
#include "arch/x86_64/cursor.h"
#include "arch/x86_64/focus.h"

/* M5.1: the v0 syscall ABI exercised from the caller side - the same
   four-step shape userland will use (docs/syscall-abi.md). RAX carries
   the number, RDI/RSI/RDX the args, RAX comes back the return. */
static u64 sys_v0(u64 nr, u64 a1, u64 a2, u64 a3)
{
    u64 ret;
    __asm__ __volatile__(
        "int $0x80\n"
        : "=a"(ret)
        : "0"(nr), "D"(a1), "S"(a2), "d"(a3)
        : "memory");
    return ret;
}

static volatile u64 worker_reports;    /* lines printed across workers */

/* M4.3: AHCI behind the portable block layer - the seam the host unit
   tests cross with the stdio backend and the boot gate crosses with
   real DMA (identical parser on both sides).

   DMA staging: AHCI requires 4096-aligned, frame-backed buffers, while
   the portable filesystem parses out of its own plain static arrays.
   The GLUE owns that transport difference: it bounces every sector
   through one pmm frame. The host backend has no such constraint -
   exactly the asymmetry the block seam is there to hide. */
static u64 blk_stage;                /* pmm frame, 0x1000-aligned */

static int ahci_blk_read_fn(void *ctx, u64 lba, void *buf, u32 n)
{
    (void)ctx;
    if (!blk_stage)
        return -1;
    u8 *dst = (u8 *)buf;
    for (u32 s = 0; s < n; ++s) {
        if (ahci_read(lba + s, (void *)blk_stage, 1) != 0)
            return -1;
        const volatile u8 *st = (const volatile u8 *)blk_stage;
        for (u32 i = 0; i < 512; ++i)
            dst[s * 512 + i] = st[i];
    }
    return 0;
}

static int ahci_blk_write_fn(void *ctx, u64 lba, const void *buf, u32 n)
{
    (void)ctx;
    if (!blk_stage)
        return -1;
    const u8 *src = (const u8 *)buf;
    for (u32 s = 0; s < n; ++s) {
        volatile u8 *st = (volatile u8 *)blk_stage;
        for (u32 i = 0; i < 512; ++i)
            st[i] = src[s * 512 + i];
        if (ahci_write(lba + s, (void *)blk_stage, 1) != 0)
            return -1;
    }
    return 0;
}

static u64 ahci_blk_sectors_fn(void *ctx)
{
    (void)ctx;
    return ahci_sector_count();
}

static const struct block_ops ahci_blk_ops = {
    ahci_blk_read_fn, ahci_blk_write_fn, ahci_blk_sectors_fn
};

static const struct block_dev ahci_blk = { &ahci_blk_ops, 0 };

static void putdec(u64 v)
{
    char b[24];
    int i = (int)sizeof b - 1;
    b[i] = 0;
    if (!v) b[--i] = '0';
    while (v) { b[--i] = (char)('0' + v % 10); v /= 10; }
    console_puts(&b[i]);
}

static void worker(void *arg)
{
    char tag = (char)(long)arg;        /* 'A' or 'B' */
    u64 wakes = 0, reports = 0;

    console_putc(tag);
    console_puts(": enter\n");

    for (;;) {
        __asm__ volatile("hlt");       /* sleep till the next tick slice */
        wakes++;
        if (wakes % 25 == 0 && reports < 3) {
            reports++;
            worker_reports++;
            console_putc(tag);
            console_puts(": report #");
            putdec(reports);
            console_puts("\n");
        }
    }
}

void kernel_main(void)
{
    vga_clear();
    serial_init();
    console_init();          /* M5.9: the one boot log; face = VGA,
                                serial already got everything from
                                the very first printed byte */

    /* M5.6: parse the stage-2 VBE handoff FIRST - the block lives in
       boot RAM below 0x100000, and everything later in this function
       must not be able to disturb the only honest copy. */
    fb_init();

    console_puts("KernOS x86-64 kernel online\n");

    char vendor[13];
    kernos_cpu_vendor(vendor);
    console_puts("CPU vendor: ");
    console_puts(vendor);
    console_puts("\n");

    /* M3.1: the real physical memory map from the E820 boot ABI. */
    memmap_boot_init();

    /* M3.2: frame allocator. Self-test: three allocs, a deliberate
       double-free (must be refused), then balance restored. */
    pmm_init();
    {
        u64 before = pmm_free_frames();
        u64 a = pmm_alloc(), b = pmm_alloc(), c = pmm_alloc();

        console_puts("pmm: alloc ");
        console_puts_hex(a);
        console_puts(" ");
        console_puts_hex(b);
        console_puts(" ");
        console_puts_hex(c);
        console_puts("\n");

        if (a && b && c && (a & 0xFFF) == 0 && (b & 0xFFF) == 0 &&
            (c & 0xFFF) == 0 && a != b && b != c) {
            pmm_free(a);
            pmm_free(a);               /* must be refused */
            pmm_free(b);
            pmm_free(c);
            if (pmm_free_frames() == before) {
                console_puts("pmm: selftest ok\n");
            } else {
                console_puts("pmm: selftest FAILED (balance)\n");
            }
        } else {
            console_puts("pmm: selftest FAILED (alloc)\n");
        }
    }

    trap_init();
    console_puts("traps online\n");

    /* M3.3 demand paging self-test:
       touch A (fresh 2 MiB block: PD + PT + page), touch B (same
       block: page only), touch C (fresh block: PD + PT + page); read
       back what we wrote before the fault. */
    pf_init();
    {
        volatile u64 *a = (volatile u64 *)PAGE_DEMAND_BASE;
        volatile u64 *b = (volatile u64 *)(PAGE_DEMAND_BASE + 8192);
        volatile u64 *c = (volatile u64 *)(PAGE_DEMAND_BASE + 4 * 1024 * 1024);

        *a = 0x4141414141414141ULL;
        *b = 0x4242424242424242ULL;
        *c = 0x4343434343434343ULL;

        if (*a == 0x4141414141414141ULL && *b == 0x4242424242424242ULL &&
            *c == 0x4343434343434343ULL) {
            u64 f, pdn, ptn, dat;
            pf_stats(&f, &pdn, &ptn, &dat);
            console_puts("pf: ok faults=");
            console_puts_hex(f);
            console_puts(" pd=");
            console_puts_hex(pdn);
            console_puts(" pt=");
            console_puts_hex(ptn);
            console_puts(" pages=");
            console_puts_hex(dat);
            console_puts("\n");
        } else {
            console_puts("pf: selftest FAILED (readback)\n");
        }
    }

    /* M3.4 kernel heap self-test: bump allocations across fresh pages,
       pattern-filled end to end through the demand pager. */
    {
        void *h1 = kmalloc(100);
        void *h2 = kmalloc(70000);        /* crosses ~17 pages */
        void *h3 = kmalloc(0);            /* NULL by contract */

        if (h1 && h2 && h3 == 0) {
            u8 *p1 = h1, *p2 = h2;
            for (int i = 0; i < 100; ++i)   p1[i] = 0xA5;
            u8 sentinel = 1;
            for (u64 i = 0; i < 70000; ++i) { p2[i] = 0x5A; sentinel &= (p2[i] == 0x5A); }

            if (p1[0] == 0xA5 && p1[99] == 0xA5 && sentinel &&
                ((u64)h2 - (u64)h1) >= 112) {
                console_puts("heap: ok h1=");
                console_puts_hex((u64)h1);
                console_puts(" h2=");
                console_puts_hex((u64)h2);
                console_puts("\n");
            } else {
                console_puts("heap: selftest FAILED (pattern)\n");
            }
        } else {
            console_puts("heap: selftest FAILED (alloc)\n");
        }
    }

    /* M5.6 framebuffer gate: map the VBE LFB stage2 handed us, paint a
       five-point color sentinel (four corners + center, distinct RGB),
       read it all back. Positive phase = "fb: pixel echo ok"; a wrong
       readback is a named FAILED line (never a silent pass). When the
       card offered no LFB, presence of the absence line IS the evidence
       and the whole rest of the boot continues on the text console. */
    {
        u64 va = fb_map();
        if (!fb_present()) {
            console_puts("fb: absent - card offered no LFB (text console)\n");
        } else if (!va) {
            console_puts("fb: map FAILED\n");
        } else {
            const u32 w = fb_width(), h = fb_height();
            console_puts("fb: mapped ");
            putdec((u64)w);
            console_puts("x");
            putdec((u64)h);
            console_puts("x32 pitch=");
            putdec((u64)fb_pitch());
            console_puts(" phys=");
            console_puts_hex(fb_phys());
            console_puts("\n");

            fb_pixel_set(0,     0,     0x120000);   /* R 0x12 */
            fb_pixel_set(w - 1, 0,     0x001200);
            fb_pixel_set(0,     h - 1, 0x000012);
            fb_pixel_set(w - 1, h - 1, 0x201212);
            fb_pixel_set(w / 2, h / 2, 0x55AA55);

            if (fb_pixel_get(0,     0)     == 0x120000 &&
                fb_pixel_get(w - 1, 0)     == 0x001200 &&
                fb_pixel_get(0,     h - 1) == 0x000012 &&
                fb_pixel_get(w - 1, h - 1) == 0x201212 &&
                fb_pixel_get(w / 2, h / 2) == 0x55AA55) {
                console_puts("fb: pixel echo ok\n");
            } else {
                console_puts("fb: pixel echo FAILED\n");
            }

            /* M6.1 rect-substrate echo: the fill paints EXACTLY its
               area (ring guards around it stay their own color), the
               clipped corner fill paints exactly the 8x8 that is
               on-screen (fill returns counts, the claims are in the
               check), and region copy moves stamped words
               bit-for-bit. */
            {
                u64 n = fb_rect_fill(100, 100, 64, 32, 0x3366CC);
                fb_pixel_set(99,  99,  0x444444);
                fb_pixel_set(164, 99,  0x444444);
                fb_pixel_set(99,  132, 0x444444);
                fb_pixel_set(164, 132, 0x444444);
                u64 c = fb_rect_fill(w - 8, h - 8, 32, 32, 0x0A0B0C);
                fb_pixel_set(200, 300, 0x112233);
                fb_pixel_set(201, 300, 0x445566);
                fb_pixel_set(202, 300, 0x778899);
                fb_region_copy(200, 300, 3, 1, 200, 320);
                fb_region_copy(200, 300, 3, 1, 150, 300); /* sideways copy */
                if (n == 64 * 32 && c == 8 * 8 &&
                    fb_pixel_get(132, 116) == 0x3366CC &&
                    fb_pixel_get(99,  99)  == 0x444444 &&
                    fb_pixel_get(164, 99)  == 0x444444 &&
                    fb_pixel_get(99,  132) == 0x444444 &&
                    fb_pixel_get(164, 132) == 0x444444 &&
                    fb_pixel_get(w - 1, h - 1) == 0x0A0B0C &&
                    fb_pixel_get(200, 320) == 0x112233 &&
                    fb_pixel_get(201, 320) == 0x445566 &&
                    fb_pixel_get(202, 320) == 0x778899 &&
                    fb_pixel_get(150, 300) == 0x112233 &&
                    fb_pixel_get(151, 300) == 0x445566 &&
                    fb_pixel_get(152, 300) == 0x778899 &&
                    fb_pixel_get(200, 300) == 0x112233) {
                    console_puts("fb: rect echo ok\n");
                } else {
                    console_puts("fb: rect echo FAILED\n");
                }
            }
        }
    }

    /* M5.7 fbcon gate: the first thing the framebuffer ever paints.
       The embedded PSF2 blob is validated here (named refusal, boot
       continues on the text console). Evidence order: (1) scroll test
       — fill the screen with printable character rain, stamp the
       bottom row with three distinct RGB words, two pure-newline
       scrolls, read the stamps back 16*k pixels higher (the scroller
       proves itself byte-for-byte, not just visibly); (2) banner last
       so the screendump evidence shows it, with banner ink counted.
       CAREFUL: putc auto-wraps when a cell row fills — a line drawn
       with fbcon_cols() cells terminates itself, an extra '\n' would
       double-advance. */
    if (!fb_present()) {
        console_puts("fbcon: absent - keeping VGA text console\n");
    } else if (fbcon_init() < 0) {
        console_puts("fbcon: psf2 header FAILED (embedded blob)\n");
    } else {
        /* (1) full-screen fill + scroll stress, stamps after fill so
           nothing but the scroller itself ever moves them */
        {
            u32 r;
            for (r = 0; r + 1 < fbcon_rows(); ++r) {
                u32 x;
                char ch = (char)('!' + r * 11 % 95);
                for (x = 0; x < fbcon_cols(); ++x) {
                    fbcon_putc(ch);          /* auto-wraps the row */
                    ch = (char)('!' + (ch - '!' + 7) % 95);
                }
            }
        }
        {
            u32 y_stamp = (fbcon_rows() - 1) * 16;
            fb_pixel_set(0, y_stamp, 0x112233);
            fb_pixel_set(1, y_stamp, 0x445566);
            fb_pixel_set(2, y_stamp, 0x778899);
            fbcon_puts("\n\n");              /* two hard scrolls */

            u64 k = fbcon_scrolls();
            if (k == 2 &&
                fb_pixel_get(0, y_stamp - 16 * (u32)k) == 0x112233 &&
                fb_pixel_get(1, y_stamp - 16 * (u32)k) == 0x445566 &&
                fb_pixel_get(2, y_stamp - 16 * (u32)k) == 0x778899) {
                console_puts("fbcon: scroll echo ok scrolls=");
                putdec(k);
                console_puts("\n");
            } else {
                console_puts("fbcon: scroll echo FAILED\n");
            }
        }

        /* M6.2 utf8 at the glyph face: the console still feeds BYTES
           (serial stays byte-transparent); fbcon turns them into
           CODEPOINTS and then glyphs. Cursor is parked at the bottom
           row's first cell here, so cell reads are deterministic:
           (a) C3 B1 (U+00F1) paints ONE cell with the placeholder box
           the font draws for undrawn Latin-1; (b) a truncated lead
           recovers - "C3 then A" paints 'A' in the NEXT cell, not a
           swallow; (c) E2 9C ?? (U+2713, beyond the font) paints the
           same >=256 placeholder. All three compare against
           fbcon_map_ink, the font's own count for the mapped glyph. */
        {
            u32 yb = (fbcon_rows() - 1) * 16;
            u64 ink[3] = {0, 0, 0};
            fbcon_puts("\xC3\xB1");        /* U+00F1: placeholder box   */
            fbcon_puts("\xC3");            /* truncated lead, its own puts */
            fbcon_puts("A");               /* re-fed: 'A' paints next cell */
            fbcon_puts("\xE2\x9C\x93");    /* U+2713: >=256 placeholder */
            for (u32 y = yb; y < yb + 16 && y < fb_height(); ++y) {
                for (u32 x =  0; x <  8 && x < fb_width(); ++x)
                    if (fb_pixel_get(x, y) == fbcon_fg()) ++ink[0];
                for (u32 x =  8; x < 16 && x < fb_width(); ++x)
                    if (fb_pixel_get(x, y) == fbcon_fg()) ++ink[1];
                for (u32 x = 16; x < 24 && x < fb_width(); ++x)
                    if (fb_pixel_get(x, y) == fbcon_fg()) ++ink[2];
            }
            if (ink[0] == fbcon_map_ink(0xF1) && ink[0] > 0 &&
                ink[1] == fbcon_map_ink(0x41) && ink[1] > 0 &&
                ink[2] == fbcon_map_ink(0x2713) && ink[2] > 0) {
                console_puts("fbcon: utf8 echo ok\n");
            } else {
                console_puts("fbcon: utf8 echo FAILED inks: got ");
                putdec(ink[0]); console_puts("/");
                putdec(ink[1]); console_puts("/");
                putdec(ink[2]);
                console_puts(" want ");
                putdec(fbcon_map_ink(0xF1)); console_puts("/");
                putdec(fbcon_map_ink(0x41)); console_puts("/");
                putdec(fbcon_map_ink(0x2713)); console_puts("\n");
            }
        }

        /* (2) the console takes the LFB as its face and keeps the
           mirrored log going: clear the screen, print the switch
           line, then count its ink - the first mirrored line IS the
           black-on-pixels proof the console painted. */
        console_fbcon();
        console_puts("console: fbcon face (earlier lines on VGA text)\n");

        u32 ink = 0;
        for (u32 y = 0; y < 16 && y < fb_height(); ++y)
            for (u32 x = 0; x < fb_width(); ++x)
                if (fb_pixel_get(x, y) == fbcon_fg()) ++ink;
        if (ink) {
            console_puts("console: mirror ok face=fbcon ink=");
            putdec((u64)ink);
            console_puts(" mirrored=");
            putdec(console_mirrored());
            console_puts("\n");
        } else {
            console_puts("console: mirror ink FAILED\n");
        }
    }

    /* M6.3 pointer sprite: the GUI's first always-visible object.
       cursor_boot_probe runs the restore->save->paint cycle once at
       a bounded spot and proves the RESTORE reads back exactly what
       SAVE read - the movement primitive's contract, byte-real -
       then parks the arrow on screen. Movements arrive through the
       input ring's single consumer (syscall nr 7 feeds cursor_apply)
       so the SPSC discipline keeps exactly one mind. */
    {
        int cpr = cursor_boot_probe();
        if (cpr == 0)
            console_puts("cursor: probe ok parked\n");
        else if (cpr == 1)
            console_puts("cursor: probe FAILED (restore mismatch)\n");
        else
            console_puts("cursor: absent - no LFB (probe skipped)\n");
    }

    /* M6.4 z-order gate: the compositor's law, proven three ways on
       real pixels. (1) PLANT: 'W' cells under the parked sprite; move
       the sprite right one cell-set by a synthetic REL event batch -
       the cells it left are re-drawn from the console's own truth and
       read back bit-exact ('W' ink per cell), the sprite reads its
       own 25 ink at the new spot. (2) RETURN: revision stands, so the
       FAST path restores the save - cell under the old spot exact
       again, and the whole screen holds EXACTLY 25 cursor-ink pixels
       (one sprite, no crumbs of any kind). (3) SCROLL: 48 forced
       newlines ride the sprite along the screen - the scroll repair
       re-lands it, and a final full-screen scan proves 25 again and
       the sprite still at its compositor position. */
    {
        if (fb_present() && cursor_x() != 0) {
            u32 gw = 8, gh = 16;
            u32 mx = cursor_x(), my = cursor_y();
            u32 c0 = mx / gw, r0 = my / gh;
            u64 iw = fbcon_map_ink('W');
            int ok = 1;
            struct input_event ev1, ev2;
            u64 s;

            /* (1) plant under the sprite, then step right 16px */
            for (u32 c = c0 - 1; c <= c0 + 2; ++c)
                fbcon_cell_draw(c, r0, 'W');
            ev1.type = IN_EVT_RELX; ev1.code = 0;
            ev1.value = 16; ev1.seq = 0;
            cursor_apply(&ev1, 1);

            /* cells left of the NEW slot must be exact 'W' bitmaps;
               the sprite's 25 ink must sit in the new slot */
            for (u32 c = c0 - 1; c <= c0 + 1 && ok; ++c) {
                u64 n = 0;
                for (u32 y = r0 * gh; y < r0 * gh + gh; ++y)
                    for (u32 x = c * gw; x < c * gw + gw; ++x)
                        if (fb_pixel_get(x, y) == fbcon_fg()) ++n;
                if (n != iw) ok = 0;
            }
            s = 0;
            for (u32 y = cursor_y(); y < cursor_y() + 12; ++y)
                for (u32 x = cursor_x(); x < cursor_x() + 8; ++x)
                    if (fb_pixel_get(x, y) == 0xF8F8F8) ++s;
            if (s != 25) ok = 0;

            /* (2) return: fast path through the untouched save */
            if (ok) {
                ev2.type = IN_EVT_RELX; ev2.code = 0;
                ev2.value = -16; ev2.seq = 0;
                cursor_apply(&ev2, 1);
                /* the sprite-free cells of the planted row must be
                   exact 'W' again (the slot it left erased nothing),
                   and the whole screen holds exactly one sprite */
                for (u32 c = c0 - 1; c <= c0 + 2 && ok; ++c) {
                    if (c == c0) continue;      /* hosts the sprite */
                    u64 n = 0;
                    for (u32 y = r0 * gh; y < r0 * gh + gh; ++y)
                        for (u32 x = c * gw; x < c * gw + gw; ++x)
                            if (fb_pixel_get(x, y) == fbcon_fg()) ++n;
                    if (n != iw) ok = 0;
                }
            }

            /* (3) 48 scrolls: the sprite must stay exactly itself -
               screen-wide cursor ink == 25, still parked at (mx,my) */
            if (ok) {
                for (u32 k = 0; k < 48; ++k)
                    fbcon_puts("\n");
                u32 all = 0;
                for (u32 y = 0; y < fb_height(); ++y)
                    for (u32 x = 0; x < fb_width(); ++x)
                        if (fb_pixel_get(x, y) == 0xF8F8F8) ++all;
                if (all != 25 || cursor_x() != mx || cursor_y() != my)
                    ok = 0;
            }

            if (ok)
                console_puts("compo: z-order echo ok ink=25\n");
            else
                console_puts("compo: z-order echo FAILED\n");
        }
    }

    /* M6.5 (focus substrate): the registry the input era grows on -
       owner tid, claim bands, open unit. Lives AFTER the compo gate
       (the face is proven and the sprite parked) and BEFORE userland
       (the shell's claim is the first transition it will ever see). */
    focus_init();

    /* M4.1: PCI walk — the storage estate the AHCI driver will own. */
    pci_init();

    /* M4.2: AHCI bring-up + DMA round-trip against the bench disk.
       The bench disk is the FAT32 fixture tools/mkfat32 plants; the
       raw markers sit in unallocated DATA-area LBAs from bench.h so
       they never collide with file system structures. */
    ahci_init();
    {
        static const u8 marker13[BENCH_MARKER_LEN] = { 'K','E','R','N','O','S','D','I','S','K', 1, 2, 3 };
        static const u8 wrote13[BENCH_MARKER_LEN]  = { 'K','E','R','N','O','S','W','R','O','T','E', 4, 5 };

        u64 buf = pmm_alloc();
        if (buf && ahci_read(BENCH_MARKER_LBA, (void *)buf, 1) == 0) {
            volatile u8 *b = (volatile u8 *)buf;
            int ok = 1;
            for (u32 i = 0; i < BENCH_MARKER_LEN; ++i) if (b[i] != marker13[i]) ok = 0;
            console_puts(ok ? "ahci: disk marker matches\n"
                           : "ahci: disk marker MISMATCH\n");

            for (u32 i = 0; i < BENCH_MARKER_LEN; ++i) ((volatile u8 *)buf)[i] = wrote13[i];
            if (ahci_write(BENCH_WRITE_LBA, (void *)buf, 1) == 0 &&
                ahci_read(BENCH_WRITE_LBA, (void *)buf, 1) == 0) {
                int ok2 = 1;
                for (u32 i = 0; i < BENCH_MARKER_LEN; ++i) if (((volatile u8 *)buf)[i] != wrote13[i]) ok2 = 0;
                console_puts(ok2 ? "ahci: disk write-verify ok\n"
                                : "ahci: disk write-verify MISMATCH\n");
            } else {
                console_puts("ahci: disk write/read op failed\n");
            }
        } else {
            console_puts("ahci: disk marker read failed\n");
        }
        if (buf) pmm_free(buf);
    }

    /* M4.3: FAT32 over the block layer. Same fixture constants as
       tools/mkfat32 and the host unit tests (include/kernel/bench.h) -
       the parser is exercised identically at both layers. */
    {
        blk_stage = pmm_alloc();
        if (fat_mount(&ahci_blk) == 0) {
            struct fat_info fi;
            fat_stats(&fi);
            console_puts("fat: mount ok data=");
            console_puts_hex(fi.data_start);
            console_puts(" root=");
            console_puts_hex(fi.root_clus);
            console_puts(" clusters=");
            console_puts_hex(fi.total_clusters);
            console_puts("\n");

            static u8 fatbuf[512];
            u32 size = (u32)fat_read_file(BENCH_FILE, fatbuf, sizeof fatbuf);

            if (size == sizeof(BENCH_SEED) - 1) {
                u32 eq = 1;
                for (u32 i = 0; i < size; ++i)
                    if (fatbuf[i] != (u8)BENCH_SEED[i]) eq = 0;

                console_puts(eq ? "fat: seed matches: " : "fat: seed MISMATCH: ");
                for (u32 i = 0; i < size; ++i) {
                    char c = (char)fatbuf[i];
                    console_putc(c == '\n' ? ' ' : c);
                }
                console_puts("\n");

                if (eq) {
                    static u8 fatwb[512];
                    u32 wr = sizeof(BENCH_WRITE) - 1;
                    for (u32 i = 0; i < size; ++i)
                        fatwb[i] = (i < wr) ? (u8)BENCH_WRITE[i] : ' ';

                    if (fat_write_file(BENCH_FILE, fatwb, size) == (int)size) {
                        u32 back = (u32)fat_read_file(BENCH_FILE, fatbuf, sizeof fatbuf);
                        u32 eq2 = (back == size);
                        if (eq2)
                            for (u32 i = 0; i < size; ++i)
                                if (fatbuf[i] != fatwb[i]) eq2 = 0;
                        console_puts(eq2 ? "fat: write-back verified\n"
                                        : "fat: write-back MISMATCH\n");
                    } else {
                        console_puts("fat: write op refused\n");
                    }
                }
            } else {
                console_puts("fat: seed read FAILED (size=");
                console_puts_hex(size);
                console_puts(")\n");
            }
        } else {
            console_puts("fat: mount FAILED\n");
        }
    }

    irq_init();
    kbd_init();                     /* M5.5: IRQ1 now load-bearing (READ) */
    console_puts("kbd: IRQ1 armed\n");

    mouse_init();                   /* M5.8: PS/2 aux, IRQ12. Synchronous
                                       handshake with IRQ12 still masked;
                                       outcome is named at the gate below
                                       (no ack = stays asleep), not here. */
    console_puts("mouse: aux armed\n");

    sched_init();
    pit_init();
    thread_spawn("A", worker, (void *)(long)'A');
    thread_spawn("B", worker, (void *)(long)'B');
    console_puts("sched: spawned A+B\n");

    __asm__ volatile("sti");

    while (worker_reports < 6 && pit_ticks() < 1500)
        __asm__ volatile("hlt");

    console_puts("sched: reports=");
    putdec(worker_reports);
    console_puts(" switches=");
    putdec(sched_switches());
    console_puts(" ticks=");
    putdec(pit_ticks());
    console_puts("\n");

    /* M5.8 mouse gate: IRQ12 is live, so give the aux port a bounded
       window of motion, exiting early once packets arrive. Signed
       totals print as sign + magnitude (main.c's putdec is u64-only;
       deltas fit in two hex/quartet digits easily, but sign rule is
       the same either way). */
    {
        u64 t0 = pit_ticks();
        while (pit_ticks() - t0 < 500 && mouse_packets() < 3)
            __asm__ volatile("hlt");

        console_puts("mouse: ");
        if (!mouse_armed()) {
            console_puts("aux refused (no device)\n");
        } else if (mouse_packets() == 0) {
            console_puts("idle - no motion (negative control ok)\n");
        } else {
            u64 dx = mouse_dx_total();
            u64 dy = mouse_dy_total();
            i64 sdx = (i64)dx;
            i64 sdy = (i64)dy;

            /* positive gate: at least 3 packets AND real motion in
               BOTH axes. Anything decoded-shapeless is named FAILED. */
            if (mouse_packets() >= 3 && sdx != 0 && sdy != 0) {
                console_puts("gate ok events=");
                putdec(mouse_packets());
                console_puts(" dx=");
                console_puts(sdx < 0 ? "-" : "+");
                putdec((u64)(sdx < 0 ? -sdx : sdx));
                console_puts(" dy=");
                console_puts(sdy < 0 ? "-" : "+");
                putdec((u64)(sdy < 0 ? -sdy : sdy));
                console_puts(" buttons=");
                putdec((u64)mouse_buttons());
                console_puts("\n");
            } else {
                console_puts("gate FAILED (packets=");
                putdec(mouse_packets());
                console_puts(" dx=");
                putdec(dx);
                console_puts(" dy=");
                putdec(dy);
                console_puts(")\n");
            }
            if (mouse_overflows()) {
                console_puts("mouse: overflow packets dropped: ");
                putdec(mouse_overflows());
                console_puts("\n");
            }
        }
    }

    /* M5.1: syscall gate self-test - the ABI observed end to end, run
       after sched_init so getpid answers truthfully (pid == tid, and
       this thread is 0). The console write lands via the gate itself,
       so the printed line IS the evidence for syscall nr 2. */
    {
        u64 pr = sys_v0(SYSCALL_PROBE, 77, 0, 0);
        console_puts(pr == 77 ? "syscall: probe ok ret=" : "syscall: probe FAILED ret=");
        console_puts_hex(pr);
        console_puts("\n");

        u64 pid = sys_v0(SYSCALL_GETPID, 0, 0, 0);
        console_puts("syscall: getpid=");
        console_puts_hex(pid);
        console_puts("\n");
        if (pid != 0)
            console_puts("syscall: getpid not thread 0 - check tids\n");

        static const char sysmsg[] = "syscall: ABI v0 console write\n";
        u64 want = sizeof sysmsg - 1;
        u64 got  = sys_v0(SYSCALL_WRITE, STDOUT_FILENO, (u64)sysmsg, want);
        console_puts(got == want ? "syscall: write ok bytes=" : "syscall: write FAILED bytes=");
        console_puts_hex(got);
        console_puts("\n");

        u64 badfd = sys_v0(SYSCALL_WRITE, 9, (u64)sysmsg, 4);
        console_puts(badfd == (u64)SYS_EINVAL ? "syscall: bad fd refused\n"
                                             : "syscall: bad fd NOT refused\n");

        u64 unk = sys_v0(77, 0, 0, 0);
        console_puts(unk == (u64)SYS_EINVAL ? "syscall: unknown nr refused\n"
                                           : "syscall: unknown nr NOT refused\n");
    }

    /* M5.11: the exit ledger answers for its slot-reuse discipline at
       boot - permanent named evidence in the M5.10 check style. */
    if (sched_ledger_boot_check() != 0)
        console_puts("main: ledger boot check FAILED\n");

    /* M5.5: the shell becomes what the OS runs first. main.c reads it
       from the FAT fixture, spawns it, and JOINS its tid (the kernel
       thread cannot go through the gate, so join is a kernel-side
       blocking call on the same wait queue the WAIT syscall sleeps
       on). Refusal is a named phase too - the negative control pairs
       "proc: elf refused - <reason>" with this line and, above all,
       with the absence of any ksh prompt. */
    static u8 shell_img[16384];
    {
        long got = fat_read_file(BENCH_SHELL_FILE, shell_img, sizeof shell_img);
        console_puts("elf: SHELL.ELF read bytes=");
        console_puts_hex((u64)got);
        console_puts("\n");
        int stid = -1;
        if (got > 64) {
            __asm__ volatile("cli" ::: "memory");
            stid = thread_spawn_user("shell", shell_img, (u64)got);
            if (stid > 0) sched_set_args(stid, "--desktop");
            __asm__ volatile("sti" ::: "memory");
        }
        if (stid > 0) {
            console_puts("gate5: shell live tid=");
            console_puts_hex((u64)stid);
            console_puts("\n");
            int code = sched_join(stid);        /* blocks in WQ_CHILD */
            console_puts("gate5: shell exited code=");
            putdec((u64)(code < 0 ? 0 : code));
            console_puts("\n");
            console_puts("gate5: user exits total=");
            putdec(sysc_user_exits());
            console_puts("\n");
            console_puts("kbd: events=");
            putdec(kbd_events());
            console_puts("\n");
            console_puts("input: events=");
            putdec(input_delivered());
            console_puts(" dropped=");
            putdec(input_dropped());
            console_puts("\n");
        } else {
            console_puts("gate5: shell load refused\n");
            for (;;)                            /* evidence stays flat */
                __asm__ volatile("hlt");
        }
    }

#if TRAP_SELFTEST
    console_puts("selftest: int3\n");
    __asm__ volatile("int3");
#endif

    while (1)
        __asm__ volatile("hlt");
}
