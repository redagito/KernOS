/* SysV x86-64 non-local return for TinyCC's diagnostic recovery path. */
.text
.globl kcc_setjmp
.type kcc_setjmp,@function
kcc_setjmp:
    movq %rbx, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %r12,16(%rdi)
    movq %r13,24(%rdi)
    movq %r14,32(%rdi)
    movq %r15,40(%rdi)
    leaq 8(%rsp),%rax
    movq %rax,48(%rdi)
    movq (%rsp),%rax
    movq %rax,56(%rdi)
    xorl %eax,%eax
    ret
.globl kcc_longjmp
.type kcc_longjmp,@function
kcc_longjmp:
    movq %rdi,%rdx
    movl %esi,%eax
    testl %eax,%eax
    jne 1f
    movl $1,%eax
1:  movq 0(%rdx),%rbx
    movq 8(%rdx),%rbp
    movq 16(%rdx),%r12
    movq 24(%rdx),%r13
    movq 32(%rdx),%r14
    movq 40(%rdx),%r15
    movq 48(%rdx),%rsp
    jmp *56(%rdx)
.section .note.GNU-stack,"",@progbits
