/*
 * KernOS user-program link script (M5.3, shared M5.5): a real ELF64
 * executable. Every userland binary (u1.elf, shell.elf, later KCC
 * output) links with this ONE script.
 *
 * One PT_LOAD segment (RWX for now - the W^X split arrives with the
 * user demand-pager work; noted as debt in include/kernel/elf64.h's
 * neighborhood), load address 0x400000 - the floor of the user window
 * proc.c maps. The kernel reads this file from the FAT fixture
 * (BENCH_U1_FILE / BENCH_SHELL_FILE), validates it with elf64_plan,
 * and builds the process space from the program headers - nothing
 * about these files is hardcoded into the kernel image anymore.
 * .text.crt0 always lands first: the ELF entry (_start) is the first
 * byte of the image, whichever libc archive member order ld picks.
 */

ENTRY(_start)

PHDRS { all PT_LOAD FLAGS(7); }

SECTIONS
{
    . = 0x400000;

    .text : { *(.text.crt0) *(.text*) } :all

    .rodata : { *(.rodata*) } :all

    .data : { *(.data*) } :all

    /* statics of the libc/userland: nobits inside the same LOAD
       segment -> p_memsz grows past p_filesz, and the kernel's
       loader zeroes pages before the segment bytes land, which IS
       the bss semantics (proc.c comment). */
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } :all

    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) *(.gnu.*) }

    ASSERT(SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) <= 32768,
           "program exceeds the 32 KiB user span budget")
}
