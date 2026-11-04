/*
 * The M5.7 console font: build/font.psf2 (tools/mkfont.c output,
 * hand-drawn 8x16 cells) embedded into the kernel image as its own
 * object. fbcon.c parses and validates the PSF2 header off these
 * symbols; a corrupt blob is a NAMED boot refusal, not a crash.
 *
 * Why a blob and not a FAT file: the console must print before -
 * and independent of - the AHCI/FAT/storage stack, the same way the
 * vga text driver does; the boot drive itself is not wired to the
 * kernel yet either.
 */
    .section .rodata
    .global font_psf2
    .global font_psf2_end
font_psf2:
    .incbin "build/font.psf2"
font_psf2_end:
