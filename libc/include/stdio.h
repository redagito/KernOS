#pragma once

/*
 * KernOS libc - the userland half of M5.4.
 *
 * Size, not coverage, is the point: the OS has exactly ONE spec'd
 * output path (the int 0x80 gate, docs/syscall-abi.md) and the libc is
 * a thin C idiom over it - v0-era errno-free, locale-free, and
 * freestanding by design so that every later milestone (shell, KCC,
 * GUI) builds on the same shape. Format-only conversion work from the
 * printf family lives in src/format.c (portable, host-testable); the
 * printf() front is printf -> vsnprintf -> one write() syscall.
 */

#include <stddef.h>
#include <stdarg.h>     /* va_list: vsnprintf takes one, like everyone's */

int printf(const char *fmt, ...);
int putchar(int c);        /* v0 has no stdin; stdout only */
int puts(const char *s);   /* appends a newline, like everyone's */
int vsnprintf(char *out, size_t cap, const char *fmt, va_list ap);
/* C99 truncation semantics: at most cap-1 bytes plus NUL; the return
   is the length it WOULD have written so callers can size honestly.
   cap == 0: out may be NULL, nothing is written, return is still the
   would-be length. */