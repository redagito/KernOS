/*
 * Exception gate stubs, vectors 0..31.
 *
 * Two shapes: the CPU pushes an error code for DF/TS/NP/SS/GP/PF/AC
 * (vectors 8,10,11,12,13,14,17), the rest get a zero placeholder so
 * every stub builds the same frame shape.
 *
 * Frame: err, vec (pet stub), then 15 GPRs (RDI first, R15 last),
 * then the hardware frame. dump_trap() never returns: it prints and
 * parks (M2.1 policy - no recovery paths yet).
 */

.code64

/* TRAP vec noerr: entry code for one vector. */
.macro TRAP vec noerr
trap_\vec:
    .if \noerr == 1
    pushq $0                      /* no hardware err: uniform frame */
    .endif
    pushq $\vec
    jmp trap_common
.endm

TRAP 0,  1  /* #DE divide error */
TRAP 1,  1  /* #DB debug */
TRAP 2,  1  /* NMI */
TRAP 3,  1  /* #BP breakpoint (int3) */
TRAP 4,  1  /* #OF overflow */
TRAP 5,  1  /* #BR bound */
TRAP 6,  1  /* #UD invalid opcode */
TRAP 7,  1  /* #NM device not available */
TRAP 8,  0  /* #DF double fault (err, IST) */
TRAP 9,  1  /* old segment overrun */
TRAP 10, 0  /* #TS invalid TSS (err) */
TRAP 11, 0  /* #NP segment not present (err) */
TRAP 12, 0  /* #SS stack fault (err) */
TRAP 13, 0  /* #GP general protection (err) */
TRAP 14, 0  /* #PF page fault (err) */
TRAP 15, 1  /* reserved */
TRAP 16, 1  /* #MF x87 */
TRAP 17, 0  /* #AC alignment (err) */
TRAP 18, 1  /* #MC machine check */
TRAP 19, 1  /* #XM SIMD */
TRAP 20, 1  /* #VE virtualization */
TRAP 21, 0  /* #CP control protection (err) */
TRAP 22, 1
TRAP 23, 1
TRAP 24, 1
TRAP 25, 1
TRAP 26, 1
TRAP 27, 1
TRAP 28, 1
TRAP 29, 1
TRAP 30, 1
TRAP 31, 1

trap_common:
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %rax
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq  %rsp, %rdi
    call  dump_trap
1:  cli
    hlt
    jmp   1b

    .section .rodata
    .globl trap_stub_table
trap_stub_table:
    .quad trap_0
    .quad trap_1
    .quad trap_2
    .quad trap_3
    .quad trap_4
    .quad trap_5
    .quad trap_6
    .quad trap_7
    .quad trap_8
    .quad trap_9
    .quad trap_10
    .quad trap_11
    .quad trap_12
    .quad trap_13
    .quad trap_14
    .quad trap_15
    .quad trap_16
    .quad trap_17
    .quad trap_18
    .quad trap_19
    .quad trap_20
    .quad trap_21
    .quad trap_22
    .quad trap_23
    .quad trap_24
    .quad trap_25
    .quad trap_26
    .quad trap_27
    .quad trap_28
    .quad trap_29
    .quad trap_30
    .quad trap_31
