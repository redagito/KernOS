/*
 * KernOS exception gate stubs, vectors 0..31.
 *
 * Two shapes: the CPU pushes an error code for DF/TS/NP/SS/GP/PF/AC
 * (vectors 8,10,11,12,13,14,17), the rest get a zero placeholder so
 * every stub builds the same frame shape.
 *
 * Frame: err, vec (per stub), then 15 GPRs (RDI first, R15 last),
 * then the hardware frame. Since M3.3 exceptions have a RETURNING
 * path exactly like IRQs: exc_dispatch(tf) looks up a C handler.
 *   - handler returns "fixed" (only #PF demand paging today):
 *     exc_dispatch returns and the tail restores all regs, drops the
 *     vec+err slots and iretq's - the faulting instruction retries.
 *   - no handler / handler gives up: exc_dispatch calls dump_trap
 *     (noreturn) - the M2.1 dump-and-park policy is unchanged.
 */

.code64

/* TRAP vec noerr: entry code for one vector. */
.macro TRAP vec noerr
trap_\vec:
    .if \noerr == 1
    pushq $0                      /* no hardware err: uniform frame */
    .endif
    pushq $\vec
    jmp trap_dispatch_common
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

/* Hardware IRQ stubs: PIC remap puts IRQ0..15 on vectors 32..47.
   Uniform frame shape (err placeholder), vector is inside tf so the C
   dispatcher only needs the frame pointer; these MUST return (iretq),
   unlike the exception path which parks. */

.macro IRQ i
irq_\i:
    pushq $0                       /* err slot (uniform frame) */
    pushq $(\i + 32)               /* vec slot: PIC remap base 32 */
    jmp irq_common
.endm

IRQ 0
IRQ 1
IRQ 2
IRQ 3
IRQ 4
IRQ 5
IRQ 6
IRQ 7
IRQ 8
IRQ 9
IRQ 10
IRQ 11
IRQ 12
IRQ 13
IRQ 14
IRQ 15

trap_dispatch_common:
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
    call  exc_dispatch          /* returns only for handled faults */
    popq  %r15
    popq  %r14
    popq  %r13
    popq  %r12
    popq  %rbp
    popq  %rbx
    popq  %rax
    popq  %r11
    popq  %r10
    popq  %r9
    popq  %r8
    popq  %rcx
    popq  %rdx
    popq  %rsi
    popq  %rdi
    addq  $16, %rsp                /* drop vec + err slots */
    iretq

irq_common:
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
    call  irq_dispatch
    popq  %r15
    popq  %r14
    popq  %r13
    popq  %r12
    popq  %rbp
    popq  %rbx
    popq  %rax
    popq  %r11
    popq  %r10
    popq  %r9
    popq  %r8
    popq  %rcx
    popq  %rdx
    popq  %rsi
    popq  %rdi
    addq  $16, %rsp                /* drop vec + err slots */
    iretq

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
    .quad irq_0
    .quad irq_1
    .quad irq_2
    .quad irq_3
    .quad irq_4
    .quad irq_5
    .quad irq_6
    .quad irq_7
    .quad irq_8
    .quad irq_9
    .quad irq_10
    .quad irq_11
    .quad irq_12
    .quad irq_13
    .quad irq_14
    .quad irq_15

    .section .note.GNU-stack,"",@progbits
