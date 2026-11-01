#pragma once

/*
 * Trap (exception) frame as arranged by the isr.s stub sequence:
 * hardware pushes rip/cs/rflags/rsp/ss, each stub pushes the vector
 * number (and a zero placeholder where the CPU pushes no error code),
 * then the common path pushes the 15 general registers starting from
 * RDI (highest) down to R15 (lowest).
 */

#include <kernel/types.h>

struct trapframe {
    u64 r15, r14, r13, r12, rbp, rbx, rax, r11, r10, r9, r8, rcx, rdx, rsi, rdi;
    u64 vector;
    u64 err;
    u64 rip, cs, rflags, rsp, ss;
};

/*
 * Exception handlers may fix a fault and let the faulting instruction
 * retry (return 1); anything that gives up must not return -
 * exc_dispatch turns it into dump_trap. Only #PF demand paging uses
 * the recover path today.
 */
typedef int (*exc_handler_t)(struct trapframe *tf);

void trap_init(void);
void exc_set_handler(int vector, exc_handler_t fn);   /* vec 0..31 */

/* Dump the frame over serial and park the CPU (M2.1 default). */
void dump_trap(struct trapframe *tf) __attribute__((noreturn));
