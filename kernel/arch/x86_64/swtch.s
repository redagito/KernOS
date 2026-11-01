/*
 * Context switch + thread first-entry (M2.4).
 *
 * swtch(save_slot, new_sp): the xv6 pattern. Saves the caller's
 * callee-saved register set below its C call chain, stores %rsp into
 * *save_slot, loads new_sp, and "returns" into whatever that context
 * last saved. Called only from IRQ context (IF already clear), so a
 * switch can never be interrupted halfway.
 *
 * thread_entry: first thing a spawned thread "returns" to. Its
 * fabricated context (built in sched.c) puts entry in %r12 and arg in
 * %rbx; swtch's ret lands here with those live. When entry() returns
 * the thread parks with interrupts off - it must never re-enter the
 * scheduler on its own in M2.4.
 */

.code64

.globl swtch
swtch:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq  %rsp, (%rdi)             /* save our rsp as the from-context */
    movq  %rsi, %rsp               /* load the to-context (by value) */
    popq  %r15
    popq  %r14
    popq  %r13
    popq  %r12
    popq  %rbp
    popq  %rbx
    ret

.globl thread_entry
thread_entry:
    sti                            /* fresh threads start inside IRQ ctx (IF=0); */
                                   /* preempted threads re-enable via iretq */
    movq  %rbx, %rdi               /* arg */
    callq *%r12                    /* entry(arg) */
    cli
1:  hlt
    jmp 1b

    .section .note.GNU-stack,"",@progbits
