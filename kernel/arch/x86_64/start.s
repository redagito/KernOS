/*
 * KernOS kernel entry — first 64-bit code that runs.
 *
 * Loaded as a flat binary at 0x100000 by stage 2 (identity-mapped).
 * Responsibilities:
 *   - put the stack somewhere sane (0x78000 already holds the stage-2
 *     stack; we move to our own .bss stack so later stages can reclaim
 *     stage-2 memory),
 *   - zero .bss (the flat binary contains no bss bytes — nobits sections
 *     are not emitted, so we must clear it ourselves),
 *   - call kernel_main() and, if it returns, die honestly.
 */

    .code64
    .section .text

    .global _start
    .extern kernel_main

_start:
    /* Zero .bss: RDI = start, RCX = size/8, RAX = 0. M5.14 (round
       four, S4): this shift drops any sub-8-BYTE tail - benign while
       the linker keeps __bss_end 8-aligned (__bss_start 0x10a000,
       __bss_end 0x1698e8 today), an uninitialized-memory bug the day
       one odd-sized bss object lands. The remainder is zeroed
       explicitly, so the guarantee no longer leans on alignment. */
    leaq    __bss_start(%rip), %rdi
    leaq    __bss_end(%rip), %rcx
    subq    %rdi, %rcx
    movq    %rcx, %r8
    shrq    $3, %rcx
    xorl    %eax, %eax
    rep stosq
    andl    $7, %r8d
    movl    %r8d, %ecx
    rep stosb

    /* Stack: our own .bss stack (linker provides stack_top). */
    leaq    stack_top(%rip), %rsp
    xorl    %ebp, %ebp

    call    kernel_main

kernel_zombie:
    cli
    hlt
    jmp kernel_zombie

    .section .bss
    .align 16
    .global stack_bottom
    .global stack_top
stack_bottom:
    .skip 32768
stack_top:
