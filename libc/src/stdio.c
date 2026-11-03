#include <kernel/syscall.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

/*
 * stdio.c - the printf front. No buffering: one write() per call.
 * The OS's only output channel is the gate, and the gate is
 * synchronous; when M6 needs a paint-capable sufface, this file is
 * the one that gains an fd-aware layer under it.
 */

int printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    write(STDOUT_FILENO, buf, (size_t)n);  /* return = formatter length */
    return n;
}

int putchar(int c)
{
    char ch = (char)c;
    if (write(STDOUT_FILENO, &ch, 1) == 1)
        return (unsigned char)ch;
    return -1;                              /* -1 on gate error */
}

int puts(const char *s)
{
    int n = 0;
    while (*s)
        n += putchar(*s++);
    n += putchar('\n');
    return n;                               /* nonneg = chars written */
}
