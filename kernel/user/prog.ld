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

    /* M5.11 (round-two review, M4): .bss belongs to the budget. It is
       NOLOAD inside the ONE PT_LOAD - its SIZEOF grows p_memsz past
       p_filesz - and the runtime loader caps the span at 8 pages
       (proc.h USER_MAX_PAGES). The old ASSERT omitted .bss: a
       program whose static storage alone overshot the budget linked
       clean and was refused only at RUN time (proc: elf refused) -
       a link-time contract enforced by the loader. Red evidence:
       scratch program with 34000 B .bss linked span 0x8510 > 0x8000. */
    ASSERT(SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) + SIZEOF(.bss)
               <= 32768,
           "program exceeds the 32 KiB user span budget")
}
