/*
 * KernOS first-user link script (M5.2).
 *
 * The user binary is flat-mapped at 0x400000: entry VA == link VA, the
 * address space builder copies text+rodata+data into fresh identity
 * frames and maps them P|U|W at exactly these addresses (proc.h).
 * NOLOAD sections are forbidden: the blob contains only bytes, and
 * bss semantics must never be pretended into the image.
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : { *(.text*) }

    .rodata : { *(.rodata*) }

    .data : { *(.data*) }

    /DISCARD/ : { *(.comment) *(.note*) }

    ASSERT(SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) <= 8192,
           "u1 blob exceeds the 2-page (8 KiB) user code budget")
}
