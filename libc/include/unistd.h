#pragma once

#include <stddef.h>
#include <stdint.h>     /* ssize_t has no freestanding home; spell it */

typedef long ssize_t;

/*
 * unistd.h - the syscall ABI as C. Longs, like the ABI; the ABI doc
 * (docs/syscall-abi.md) is normative for both sides' numbering.
 * v0 numbers: probe/getpid/write/exit; fds are small ints forever.
 */

ssize_t write(int fd, const void *buf, size_t n);
long    getpid(void);       /* pid == tid (M5 ABI, split deferred)  */

#define STDIN_FILENO   0     /* v0: input undefined (kbd later)      */
#define STDOUT_FILENO  1     /* the console (COM1 line printer)      */
