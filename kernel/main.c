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
#include "arch/x86_64/sched.h"

static volatile u64 worker_reports;    /* lines printed across workers */

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

    /* M4.1: PCI walk — the storage estate the AHCI driver will own. */
    pci_init();

    irq_init();
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

#if TRAP_SELFTEST
    vga_puts("selftest: int3\n");
    serial_puts("selftest: int3\n");
    __asm__ volatile("int3");
#endif

    while (1)
        __asm__ volatile("hlt");
}
