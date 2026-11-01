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
