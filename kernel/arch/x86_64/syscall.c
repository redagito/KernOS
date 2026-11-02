#include <kernel/types.h>
#include <kernel/syscall.h>
#include "syscall.h"
#include "sched.h"
#include "serial.h"

/*
 * M5.1 - the v0 syscall surface (docs/syscall-abi.md).
 *
 * The dispatcher's contract: read the frame, write RAX, return - the
 * register-polypopping stub in isr.s is the only other hand at this
 * frame, and it owns everything else. Numbers not in the table are
 * SYS_EINVAL, silently (ABI rule).
 */

void syscall_dispatch(struct trapframe *tf)
{
    u64 r;

    switch (tf->rax) {
    case SYSCALL_PROBE:
        r = tf->rdi;                       /* arg1 straight through      */
        break;

    case SYSCALL_GETPID:
        /* v0: pid == thread id, one namespace; the split is M5.2. */
        r = sched_tid();
        break;

    case SYSCALL_WRITE: {
        u64 fd = tf->rdi;
        const u8 *buf = (const u8 *)tf->rsi;
        u64 n   = tf->rdx;

        /* v0 fd truth table: 1 exists, nothing else does. The pointer
           validation debt (CPL3 world) is documented in the ABI; v0
           callers are kernel threads. */
        if (fd != STDOUT_FILENO || buf == 0) {
            r = (u64)SYS_EINVAL;
            break;
        }
        for (u64 i = 0; i < n; ++i)
            serial_putc((char)buf[i]);
        r = n;
        break;
    }

    default:
        r = (u64)SYS_EINVAL;
        break;
    }

    tf->rax = r;
}
