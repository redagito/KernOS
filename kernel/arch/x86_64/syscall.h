#pragma once

#include "trap.h"
#include <kernel/syscall.h>

/*
 * The 0x80 syscall gate (M5.1): isr.s routes vector 0x80 into
 * syscall_common, which hands the uniform trapframe to
 * syscall_dispatch; the dispatcher writes the return value into the
 * frame's RAX slot, then the stub restores all registers and iretq's.
 * docs/syscall-abi.md is the normative ABI contract.
 */

void syscall_dispatch(struct trapframe *tf);

/* Count of completed user exits (M5.2 boot-gate evidence). */
u64  sysc_user_exits(void);
