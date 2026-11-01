/*
 * KernOS kernel main. First C++ the boot chain reaches; the platform is
 * still tiny, so this file is the "product" for milestone M1: prove the
 * 64-bit toolchain, paging, and both consoles on real hardware.
 */

#include "arch/x86_64/vga.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/cpu.h"

extern "C" void kernel_main() {
    using namespace kernos;

    vga::clear();
    serial::init();

    vga::puts("KernOS x86-64 kernel online\n");
    serial::puts("KernOS x86-64 kernel online\n");

    char vendor[13];
    cpu::vendor(vendor);
    vga::puts("CPU vendor: ");
    vga::puts(vendor);
    vga::puts("\n");
    serial::puts("CPU vendor: ");
    serial::puts(vendor);
    serial::puts("\n");

    while (true)
        asm volatile("hlt");
}
