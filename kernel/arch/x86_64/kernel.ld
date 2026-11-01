/*
 * KernOS x86-64 kernel link script.
 *
 * The kernel is linked (and identity-mapped) at 0x100000 — inside the
 * 1 GiB low map stage 2 built. The flat boot image contains text,
 * rodata and data; .bss (NOLOAD) is cleared by the entry code using
 * the _bss symbols provided here.
 *
 * Budget: the boot chain reserves exactly 256 KiB (512 sectors) for
 * the kernel image between 0x100000 and 0x180000; the assert makes an
 * overflow a link error, not a mysterious boot failure.
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    .text : { *(.text*) }

    .rodata : { *(.rodata*) }

    . = ALIGN(16);
    .data : { *(.data*) }

    __data_end = .;

    . = ALIGN(4096);
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON*) }
    __bss_end = .;

    /DISCARD/ : { *(.comment) *(.note*) }

    ASSERT(__data_end <= 0x100000 + 512 * 512,
           "kernel image exceeds its fixed 256 KiB boot slot (text+rodata+data)")
}
