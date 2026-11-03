/*
 * KernOS boot stage 2.
 *
 * Linked at 0x7E00 as a flat binary, loaded by stage 1 (LBA 1..62).
 *
 * The kernel slot at LBA 64 is described by the versioned image header
 * (KIH v1) that mkimage plants at LBA 63: stage 2 reads it, validates
 * magic/version/checksums, then loads EXACTLY the sectors the header
 * names and verifies the payload checksum before jumping to the entry
 * point the header declares. Fixed constants below only pin the header
 * location and hard safety ceilings; the header is the boot ABI — a
 * protocol, not a layout guess (docs/disk-layout.md is normative).
 *
 * Responsibilities, in order:
 *   1. A20 (int 15h AX=2401, then port 0x92; both applied).
 *   2. Silence interrupts for good: both PICs masked, drive nIEN set.
 *      From here on the chain is self-polling and never calls BIOS again.
 *   3. Enter protected mode (our GDT, PE=1) — permanently.
 *   4. Read + validate the KIH header (LBA 63), then load the kernel
 *      image it describes at 0x100000 with direct ATA PIO reads through
 *      the task-file ports (same discipline as stage 1): chunked
 *      commands up to CHUNK_SECTORS each, REP INSW with the flat data
 *      segment addressing 0x100000+. No bounce buffer, no BIOS.
 *   5. Page tables at 0x20000: identity-map the first 1 GiB with 2 MiB
 *      pages: PML4[0] -> PDPT[0] -> PD[i] = i*2MiB | P|RW|PS.
 *   6. PAE (CR4.PAE), LME (EFER.LME, MSR 0xC0000080 bit 8), PG (CR0.PG),
 *      far jump to 64-bit code. SDM Vol.3 §9.8.5-9.8.7 sequence.
 *
 * State on entry to the kernel:
 *   - 64-bit, CPL0, identity-mapped 1 GiB, interrupts off (PICs masked)
 *   - VBE linear framebuffer mode active if the emulated card offered
 *     one (handoff block at 0x0A00, docs/disk-layout.md); else text mode
 *     (0xB8000 is inside the map either way)
 *   - DS/ES/SS = our 4 GiB flat data segment, RSP = 0x78000
 *   - boot drive preserved in stage2's (now dead) memory — the kernel
 *     will carry it properly once we hand over a real boot params struct.
 *
 * COM1 phase sensors 'U','M','V','H','L','P','K','X' name the last phase
 * reached ('V' only on a confirmed VEB LFB mode set; absence is a
 * signal too - the kernel keeps a text console, not a lie).
 */

    /* ----- Configuration (docs/disk-layout.md is normative) ----- */
    .set HEADER_LBA,     63        /* KIH v1 image header sector */
    .set HEADER_AT,      0x1000    /* low-RAM scratch the header lands in */
    .set KIH_MAGIC,      0x3148494B /* 'K','I','H','1' little-endian */
    .set KERNEL_LBA,     64        /* kernel slot first LBA (header-checked) */
    .set KERNEL_MAX_SECTORS, 512   /* kernel slot hard ceiling: 256 KiB */
    .set CHUNK_SECTORS,  128       /* max sectors per ATA command (8-bit reg) */
    .set KERNEL_ADDR,    0x100000
    .set PT_PML4,        0x20000
    .set PT_PDPT,        0x21000
    .set PT_PD,          0x22000
    .set STACK_TOP,      0x78000   /* stack top; grows down */

    /* E820 handoff (docs/disk-layout.md, boot ABI): u64 count at 0x600,
       24-byte entries (base, len, type, res) from 0x608, max 32. */
    .set E820_COUNT,     0x600
    .set E820_DATA,      0x608
    .set E820_MAX,       32

    /* VBE handoff (docs/disk-layout.md, boot ABI, M5.6): written ONLY
       on a confirmed mode set. Width==0 there means "no fb" - that is
       information, not failure; the kernel keeps a text console. */
    .set FB_INFO_AT,     0x0A00
    /* ModeInfoBlock scratch (transient, dead after the copy). */
    .set MIB_AT,         0x0C00
    /* VbeInfoBlock scratch (512 B, transient: dead past PM entry —
       so is the KIH strip at 0x1000, but VEB runs before that read). */
    .set VIB_AT,         0x0E00

    /* Bochs VBE dispi ports (QEMU VLAN documented; primary path M5.6):
       register-level mode set with NO BIOS cooperation. Widely
       emulated ('VGA'/'bochs-display'), and the emitted mode is plain
       geometry, not a vt list lookup. */
    .set DISPI_INDEX,    0x01CE
    .set DISPI_DATA,     0x01CF
    .set DISPI_ID,       0
    .set DISPI_XRES,     1
    .set DISPI_YRES,     2
    .set DISPI_BPP,      3
    .set DISPI_ENABLE,   4
    .set DISPI_VWIDTH,   7
    .set DISPI_ENABLED,  0x01
    .set DISPI_LFB,      0x40
    .set FB_W,           1024       /* deterministic budget: 1024x768x32 */
    .set FB_H,           768        /* = 3 MB ble: vmem 16 MB default ok */

    .set SEL_CODE32, 0x08
    .set SEL_DATA,   0x10
    .set SEL_CODE64, 0x20

    .code16
    .section .text

    .global stage2_entry

stage2_entry:
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movb %dl, boot_drive          /* BIOS gives DL = boot drive */

    /* COM1 init (115200 8N1) like the kernel will do, so a dead boot
       names the phase it died in. */
    movw $0x3f9, %dx
    movb $0x00, %al
    outb %al, %dx                      /* IER */
    movw $0x3fb, %dx
    movb $0x80, %al
    outb %al, %dx                      /* DLAB on */
    movw $0x3f8, %dx
    movb $0x01, %al
    outb %al, %dx                      /* divisor 1 -> 115200 baud */
    movw $0x3f9, %dx
    movb $0x00, %al
    outb %al, %dx
    movw $0x3fb, %dx
    movb $0x03, %al
    outb %al, %dx                      /* LCR: 8N1, DLAB off */
    movw $0x3fa, %dx
    movb $0xC7, %al
    outb %al, %dx                      /* FIFO: enable, clear, 14-byte thr */
    movw $0x3fc, %dx
    movb $0x0B, %al
    outb %al, %dx                      /* MCR: DTR | RTS | OUT2 */

    /* Banner 'S2' via BIOS teletype (the screen matters too). */
    xorw %bx, %bx
    movb $0x0e, %ah
    movb $'S', %al
    int $0x10
    movb $'2', %al
    int $0x10

    /* ----- A20: int 15h method, then fast-A20 port 0x92.
       Note bit 0 of 0x92 is fast reset — it MUST stay clear. ----- */
    movw $0x2401, %ax
    int $0x15
    inb $0x92, %al
    orb $0x02, %al
    andb $0xfe, %al
    outb %al, $0x92

    movw $0x3f8, %dx                   /* phase 'U': A20 applied */
    movb $'U', %al
    outb %al, %dx

    /* ----- E820 memory map capture: the ONE remaining BIOS data call.
       Lives at 0x600 (free per the low-memory table above the MBR), and
       it must happen now: this is the last interrupt-driven BIOS call
       of the whole boot - everything after masks the PICs and never
       returns to real mode. Entry layout: u64 base, u64 len, u32 type,
       u32 res (24 B stride, count in the u64 at 0x600). ----------- */
    movl $E820_COUNT, %edi
    movl $0, (%edi)                    /* count = 0 */
    movl $0, 4(%edi)
    xorl %ebx, %ebx                    /* continuation = 0 */
e820_next:
    movl E820_COUNT, %eax
    leal (%eax,%eax,2), %ecx           /* count * 3 */
    shll $3, %ecx                      /* ... * 8 = *24 bytes */
    addl $E820_DATA, %ecx
    movl %ecx, %edi                    /* ES:DI = next entry slot (ES=0) */
    movl $24, %ecx                     /* I want the 24-byte ACPI layout */
    movl $0xE820, %eax
    movl $0x534D4150, %edx             /* 'SMAP' */
    int $0x15
    jc e820_done                       /* CF=1: last entry (or BIOS said no) */
    cmpl $20, %ecx
    jb stage2_fail                     /* handler returned garbage */
    movl E820_COUNT, %eax
    incl %eax
    movl %eax, E820_COUNT
    cmpl $E820_MAX, %eax
    jae e820_done                      /* table full; tail is lossy */
    testl %ebx, %ebx                   /* continuation 0 = final entry */
    jz e820_done
    jmp e820_next

e820_done:
    cmpl $0, E820_COUNT
    jz stage2_fail
    movl $0x3F8, %edx                  /* phase 'M': memory map captured */
    movb $'M', %al
    outb %al, %dx

    /* ----- VBE: set a linear-framebuffer graphics mode (M5.6). This is
       the LAST display BIOS call, made while interrupts are still on in
       real mode. Candidates in order; the first that sets AND validates
       wins. The handoff block at FB_INFO_AT is pre-zeroed and written
       only on success, so width==0 downstream means an honest "no fb".
       A dead/absent card is a handoff of absence - boot continues in
       text mode, no halt, no guess. Phase 'V' = mode confirmed. ----- */

    /* zero the handoff block (and it stays zero on every failure path) */
    movl $FB_INFO_AT, %edi
    movl $0, (%edi)                    /* width = 0 */
    movl $0, 4(%edi)
    movl $0, 8(%edi)
    movl $0, 12(%edi)
    movl $0, 16(%edi)
    movl $0, 20(%edi)

    /* ----- PRIMARY: Bochs dispi (register-level, no BIOS). Find the
       display device via config space (class 0x03 on bus 0), latch its
       BAR0 (the std VGA LFB), program 1024x768x32 through the dispi
       ports. On any refusal fall to the enumerating BIOS path below —
       never a halt: absence stays an honest handoff. ----- */
    movw $DISPI_INDEX, %dx
    movw $DISPI_ID, %ax
    outw %ax, %dx
    movw $DISPI_DATA, %dx
    inw %dx, %ax                       /* ax = id (bochs: 0xB0C0..0xB0C5) */
    andl $0xFFF8, %eax
    cmpl $0xB0C0, %eax
    jne vbe_enum_start                 /* no dispi: BIOS fallback */

    /* locate the display device: bus 0, dev d, BAR0 = offset 0x10,
       class register = offset 0x08 (base class is bit 23..16). */
    movl $0x80000000, %ecx             /* probe addr: dev 0, fn 0 */
pci_dev_next:
    movl %ecx, %eax
    orl $0x08, %eax
    movl $0xCF8, %edx
    outl %eax, %dx
    movl $0xCFC, %edx
    inl  %dx, %eax
    shrl $24, %eax                     /* base class = byte 3 of reg 0x08 */
    cmpb $0x03, %al
    jne pci_dev_advance

    movl %ecx, %eax                    /* vendor: 0xFFFF = absent slot */
    movl $0xCF8, %edx
    outl %eax, %dx
    movl $0xCFC, %edx
    inl  %dx, %eax
    cmpl $0x0000FFFF, %eax             /* low dword of vendor+device */
    je  pci_dev_advance

    movl %ecx, %eax
    orl  $0x10, %eax                   /* BAR0 */
    movl $0xCF8, %edx
    outl %eax, %dx
    movl $0xCFC, %edx
    inl  %dx, %eax
    testl %eax, %eax
    jz   pci_dev_advance
    andl $0xFFFFFF00, %eax             /* 32-bit mem BAR: low byte flags */
    jmp  dispi_have_phys
pci_dev_advance:
    addl $0x00000800, %ecx             /* next device slot (1 << 11) */
    cmpl $0x81000000, %ecx             /* walk device 0..31... */
    jb pci_dev_next
    jmp vbe_enum_start                 /* no display device: fallback */

dispi_have_phys:
    movl %eax, vbe_phys_scratch

    /* program the mode: disable, geometry, virtual width (= pitch),
       enable with LFB. */
    movw $DISPI_INDEX, %dx
    movw $DISPI_ENABLE, %ax
    outw %ax, %dx
    movw $DISPI_DATA, %dx
    xorw %ax, %ax
    outw %ax, %dx                      /* enable = 0 */

    movw $DISPI_INDEX, %dx
    movw $DISPI_XRES, %ax
    outw %ax, %dx
    movw $DISPI_DATA, %dx
    movw $FB_W, %ax
    outw %ax, %dx

    movw $DISPI_INDEX, %dx
    movw $DISPI_YRES, %ax
    outw %ax, %dx
    movw $DISPI_DATA, %dx
    movw $FB_H, %ax
    outw %ax, %dx

    movw $DISPI_INDEX, %dx
    movw $DISPI_BPP, %ax
    outw %ax, %dx
    movw $DISPI_DATA, %dx
    movw $32, %ax
    outw %ax, %dx

    movw $DISPI_INDEX, %dx
    movw $DISPI_VWIDTH, %ax
    outw %ax, %dx
    movw $DISPI_DATA, %dx
    movw $FB_W, %ax                    /* virt width = xres: pitch = xres*4 */
    outw %ax, %dx

    movw $DISPI_INDEX, %dx
    movw $DISPI_ENABLE, %ax
    outw %ax, %dx
    movw $DISPI_DATA, %dx
    movw $(DISPI_ENABLED | DISPI_LFB), %ax
    outw %ax, %dx

    /* handoff: the geometry we just programmed + the PCI truth */
    movl $FB_W, %eax
    movl %eax, FB_INFO_AT
    movl $FB_H, %eax
    movl %eax, FB_INFO_AT+4
    /* pitch = xres * 4 (bpp 32) */
    movl $(FB_W * 4), %eax
    movl %eax, FB_INFO_AT+8
    movl $32, %eax
    movl %eax, FB_INFO_AT+12
    movl vbe_phys_scratch, %eax
    movl %eax, FB_INFO_AT+16
    movl $0, FB_INFO_AT+20

    movl $0x3F8, %edx                  /* phase 'V': LFB mode set + info */
    movb $'V', %al
    outb %al, %dx
    jmp vbe_done

vbe_enum_start:
    /* ----- FALLBACK: enumerate the card's own list (4F00),
       interrogate each entry (4F01), keep the largest 32 bpp LFB mode.
       No mode-number folklore: the card's list is the truth. Falls
       back further to the static candidate table when enumeration
       fails. ----- */
    movw $0x4F00, %ax                  /* GetVbeInfoBlock -> ES:DI */
    movw $VIB_AT, %di
    int $0x10
    cmpb $0x4F, %al
    jnz vbe_enum_fail
    cmpb $0x00, %ah
    jnz vbe_enum_fail
    cmpl $0x41534556, VIB_AT           /* 'V','E','S','A' LE */
    jnz vbe_enum_fail

    movw $0, %bx                       /* tried-modes counter */

    /* walk VideoModePtr at (seg=VIB+0x0C, off=VIB+0x0E) via %gs, keep
       ES=0 for every 4F01 call below. %bp = best area (u32), 0 = none. */
    movw VIB_AT+0x0C, %gs              /* list segment */
    movw VIB_AT+0x0E, %si              /* list offset */
    movl $0, %ebp                      /* best = none */
vbe_list_next:
    movw %gs:(%si), %dx
    cmpw $0xFFFF, %dx                  /* 0xFFFF terminator */
    jz vbe_list_done
    addw $2, %si
    incw %bx                           /* tried-modes counter (debug) */

    movw $0x4F01, %ax                  /* GetModeInfoBlock for DX */
    movw %dx, %cx
    andw $0x3FFF, %cx
    movw $MIB_AT, %di
    int $0x10
    cmpb $0x4F, %al
    jnz vbe_list_info_fail
    cmpb $0x00, %ah
    jz vbe_list_have_info
vbe_list_info_fail:
    jmp vbe_list_next
vbe_list_have_info:
    /* attr: present(bit0) | graphics(bit4). The LFB ability itself is
       NOT trusted to the attr table - some vgabios lineages omit bit7 -
       PhysBasePtr != 0 below is the load-bearing check. */
    movzbl MIB_AT+0x00, %eax
    andl $0x11, %eax
    cmpl $0x11, %eax
    jnz vbe_list_next

    movzbl MIB_AT+0x19, %eax           /* bits-per-pixel */
    cmpl $32, %eax
    jnz vbe_list_next

    movl MIB_AT+0x28, %eax             /* PhysBasePtr: must exist   */
    testl %eax, %eax
    jz vbe_list_next

    /* area = XRes * YRes (u16 * u16 -> u32); keep the biggest.
       32-bit imul on zero-extended words: the .code16 one-operand
       'imull' form would compute in DX:AX and lose the high half into
       DX (which also destroys the candidate mode register). */
    movzwl MIB_AT+0x12, %eax
    movzwl MIB_AT+0x14, %ecx
    imull %ecx, %eax
    cmpl %ebp, %eax
    jbe vbe_list_next
    movl %eax, %ebp
    movw %dx, vbe_best                 /* remember the winning mode */

vbe_list_done:
    testl %ebp, %ebp
    jz vbe_enum_fail
    /* self-inject the winner as the FIRST candidate: the stage-2 flat
       image is writable RAM, and the candidate loop below still owns
       every set/info/validate step - the scanner only changes who
       gets tried first. */
    movw vbe_best, %ax
    movw %ax, vbe_modes
    movw $0, vbe_modes+2               /* ...then end the table right there */

vbe_enum_fail:
    movw $0, %si                       /* candidate index */
vbe_next_mode:
    movw %si, %bx
    shlw $1, %bx
    movw vbe_modes(%bx), %dx           /* DX = candidate mode */
    testw %dx, %dx
    jz vbe_none                        /* table drained: no fb; block stays 0 */


    /* mode set: int 10h AX=4F02, BX = mode (bit 14 = LFB flag is IN the
       candidate table already; enum-injected modes need the OR). */
    movw $0x4F02, %ax
    movw %dx, %bx                      /* BX = mode; AL=0x4F, AH=0 on ok */
    orw $0x4000, %bx
    int $0x10
    cmpb $0x4F, %al
    jnz vbe_advance
    cmpb $0x00, %ah
    jnz vbe_advance

    /* mode info: int 10h AX=4F01, CX = base mode (bit 14 cleared) into
       the MIB scratch (128 B are mandatory; we read fixed offsets). */
    movw $0x4F01, %ax
    movw %dx, %cx
    andw $0x3FFF, %cx                  /* drop vbeModeInfo flags bits */
    movw $MIB_AT, %di
    int $0x10
    cmpb $0x4F, %al
    jnz vbe_advance
    cmpb $0x00, %ah
    jnz vbe_advance

    /* validate the mode the BIOS actually accepted: 32 bpp with a
       real linear base. Falling short = try the next candidate. */
    movzbl MIB_AT+0x19, %eax           /* ModeInfoBlock: bits-per-pixel */
    cmpl $32, %eax
    jnz vbe_advance
    movl MIB_AT+0x28, %eax             /* ModeInfoBlock: PhysBasePtr */
    testl %eax, %eax
    jz vbe_advance

vbe_accepted:

    /* handoff block, docs/disk-layout.md: u32 width, u32 height,
       u32 pitch (bytes/scanline), u32 bpp, u64 phys (LE dword + 0). */
    movzwl MIB_AT+0x12, %eax           /* XResolution */
    cmpl $1, %eax
    jb vbe_advance                     /* nonsense resolution */
    movl %eax, FB_INFO_AT
    movzwl MIB_AT+0x14, %eax           /* YResolution */
    cmpl $1, %eax
    jb vbe_advance
    movl %eax, FB_INFO_AT+4
    movzwl MIB_AT+0x10, %eax           /* BytesPerScanLine */
    cmpl $4, %eax
    jb vbe_advance                     /* can't even hold one pixel */
    movl %eax, FB_INFO_AT+8
    movl $32, %eax
    movl %eax, FB_INFO_AT+12           /* bpp */
    movl MIB_AT+0x28, %eax
    movl %eax, FB_INFO_AT+16           /* PhysBasePtr (LE dword) */
    movl $0, FB_INFO_AT+20             /* high dword: sub-4-GiB BAR */

    movl $0x3F8, %edx                  /* phase 'V': LFB mode set + info */
    movb $'V', %al
    outb %al, %dx
    jmp vbe_done

vbe_advance:
    incw %si
    jmp vbe_next_mode

vbe_done:
vbe_none:

    /* ----- No more BIOS: silence both PICs and the drive IRQ line.
       Interrupts stay off for the rest of the boot. ----- */
    cli
    movw $0x21, %dx                    /* master PIC IMR: all masked (DX was
                                          still 0x3F8 from the 'U' sensor —
                                          that write leaked 0xFF into COM1
                                          and left the PIC unmasked) */
    movb $0xFF, %al
    outb %al, %dx
    movw $0xA1, %dx                    /* slave: all masked */
    movb $0xFF, %al
    outb %al, %dx
    movw $0x3F6, %dx                   /* device control: nIEN = 1 */
    movb $0x02, %al
    outb %al, %dx

    /* ----- Enter protected mode, for the last time ----- */
    lgdt stage2_gdtdesc
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmp $SEL_CODE32, $pm_entry

    .code32
pm_entry:
    movw $SEL_DATA, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl $STACK_TOP, %esp

    /* ----- KIH v1 image header: 1 sector, LBA HEADER_LBA -> HEADER_AT.
       Flat ES:EDI again (ES base 0). The header names kernel_lba,
       kernel_sectors and kernel_entry; stage 2 trusts the checksummed
       bytes, not its own layout guesses (docs/disk-layout.md). ----- */
    movl $HEADER_LBA, %ebp
    movl $HEADER_AT, %edi
    movl $1, %esi
    call pio_read

    /* magic */
    cmpl $KIH_MAGIC, (HEADER_AT)
    jnz stage2_fail
    /* version */
    movzbl HEADER_AT+4, %eax
    cmpl $1, %eax
    jnz stage2_fail
    /* header must describe the slot this chain can actually enter:
       the entry point is the fixed flat kernel image position. */
    cmpl $KERNEL_ADDR, HEADER_AT+0x10
    jnz stage2_fail
    /* kernel_lba: beyond the header itself */
    movl HEADER_AT+8, %eax
    cmpl $HEADER_LBA+1, %eax
    jb stage2_fail
    /* kernel_sectors: at least 1, at most the 256-KiB slot ceiling */
    movl HEADER_AT+0xC, %eax
    testl %eax, %eax
    jz stage2_fail
    cmpl $KERNEL_MAX_SECTORS, %eax
    ja stage2_fail
    /* header self-checksum: additive sum of the whole sector's dwords
       (checksum slot at 0x1C makes the total wrap to 0). */
    movl $HEADER_AT, %edi
    movl $128, %ecx
    xorl %esi, %esi
kih_sum:
    addl (%edi), %esi
    addl $4, %edi
    decl %ecx
    jnz kih_sum
    testl %esi, %esi
    jnz stage2_fail

    movl $0x3F8, %edx                  /* phase 'H': header read + valid */
    movb $'H', %al
    outb %al, %dx

    /* remember the payload checksum (stack survives everything below) */
    movl HEADER_AT+0x20, %eax
    pushl %eax

    /* ----- Direct ATA PIO kernel load, per the header:
       kernel_sectors from kernel_lba to 0x100000. Chunked commands
       (the count register is 8 bits; 128 keeps the loop bookkeeping
       obvious). Flat ES:EDI addresses 0x100000 directly. ----- */
    movl HEADER_AT+8, %ebp             /* kernel_lba */
    movl $KERNEL_ADDR, %edi            /* ES:EDI, flat */
    movl HEADER_AT+0xC, %esi           /* sector count, exact */
    call pio_read

    movl $0x3F8, %edx                  /* phase 'L': kernel image in RAM */
    movb $'L', %al
    outb %al, %dx

    /* payload checksum: additive sum over the loaded image's dword
       span (sector-padded, mkimage sums the identical bytes). */
    popl %esi                          /* expected sum */
    movl $KERNEL_ADDR, %edi
    movl HEADER_AT+0xC, %ecx
    shll $7, %ecx                      /* sectors * 512 / 4 dwords */
    xorl %eax, %eax
kih_payload:
    addl (%edi), %eax
    addl $4, %edi
    decl %ecx
    jnz kih_payload
    cmpl %esi, %eax
    jnz stage2_fail

    /* ----- Page tables: identity-map 1 GiB with 2 MiB pages ----- */
    /* PML4[0] -> PDPT, PDPT[0] -> PD: Present | Writable */
    movl $PT_PDPT, %eax
    orl $3, %eax
    movl %eax, PT_PML4
    movl $PT_PD, %eax
    orl $3, %eax
    movl %eax, PT_PDPT

    /* PD[i] = i * 2 MiB | P|RW|PS (0x83). 512 entries → 1 GiB identity. */
    movl $PT_PD, %edi
    xorl %ecx, %ecx
build_pd:
    movl %ecx, %eax
    shll $21, %eax
    orl $0x83, %eax
    movl %eax, (%edi)
    xorl %eax, %eax
    movl %eax, 4(%edi)                 /* upper dword: no NX, no attr bits */
    addl $8, %edi
    incl %ecx
    cmpl $512, %ecx
    jne build_pd

    /* CR4.PAE before CR3 (PAE paging must be enabled to interpret PML4) */
    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4

    movl $PT_PML4, %eax
    movl %eax, %cr3

    movl $0x3F8, %edx                  /* phase 'P': page tables live */
    movb $'P', %al
    outb %al, %dx

    /* EFER.LME: MSR 0xC0000080, bit 8. SDM Vol.3 §9.8.5. */
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    /* CR0.PG with CR4.PAE=1 and EFER.LME=1 ⇒ long mode active. §9.8.7 */
    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    ljmp $SEL_CODE64, $long_entry

    .code64
long_entry:
    movw $SEL_DATA, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl $STACK_TOP, %esp              /* zero-extends into RSP */

    /* Hand off to the kernel at its fixed entry point (0x100000). The
       first object linked into the kernel is start.s's _start, so the
       entry is exactly that. Stage 2 never knows about kernel symbols —
       the boundary between boot chain and kernel is an ABI (an address
       and a state), not a function call. See docs/disk-layout.md.
       (In long mode CS already covers the whole line at L=1, so this is
       a near jump; immediate far jumps don't exist in 64-bit mode.) */
    /* Phase 'K' BEFORE loading RAX: `outb %al,%dx` writes RAX, and RAX
       carries the jump target — emitting 'K' after `movq $0x100000,%rax`
       silently retargeted the jump to 0x10004B (entry mid-kernel_main). */
    movl $0x3F8, %edx                  /* phase 'K': entering kernel entry */
    movb $'K', %al
    outb %al, %dx
    movq $0x100000, %rax
    jmp *%rax

long_halt:
    hlt
    jmp long_halt

stage2_fail:
    movl $0x3F8, %edx                  /* phase 'X': header/load/tables failed */
    movb $'X', %al
    outb %al, %dx
    cli
    hlt
    jmp stage2_fail

/* ---------------------------------------------------------------------
   pio_read: ATA PIO LBA28 reader, chunked to CHUNK_SECTORS max per
   command. Same T13 discipline as stage 1: bounded BSY waits, ERR is
   fatal. Any lie in the media or in the header ends in stage2_fail.
   In:  %ebp = first LBA, %edi = flat ESP dest, %esi = sector count.
   Out: %edi advanced past the load; everything else clobbered.
   Clobbers: eax ebx ecx edx esi edi.
   (Assembled .code32: this runs in protected mode; the whole long-mode
   tail above is reachable, its bytes are shared, nothing returns here
   into 64-bit state.)
   --------------------------------------------------------------------- */
    .code32
pio_read:
    pushl %ebp                         /* lived across the chunk loop */
    movl $0x1F6, %edx                  /* device 0, LBA28 */
    movb $0xE0, %al
    outb %al, %dx

chunk_loop:
    cmpl $0, %esi
    jz pio_done
    movl %esi, %eax                    /* this command: min(esi, CHUNK) */
    cmpl $CHUNK_SECTORS, %eax
    jbe 1f
    movl $CHUNK_SECTORS, %eax
1:  pushl %eax                         /* chunk count (for the epilogue)   */
    pushl %eax                         /* chunk count (the sector counter) */
    movw $0x1F2, %dx
    outb %al, %dx                      /* sector count (<= 128) */

    movl %ebp, %eax
    movw $0x1F3, %dx                   /* LBA bits 7..0 */
    outb %al, %dx
    movl %ebp, %eax
    shrl $8, %eax
    movw $0x1F4, %dx                   /* LBA bits 15..8 */
    outb %al, %dx
    movl %ebp, %eax
    shrl $16, %eax
    movw $0x1F5, %dx                   /* LBA bits 23..16 */
    outb %al, %dx
    movl %ebp, %eax
    shrl $24, %eax
    andb $0x0F, %al
    orb $0xE0, %al                     /* LBA bits 27..24 + dev 0 */
    movw $0x1F6, %dx
    outb %al, %dx

    movw $0x1F7, %dx                   /* READ SECTORS LBA28 (0x20) */
    movb $0x20, %al
    outb %al, %dx

    popl %ebx                          /* sectors left in this command */
pio_sector:
    movw $0x1F7, %dx                   /* status: wait BSY=0, ERR kill */
    movl $0xFFFF, %ecx                 /* bounded wait (~1s of spinning).
                                          FULL ECX: the 32-bit 'loop'
                                          decrements all of ECX, and the
                                          caller may leave garbage in the
                                          upper half (the dispi PCI walk
                                          leaves a slot address there). */
pio_wait_drq:
    inb %dx, %al
    testb $0x80, %al                   /* BSY */
    jnz pio_wait_tick
    testb $0x01, %al                   /* ERR: drive refuses */
    jnz stage2_fail
    testb $0x08, %al                   /* DRQ: data ready */
    jnz pio_have_drq
pio_wait_tick:
    loop pio_wait_drq
    jmp stage2_fail

pio_have_drq:
    movl $256, %ecx                    /* one sector = 256 words; REP INSW
                                          advances EDI 512 bytes itself and
                                          decrements FULL ECX in 32-bit
                                          mode — a 16-bit movw would keep
                                          caller garbage up top and ~2
                                          billion words would scribble the
                                          whole 4 GiB (stage2's own code
                                          included). 16-bit DX = data port. */
    movw $0x1F0, %dx
    rep
    insw                               /* ES:EDI, flat */
    decl %ebx
    jnz pio_sector

    popl %eax                          /* this command's sector count */
    addl %eax, %ebp
    subl %eax, %esi
    jmp chunk_loop

pio_done:
    popl %ebp
    ret

/* Data lives in .text: flat binary, RAM at 0x7e00+ is writable, and the
   boot image has no separate loading sections. */
boot_drive: .byte 0

/* VBE LFB candidates (docs/disk-layout.md, M5.6), most preferred first.
   Bit 14 = linear framebuffer. Two naming conventions exist in the
   wild for the 32 bpp modes and NEITHER is guaranteed: the Bochs/QEMU
   vgabios lineage numbers 640x480x32=0x114, 800x600x32=0x115,
   1024x768x32=0x116, 1280x1024x32=0x117, while the VESA spec's
   extension table numbers them 0x113..0x11B (cycle 15/16/24/32 per
   resolution). The MIB validation below owns the truth: whatever
   number the card accepts, we keep it ONLY if it reports 32 bpp and a
   real PhysBasePtr. A card matching neither convention leaves the
   handoff zeroed and the kernel boots on the text console. */
    .align 2
vbe_modes:
    .word 0x4116                   /* Bochs: 1024x768x32            */
    .word 0x4115                   /* Bochs: 800x600x32             */
    .word 0x4114                   /* Bochs: 640x480x32             */
    .word 0x411B                   /* VESA spec: 1024x768x32        */
    .word 0x4117                   /* VESA spec: 800x600x32         */
    .word 0x4113                   /* VESA spec: 640x480x32         */
    .word 0

vbe_best: .word 0                  /* scanner's winning plain mode  */
vbe_phys_scratch: .long 0          /* PCI BAR0 of the display device */

    .align 8
    .section .rodata.gdt, "a"
stage2_gdt:
    .quad 0                                   /* null */
    .quad 0x00CF9A000000FFFF                  /* 0x08 code32: 4GiB G=1 D=1 */
    .quad 0x00CF92000000FFFF                  /* 0x10 data:   4GiB G=1 */
    .quad 0x000F9A000000FFFF                  /* 0x18 code16: G=0 D=0 (spare) */
    .quad 0x00AF9A0000000000                  /* 0x20 code64: L=1 G=1 */
stage2_gdt_end:

    .section .rodata.gdtdesc, "a"
stage2_gdtdesc:
    .word stage2_gdt_end - stage2_gdt - 1
    .long stage2_gdt
