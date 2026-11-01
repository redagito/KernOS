/*
 * Stage 1 (MBR) link script.
 * Physical load address 0x7C00, exactly 512 bytes: code ≤ 446, then
 * partition-table placeholders (ours = zeros, we own the disk) and the
 * 0xAA55 signature at offset 510.
 */

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)

SECTIONS
{
    . = 0x7c00;

    .text : { *(.text*) }

    . = 0x7c00 + 510;
    .boot_sig : { SHORT(0xAA55) }

    /DISCARD/ : { *(.data*) *(.rodata*) *(.comment) }

    ASSERT(SIZEOF(.text) <= 446, "stage 1 code overflows the MBR (446 byte budget)")
}
