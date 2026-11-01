/*
 * KernOS kernel main. First C the boot chain reaches; M2.1 adds the
 * exception safety net: after the consoles spin up we install our own
 * GDT/TSS/IDT. The int3 self-test at the end should produce a full
 * register dump over serial - dump-and-park is M2.1 policy.
 */

#include "arch/x86_64/vga.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/trap.h"

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

    /* M2.1: exception safety net. Any fault from here prints a full
       dump over serial instead of a silent triple-fault reboot. */
    trap_init();
    vga_puts("traps online\n");
    serial_puts("traps online\n");

    /* Self-test: an intentional #BP - expect the dump, then park. */
    vga_puts("selftest: int3\n");
    serial_puts("selftest: int3\n");
    __asm__ volatile("int3");

    while (1)
        __asm__ volatile("hlt");
}
