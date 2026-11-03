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
long    spawn(const char *name); /* new child pid, or -1 on refusal */
long    wait(void);             /* child exit code; -1 if no children  */

#define STDIN_FILENO   0     /* the keyboard ring (M5.5)             */
#define STDOUT_FILENO  1     /* the console (COM1 line printer)      */
