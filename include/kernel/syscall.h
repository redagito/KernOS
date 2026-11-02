#pragma once

/*
 * KernOS syscall ABI (v0) - the numbers and the calling shape.
 * docs/syscall-abi.md is the normative definition; this header is how
 * both sides of the boundary (kernel dispatch and, later, libc)
 * reference the same constants without drift.
 *
 * RAX = syscall number, args RDI RSI RDX R10 R8 R9, return in RAX
 * (>= 0 ok, < 0 negated error). Everything else preserved.
 */

#define SYSCALL_PROBE   0    /* probe(arg1) -> arg1          */
#define SYSCALL_GETPID  1    /* getpid() -> thread id        */
#define SYSCALL_WRITE   2    /* write(fd=1, buf, n) -> bytes */
#define SYSCALL_EXIT    3    /* exit(code): never returns    */

#define STDIN_FILENO  0      /* v0: input undefined (kbd arrives M5) */
#define STDOUT_FILENO 1      /* v0: COM1 console                */

/* Error codes: v0 is the cheap era - one value. The numbering stays
   open (negated, small, errno-shaped) for later detail. */
#define SYS_EINVAL (-1)
