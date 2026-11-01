/*
 * KernOS boot stage 2.
 *
 * Linked at 0x7E00 as a flat binary, loaded by stage 1 (LBA 1..62).
 * Fixed disk layout, see docs/disk-layout.md; the kernel image lives at
 * LBA 64 and is exactly KERNEL_SECTORS sectors (no size passing needed
 * until the kernel outgrows 256 KiB — then we add a proper image header).
 *
 * Responsibilities, in order:
 *   1. A20 (int 15h AX=2401, then port 0x92; both applied).
 *   2. Silence interrupts for good: both PICs masked, drive nIEN set.
 *      From here on the chain is self-polling and never calls BIOS again.
 *   3. Enter protected mode (our GDT, PE=1) — permanently.
 *   4. Load the kernel image (LBA 64..) to 0x100000 with direct ATA PIO
 *      reads through the task-file ports (same discipline as stage 1):
 *      2 MiB-page commands of 128 sectors each, REP INSW with the flat
 *      data segment addressing 0x100000+. No bounce buffer, no BIOS.
 *   5. Page tables at 0x20000: identity-map the first 1 GiB with 2 MiB
 *      pages: PML4[0] -> PDPT[0] -> PD[i] = i*2MiB | P|RW|PS.
 *   6. PAE (CR4.PAE), LME (EFER.LME, MSR 0xC0000080 bit 8), PG (CR0.PG),
 *      far jump to 64-bit code. SDM Vol.3 §9.8.5-9.8.7 sequence.
 *
 * State on entry to the kernel:
 *   - 64-bit, CPL0, identity-mapped 1 GiB, interrupts off (PICs masked)
 *   - BIOS VGA text mode intact (0xB8000 is inside the map)
 *   - DS/ES/SS = our 4 GiB flat data segment, RSP = 0x78000
 *   - boot drive preserved in stage2's (now dead) memory — the kernel
 *     will carry it properly once we hand over a real boot params struct.
 *
 * COM1 phase sensors 'U','L','P','K','X' name the last phase reached.
 */

    /* ----- Configuration (docs/disk-layout.md is normative) ----- */
    .set KERNEL_LBA,     64        /* kernel image first sector */
    .set KERNEL_SECTORS, 512       /* 256 KiB fixed kernel slot */
    .set CHUNK_SECTORS,  128       /* sectors per ATA command (8-bit reg) */
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

    /* ----- Direct ATA PIO kernel load: LBA 64.. 512 sectors -----
       Flat ES:EDI addresses 0x100000 directly. 128-sector commands
       (the count register is 8 bits; 256 would mean 0 in the register,
       which is legal but 128 keeps the loop bookkeeping obvious).      */

    movl $0x1F6, %edx                  /* device 0, LBA28 */
    movb $0xE0, %al
    outb %al, %dx

    movl $KERNEL_ADDR, %edi            /* ES:EDI, flat */
    movl $KERNEL_LBA, %ebp             /* next LBA */
    movl $KERNEL_SECTORS / CHUNK_SECTORS, %esi   /* 4 commands */

load_chunk:
    movw $0x1F2, %dx                   /* sector count = 128 */
    movb $CHUNK_SECTORS, %al
    outb %al, %dx

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

    movl $CHUNK_SECTORS, %ebx          /* sectors left in this command */
sector_loop:
    movw $0x1F7, %dx                   /* status: wait BSY=0, ERR kill */
    movw $0xFFFF, %cx                  /* bounded wait (~1s of spinning) */
wait_drq:
    inb %dx, %al
    testb $0x80, %al                   /* BSY */
    jnz wait_tick
    testb $0x01, %al                   /* ERR: drive refuses */
    jnz stage2_fail
    testb $0x08, %al                   /* DRQ: data ready */
    jnz have_drq
wait_tick:
    loop wait_drq
    jmp stage2_fail

have_drq:
    movw $256, %cx                     /* one sector = 256 words; REP INSW
                                          advances EDI 512 bytes itself.
                                          16-bit DX = the data port. */
    movw $0x1F0, %dx
    rep
    insw                               /* ES:EDI, flat -> 0x100000+ */
    decl %ebx
    jnz sector_loop

    addl $CHUNK_SECTORS, %ebp
    decl %esi
    jnz load_chunk

    movl $0x3F8, %edx                  /* phase 'L': kernel image in RAM */
    movb $'L', %al
    outb %al, %dx

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
    movl $0x3F8, %edx                  /* phase 'X': load/tables failed */
    movb $'X', %al
    outb %al, %dx
    cli
    hlt
    jmp stage2_fail

/* Data lives in .text: flat binary, RAM at 0x7e00+ is writable, and the
   boot image has no separate loading sections. */
boot_drive: .byte 0

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
