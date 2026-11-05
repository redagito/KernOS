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
    /* vsnprintf returns the WOULD-BE length; the gate may only be fed
       bytes that exist in buf (M5.10: a longer format used to transmit
       past-the-buffer stack bytes - clamp to the buffer, by contract).
       Valid data is 0..cap-2 (the formatter parks its NUL at cap-1), so
       the ceiling is cap-1: no NUL byte ever rides the wire. Longer
       output needs a buffer the caller owns (snprintf + write), not
       this clamp's silent cutting. */
    if (n > (int)sizeof buf - 1)
        n = (int)sizeof buf - 1;
    write(STDOUT_FILENO, buf, (size_t)n);
    /* return = the OUTPUT length actually transmitted after clamping -
       would-be formatter length UNLESS output was limited (M5.10
       clamp). Callers wanting the full length use snprintf + write. */
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
