/*
 * KernOS boot stage 1 — MBR.
 *
 * The BIOS loads these 512 bytes at 0000:7C00 and transfers control with
 * DL = boot drive. CS:IP is not guaranteed to be 0000:7C00, so the first
 * instruction normalizes it.
 *
 * Job: load stage 2 (LBA 1..62, see docs/disk-layout.md) to 0000:7E00
 * and far-jump to it. Loading works, in order of preference:
 *
 *   1. Direct ATA PIO read through the task-file ports (0x1F0..).
 *      Spec-driven (T13), our own code. This is the primary path on
 *      purpose: the boot chain should not hang on BIOS service quirks.
 *   2. INT 13h extended read (AH=42 + DAP), for other drive types,
 *      kept as a fallback with the DAP resident here.
 *
 * COM1 phase sensors are deliberate: a dead boot names the stage that
 * died ('1' booted, 'a' ATA path returned, 'x' BIOS fallback attempted,
 * 'j' jumping to stage 2).
 *
 * Bytes 446-509: partition entry area, left zero — KernOS owns the
 * whole boot disk, there are no partitions.
 */

.code16

    .section .text

    .global _stage1

_stage1:
    /* Normalize CS:IP to 0000:xxxx, whatever BIOS chose. */
    ljmp $0x0000, $stage1_init

stage1_init:
    cli                     /* no interrupts while we gut the segments */
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp       /* stack grows down from our own footprint */
    sti

    /* COM1 init (LCR 8N1, IER off); every phase stamps one byte. */
    movw $0x3fb, %dx        /* LCR */
    movb $0x03, %al
    outb %al, %dx
    movw $0x3f9, %dx        /* IER */
    movb $0x00, %al
    outb %al, %dx
    movw $0x3f8, %dx        /* THR */
    movb $'1', %al
    outb %al, %dx

    movb %dl, boot_drive    /* stash the BIOS boot drive; ata_load is
                               about to trash DX with port numbers */

    call load_stage2

    movb boot_drive, %dl    /* DL must be the real drive again on entry
                               to stage 2: ATA pokes destroyed it */
    movw $0x3f8, %dx        /* phase 'j': jumping to stage 2 */
    movb $'j', %al
    outb %al, %dx
    ljmp $0x0000, $0x7e00

/* ---------------------------------------------------------------
 * Load stage 2. ATA first; falls back to BIOS extended read.
 * --------------------------------------------------------------- */
load_stage2:
    call ata_load
    movw $0x3f8, %dx
    movb $'a', %al          /* phase 'a': ATA path returned */
    outb %al, %dx
    jnc load_stage2_done    /* CF=0: success */

    /* -------- BIOS fallback: INT 13h AH=42 with the resident DAP. --- */
    movb $'x', %al          /* phase 'x': BIOS extended read tried */
    outb %al, %dx
    movw $dap, %si
    movb boot_drive, %dl    /* ata_load trashed DL; restore the drive */
    xorw %cx, %cx           /* CX reserved: must be 0 */
    movw $0x4200, %ax       /* AH=42 */
    int $0x13
    jnc load_stage2_done

    /* Dead in every path: raise it on the screen too. */
    xorw %bx, %bx
    movw $0x0007, %bx
    movb $0x0e, %ah
    movb $'E', %al
    int $0x10
stage1_die:
    hlt
    jmp stage1_die

load_stage2_done:
    ret

/* ---------------------------------------------------------------
 * ata_load: LBA 1..62 straight from the task-file ports (T13).
 * 62 sectors = 32 KiB, fits entirely in 0000:7E00..0000:FA00.
 * Returns CF=0 on success, CF=1 on any BSY/DRQ/ERR anomaly.
 * --------------------------------------------------------------- */
ata_load:
    cli                         /* poll, don't swim in IRQs */
    movw $0x3f6, %dx            /* alt status / device control: nIEN=1 */
    movb $0x02, %al
    outb %al, %dx

    movw $0x1f6, %dx            /* device register: dev0, LBA28 */
    movb $0xe0, %al
    outb %al, %dx

    movw $0x1f2, %dx            /* sector count */
    movb $62, %al
    outb %al, %dx
    movw $0x1f3, %dx            /* LBA bits 7..0 */
    movb $0x01, %al
    outb %al, %dx
    movw $0x1f4, %dx            /* LBA bits 15..8 */
    movb $0x00, %al
    outb %al, %dx
    movw $0x1f5, %dx            /* LBA bits 23..16 */
    movb $0x00, %al
    outb %al, %dx

    movw $0x1f7, %dx            /* command: READ SECTORS LBA28 */
    movb $0x20, %al
    outb %al, %dx
    sti

    movw $62, %si               /* sectors remaining */
    movw $0x0000, %ax
    movw %ax, %es               /* INS transfers go to ES:DI — set up */
    movw $0x7e00, %di           /* the destination explicitly */
ata_read_sector:
    movw $0x1f7, %dx
    movw $0x7fff, %cx           /* bounded wait: ~0.5s of spinning */
ata_wait:
    inb %dx, %al
    testb $0x80, %al            /* BSY: keep waiting */
    jnz ata_wait_tick
    testb $0x01, %al            /* ERR: this drive said no */
    jnz ata_fail
    testb $0x08, %al            /* DRQ: data ready */
    jnz ata_have_data
ata_wait_tick:
    loop ata_wait
    stc                         /* timed out */
    ret

ata_have_data:
    movw $256, %cx              /* one sector = 256 words; REP INSW
                                   advances DI 512 bytes itself. */
    movw $0x1f0, %dx
    rep
    insw
    decw %si
    jnz ata_read_sector
    clc
    ret

ata_fail:
    stc
    ret

    /* Small data blob kept in .text (RAM at 0x7cxx is writable, and the
       linker script discards .data; one sector, all ours). */
boot_drive: .byte 0
dap:
    .word 16                /* DAP size */
    .word 62                /* sector count */
    .word 0x7e00            /* buffer offset */
    .word 0x0000            /* buffer segment */
    .quad 1                 /* starting LBA */
