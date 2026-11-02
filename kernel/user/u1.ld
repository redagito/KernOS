/*
 * KernOS first-user link script (M5.3): a real ELF64 executable.
 *
 * One PT_LOAD segment (RWX for now - the W^X split arrives with the
 * user demand-pager work; noted as debt in include/kernel/elf64.h's
 * neighborhood), load address 0x400000 - the floor of the user window
 * proc.c maps. The kernel reads this file from the FAT fixture
 * (BENCH_U1_FILE), validates it with elf64_plan, and builds the
 * process space from the program headers - nothing about this file is
 * hardcoded into the kernel image anymore.
 */

ENTRY(_start)

PHDRS { all PT_LOAD FLAGS(7); }

SECTIONS
{
    . = 0x400000;

    .text : { *(.text*) } :all

    .rodata : { *(.rodata*) } :all

    .data : { *(.data*) } :all

    /DISCARD/ : { *(.comment) *(.note*) *(.gnu.*) }

    ASSERT(SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) <= 32768,
           "u1 exceeds the 32 KiB user span budget")
}
