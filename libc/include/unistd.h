#pragma once

#include <stddef.h>
#include <stdint.h>     /* ssize_t has no freestanding home; spell it */

typedef long ssize_t;

/*
 * unistd.h - the syscall ABI as C. Longs, like the ABI; the ABI doc
 * (docs/syscall-abi.md) is normative for both sides' numbering.
 * v0 numbers: probe/getpid/write/exit; M5.5 appends read/spawn/wait -
 * the first BLOCKING calls (they park the whole process in the gate
 * until the kernel's irq/scheduler layers feed them).
 */

ssize_t write(int fd, const void *buf, size_t n);
ssize_t read(int fd, void *buf, size_t n);        /* blocks on fd 0   */
long    getpid(void);       /* pid == tid (M5 ABI, split deferred)  */
long    spawn(const char *name);
/* new child pid; -1 no such program (or loader/table refusal); M5.11:
   -2 SYS_ENAME for a malformed NAME (over the 12-char 8.1 shape, stem
   over 8) - the two refusals are distinct, no hollow blame */
long    wait(void);             /* child exit code; -1 if no children  */

/* M6.1: the event channel - drain up to nn 8-byte input_event records
   (include/kernel/input.h); returns events moved (0 = none right now,
   nonblocking by contract). */
long    input(void *buf, long max);

#define STDIN_FILENO   0     /* the keyboard ring (M5.5)             */
#define STDOUT_FILENO  1     /* the console (COM1 line printer)      */
