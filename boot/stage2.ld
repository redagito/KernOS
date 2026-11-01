/*
 * Stage 2 link script.
 * Physical load address 0x7E00 (loaded by stage 1 from LBA 1..62).
 * Flat binary must not exceed 62 sectors = 31,744 bytes; the linker
 * asserts that so an overgrown stage 2 can never silently truncate.
 */

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)

SECTIONS
{
    . = 0x7e00;

    .text      : { *(.text*) }
    .rodata    : { *(.rodata*) }
    .data      : { *(.data*) }

    . = ALIGN(16);

    /DISCARD/ : { *(.comment) }

    _stage2_image_end = .;

    ASSERT(. < 0x7e00 + 62 * 512, "stage 2 image overflows its 62-sector disk slot")
}
