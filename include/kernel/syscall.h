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

/* M5.5: the first blocking numbers. Blocking lives INSIDE the gate
   (docs/syscall-abi.md): the trapframe parks above the swtch save and
   the stub's iretq restores the caller's frame wholesale. */
#define SYSCALL_READ    4    /* read(fd=0, buf, n) -> bytes; blocks on kbd  */
#define SYSCALL_SPAWN   5    /* spawn(name) -> new pid (our child); or -1   */
#define SYSCALL_WAIT    6    /* wait() -> child exit code; blocks; -1 none  */

#define STDIN_FILENO  0      /* the keyboard ring (M5.5)         */
#define STDOUT_FILENO 1      /* v0: COM1 console                */

/* Error codes: v0 is the cheap era - one value. The numbering stays
   open (negated, small, errno-shaped) for later detail. */
#define SYS_EINVAL (-1)
