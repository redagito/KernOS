/*
 * KernOS kernel main. First C the boot chain reaches.
 * M2.2: interrupts come up (PIC remapped, PIT live) and the IRQ0 tick
 * is measured against wall clock. TRAP_SELFTEST=1 adds an int3 gate.
 */

#include "arch/x86_64/vga.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/trap.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/irq.h"
#include "arch/x86_64/pit.h"

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

    /* M2.1: fault safety net (dump-and-park paths). */
    trap_init();
    vga_puts("traps online\n");
    serial_puts("traps online\n");

    /* M2.2: we own the PIC now; PIT is the first device on it. */
    irq_init();
    pit_init();

    __asm__ volatile("sti");
    vga_puts("irqs on\n");
    serial_puts("irqs on: waiting 300 ticks (3s)\n");

    while (pit_ticks() < 300)
        __asm__ volatile("hlt");

    serial_puts("done: ticks=");
    serial_puts_hex(pit_ticks());
    serial_puts("\n");

#if TRAP_SELFTEST
    vga_puts("selftest: int3\n");
    serial_puts("selftest: int3\n");
    __asm__ volatile("int3");
#endif

    while (1)
        __asm__ volatile("hlt");
}
