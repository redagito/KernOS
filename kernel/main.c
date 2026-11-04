/*
 * KernOS kernel main. M2.4: preemptive round-robin scheduler over the
 * PIT tick. Two worker threads report over serial from their own
 * stacks; kernel_main (thread 0) waits for them and prints the
 * switch count. TRAP_SELFTEST=1 adds an int3 gate.
 */

#include "arch/x86_64/vga.h"
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
#include <kernel/block.h>
#include "arch/x86_64/syscall.h"
#include "arch/x86_64/sched.h"
#include "arch/x86_64/kbd.h"
#include "arch/x86_64/mouse.h"
#include "arch/x86_64/fb.h"
#include "arch/x86_64/fbcon.h"

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
    serial_puts(&b[i]);
}

static void worker(void *arg)
{
    char tag = (char)(long)arg;        /* 'A' or 'B' */
    u64 wakes = 0, reports = 0;

    serial_putc(tag);
    serial_puts(": enter\n");

    for (;;) {
        __asm__ volatile("hlt");       /* sleep till the next tick slice */
        wakes++;
        if (wakes % 25 == 0 && reports < 3) {
            reports++;
            worker_reports++;
            serial_putc(tag);
            serial_puts(": report #");
            putdec(reports);
            serial_puts("\n");
        }
    }
}

void kernel_main(void)
{
    vga_clear();
    serial_init();

    /* M5.6: parse the stage-2 VBE handoff FIRST - the block lives in
       boot RAM below 0x100000, and everything later in this function
       must not be able to disturb the only honest copy. */
    fb_init();

    vga_puts("KernOS x86-64 kernel online\n");
    serial_puts("KernOS x86-64 kernel online\n");

    char vendor[13];
    kernos_cpu_vendor(vendor);
    vga_puts("CPU vendor: ");
    vga_puts(vendor);
    vga_puts("\n");
    serial_puts("CPU vendor: ");
    serial_puts(vendor);
    serial_puts("\n");

    /* M3.1: the real physical memory map from the E820 boot ABI. */
    memmap_boot_init();

    /* M3.2: frame allocator. Self-test: three allocs, a deliberate
       double-free (must be refused), then balance restored. */
    pmm_init();
    {
        u64 before = pmm_free_frames();
        u64 a = pmm_alloc(), b = pmm_alloc(), c = pmm_alloc();

        serial_puts("pmm: alloc ");
        serial_puts_hex(a);
        serial_puts(" ");
        serial_puts_hex(b);
        serial_puts(" ");
        serial_puts_hex(c);
        serial_puts("\n");

        if (a && b && c && (a & 0xFFF) == 0 && (b & 0xFFF) == 0 &&
            (c & 0xFFF) == 0 && a != b && b != c) {
            pmm_free(a);
            pmm_free(a);               /* must be refused */
            pmm_free(b);
            pmm_free(c);
            if (pmm_free_frames() == before) {
                serial_puts("pmm: selftest ok\n");
            } else {
                serial_puts("pmm: selftest FAILED (balance)\n");
            }
        } else {
            serial_puts("pmm: selftest FAILED (alloc)\n");
        }
    }

    trap_init();
    serial_puts("traps online\n");

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
            serial_puts("pf: ok faults=");
            serial_puts_hex(f);
            serial_puts(" pd=");
            serial_puts_hex(pdn);
            serial_puts(" pt=");
            serial_puts_hex(ptn);
            serial_puts(" pages=");
            serial_puts_hex(dat);
            serial_puts("\n");
        } else {
            serial_puts("pf: selftest FAILED (readback)\n");
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
                serial_puts("heap: ok h1=");
                serial_puts_hex((u64)h1);
                serial_puts(" h2=");
                serial_puts_hex((u64)h2);
                serial_puts("\n");
            } else {
                serial_puts("heap: selftest FAILED (pattern)\n");
            }
        } else {
            serial_puts("heap: selftest FAILED (alloc)\n");
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
            serial_puts("fb: absent - card offered no LFB (text console)\n");
        } else if (!va) {
            serial_puts("fb: map FAILED\n");
        } else {
            const u32 w = fb_width(), h = fb_height();
            serial_puts("fb: mapped ");
            putdec((u64)w);
            serial_puts("x");
            putdec((u64)h);
            serial_puts("x32 pitch=");
            putdec((u64)fb_pitch());
            serial_puts(" phys=");
            serial_puts_hex(fb_phys());
            serial_puts("\n");

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
                serial_puts("fb: pixel echo ok\n");
            } else {
                serial_puts("fb: pixel echo FAILED\n");
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
        serial_puts("fbcon: absent - keeping VGA text console\n");
    } else if (fbcon_init() < 0) {
        serial_puts("fbcon: psf2 header FAILED (embedded blob)\n");
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
                serial_puts("fbcon: scroll echo ok scrolls=");
                putdec(k);
                serial_puts("\n");
            } else {
                serial_puts("fbcon: scroll echo FAILED\n");
            }
        }

        /* (2) banner last: rows 0..2, then one printed line-up of the
           font for the screendump. */
        {
            u32 ink = 0;
            fbcon_clear();
            fbcon_puts("KernOS x86-64\n");
            fbcon_puts("fbcon: 8x16 psf2 console (M5.7)\n");
            fbcon_puts("ksh still on serial (M6 GUI later)\n");

            for (u32 y = 0; y < 16 && y < fb_height(); ++y)
                for (u32 x = 0; x < fb_width(); ++x)
                    if (fb_pixel_get(x, y) == fbcon_fg()) ++ink;
            if (ink) {
                serial_puts("fbcon: banner ink px=");
                putdec((u64)ink);
                serial_puts("\n");
            } else {
                serial_puts("fbcon: banner ink FAILED\n");
            }

            fbcon_puts("PSF2 A-Z 0-9: !\"#$%&'()*+,-./0123456789:<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~\n");
        }
    }

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
            serial_puts(ok ? "ahci: disk marker matches\n"
                           : "ahci: disk marker MISMATCH\n");

            for (u32 i = 0; i < BENCH_MARKER_LEN; ++i) ((volatile u8 *)buf)[i] = wrote13[i];
            if (ahci_write(BENCH_WRITE_LBA, (void *)buf, 1) == 0 &&
                ahci_read(BENCH_WRITE_LBA, (void *)buf, 1) == 0) {
                int ok2 = 1;
                for (u32 i = 0; i < BENCH_MARKER_LEN; ++i) if (((volatile u8 *)buf)[i] != wrote13[i]) ok2 = 0;
                serial_puts(ok2 ? "ahci: disk write-verify ok\n"
                                : "ahci: disk write-verify MISMATCH\n");
            } else {
                serial_puts("ahci: disk write/read op failed\n");
            }
        } else {
            serial_puts("ahci: disk marker read failed\n");
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
            serial_puts("fat: mount ok data=");
            serial_puts_hex(fi.data_start);
            serial_puts(" root=");
            serial_puts_hex(fi.root_clus);
            serial_puts(" clusters=");
            serial_puts_hex(fi.total_clusters);
            serial_puts("\n");

            static u8 fatbuf[512];
            u32 size = (u32)fat_read_file(BENCH_FILE, fatbuf, sizeof fatbuf);

            if (size == sizeof(BENCH_SEED) - 1) {
                u32 eq = 1;
                for (u32 i = 0; i < size; ++i)
                    if (fatbuf[i] != (u8)BENCH_SEED[i]) eq = 0;

                serial_puts(eq ? "fat: seed matches: " : "fat: seed MISMATCH: ");
                for (u32 i = 0; i < size; ++i) {
                    char c = (char)fatbuf[i];
                    serial_putc(c == '\n' ? ' ' : c);
                }
                serial_puts("\n");

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
                        serial_puts(eq2 ? "fat: write-back verified\n"
                                        : "fat: write-back MISMATCH\n");
                    } else {
                        serial_puts("fat: write op refused\n");
                    }
                }
            } else {
                serial_puts("fat: seed read FAILED (size=");
                serial_puts_hex(size);
                serial_puts(")\n");
            }
        } else {
            serial_puts("fat: mount FAILED\n");
        }
    }

    irq_init();
    kbd_init();                     /* M5.5: IRQ1 now load-bearing (READ) */
    serial_puts("kbd: IRQ1 armed\n");

    mouse_init();                   /* M5.8: PS/2 aux, IRQ12. Synchronous
                                       handshake with IRQ12 still masked;
                                       outcome is named at the gate below
                                       (no ack = stays asleep), not here. */
    serial_puts("mouse: aux armed\n");

    sched_init();
    pit_init();
    thread_spawn("A", worker, (void *)(long)'A');
    thread_spawn("B", worker, (void *)(long)'B');
    serial_puts("sched: spawned A+B\n");

    __asm__ volatile("sti");

    while (worker_reports < 6 && pit_ticks() < 1500)
        __asm__ volatile("hlt");

    serial_puts("sched: reports=");
    putdec(worker_reports);
    serial_puts(" switches=");
    putdec(sched_switches());
    serial_puts(" ticks=");
    putdec(pit_ticks());
    serial_puts("\n");

    /* M5.8 mouse gate: IRQ12 is live, so give the aux port a bounded
       window of motion, exiting early once packets arrive. Signed
       totals print as sign + magnitude (main.c's putdec is u64-only;
       deltas fit in two hex/quartet digits easily, but sign rule is
       the same either way). */
    {
        u64 t0 = pit_ticks();
        while (pit_ticks() - t0 < 500 && mouse_packets() < 3)
            __asm__ volatile("hlt");

        serial_puts("mouse: ");
        if (!mouse_armed()) {
            serial_puts("aux refused (no device)\n");
        } else if (mouse_packets() == 0) {
            serial_puts("idle - no motion (negative control ok)\n");
        } else {
            u64 dx = mouse_dx_total();
            u64 dy = mouse_dy_total();
            i64 sdx = (i64)dx;
            i64 sdy = (i64)dy;

            /* positive gate: at least 3 packets AND real motion in
               BOTH axes. Anything decoded-shapeless is named FAILED. */
            if (mouse_packets() >= 3 && sdx != 0 && sdy != 0) {
                serial_puts("gate ok events=");
                putdec(mouse_packets());
                serial_puts(" dx=");
                serial_puts(sdx < 0 ? "-" : "+");
                putdec((u64)(sdx < 0 ? -sdx : sdx));
                serial_puts(" dy=");
                serial_puts(sdy < 0 ? "-" : "+");
                putdec((u64)(sdy < 0 ? -sdy : sdy));
                serial_puts(" buttons=");
                putdec((u64)mouse_buttons());
                serial_puts("\n");
            } else {
                serial_puts("gate FAILED (packets=");
                putdec(mouse_packets());
                serial_puts(" dx=");
                putdec(dx);
                serial_puts(" dy=");
                putdec(dy);
                serial_puts(")\n");
            }
            if (mouse_overflows()) {
                serial_puts("mouse: overflow packets dropped: ");
                putdec(mouse_overflows());
                serial_puts("\n");
            }
        }
    }

    /* M5.1: syscall gate self-test - the ABI observed end to end, run
       after sched_init so getpid answers truthfully (pid == tid, and
       this thread is 0). The console write lands via the gate itself,
       so the printed line IS the evidence for syscall nr 2. */
    {
        u64 pr = sys_v0(SYSCALL_PROBE, 77, 0, 0);
        serial_puts(pr == 77 ? "syscall: probe ok ret=" : "syscall: probe FAILED ret=");
        serial_puts_hex(pr);
        serial_puts("\n");

        u64 pid = sys_v0(SYSCALL_GETPID, 0, 0, 0);
        serial_puts("syscall: getpid=");
        serial_puts_hex(pid);
        serial_puts("\n");
        if (pid != 0)
            serial_puts("syscall: getpid not thread 0 - check tids\n");

        static const char sysmsg[] = "syscall: ABI v0 console write\n";
        u64 want = sizeof sysmsg - 1;
        u64 got  = sys_v0(SYSCALL_WRITE, STDOUT_FILENO, (u64)sysmsg, want);
        serial_puts(got == want ? "syscall: write ok bytes=" : "syscall: write FAILED bytes=");
        serial_puts_hex(got);
        serial_puts("\n");

        u64 badfd = sys_v0(SYSCALL_WRITE, 9, (u64)sysmsg, 4);
        serial_puts(badfd == (u64)SYS_EINVAL ? "syscall: bad fd refused\n"
                                             : "syscall: bad fd NOT refused\n");

        u64 unk = sys_v0(77, 0, 0, 0);
        serial_puts(unk == (u64)SYS_EINVAL ? "syscall: unknown nr refused\n"
                                           : "syscall: unknown nr NOT refused\n");
    }

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
        serial_puts("elf: SHELL.ELF read bytes=");
        serial_puts_hex((u64)got);
        serial_puts("\n");
        int stid = -1;
        if (got > 64)
            stid = thread_spawn_user("shell", shell_img, (u64)got);
        if (stid > 0) {
            serial_puts("gate5: shell live tid=");
            serial_puts_hex((u64)stid);
            serial_puts("\n");
            int code = sched_join(stid);        /* blocks in WQ_CHILD */
            serial_puts("gate5: shell exited code=");
            putdec((u64)(code < 0 ? 0 : code));
            serial_puts("\n");
            serial_puts("gate5: user exits total=");
            putdec(sysc_user_exits());
            serial_puts("\n");
            serial_puts("kbd: events=");
            putdec(kbd_events());
            serial_puts("\n");
        } else {
            serial_puts("gate5: shell load refused\n");
            for (;;)                            /* evidence stays flat */
                __asm__ volatile("hlt");
        }
    }

#if TRAP_SELFTEST
    vga_puts("selftest: int3\n");
    serial_puts("selftest: int3\n");
    __asm__ volatile("int3");
#endif

    while (1)
        __asm__ volatile("hlt");
}
