/*
 * KernOS kernel main. First C the boot chain reaches; the platform is
 * still tiny, so this file is the "product" for milestone M1: prove the
 * 64-bit toolchain, paging, and both consoles on real hardware.
 */

#include "arch/x86_64/vga.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/cpu.h"

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

    while (1)
        __asm__ volatile("hlt");
}
