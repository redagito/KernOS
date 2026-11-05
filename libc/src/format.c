#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/*
 * vsnprintf - the portable core of the printf family, host-testable.
 *
 * Supported verbs (covers libc-m5.4's actual callers, no more):
 *   %d  signed long? no - signed int (promoted), also %ld
 *   %x  unsigned 64-bit wide, lowercase hex
 *   %s  NUL-terminated string; NULL prints "(null)"
 *   %c  one char
 *   %%  a literal percent
 * Zero flags/width/precision - printf() is a printer, not a tabulator,
 * until something in the shell/GUI genuinely needs them.
 *
 * Truncation semantics = the C standard's: at most n-1 bytes plus NUL
 * when n > 0; the return value is the length it WOULD have written
 * (so callers can size buffers honestly).
 */

/* One character lands at pos unless the buffer is too small to hold
   it plus a NUL. cap==0 means "size it only": out may be NULL (C99),
   nothing is ever written - so the guard must be cap > 1, NOT the
   naive pos < cap - 1, which underflows a zero size_t. (M5.11: the
   old cap==0 early-return in vsnprintf was a LIE hiding this very
   overflow - it returned 0 instead of the would-be length, and the
   suite never caught it because gcc folded the host test's calls
   into its builtin before -fno-builtin became host discipline.) */
static int emit_char(char *out, size_t cap, size_t *pos, char c)
{
    if (cap > 1 && *pos < cap - 1)
        out[*pos] = c;
    ++*pos;
    return 0;
}

static int emit_str(char *out, size_t cap, size_t *pos, const char *s)
{
    while (*s) {
        emit_char(out, cap, pos, *s);
        ++s;
    }
    return 0;
}

/* longest u64 is 20 digits in decimal, 16 in hex */
static int emit_unsigned(char *out, size_t cap, size_t *pos,
                         uint64_t v, int base)
{
    char tmp[24];
    int n = 0;
    do {
        int d = (int)(v % (uint64_t)base);
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= (uint64_t)base;
    } while (v);

    while (n--)
        emit_char(out, cap, pos, tmp[n]);
    return 0;
}

int vsnprintf(char *out, size_t cap, const char *fmt, va_list ap)
{
    size_t pos = 0;

    /* cap==0: pure would-be length, nothing written (C99). The loop
       below is honest for it now: emit_char refrains, the NUL-park
       below is guarded. No early return 0 - that was the bug. */
    while (*fmt) {
        if (*fmt != '%') {
            emit_char(out, cap, &pos, *fmt++);
            continue;
        }
        fmt++;

        /* %ld: longs are 64-bit here (a SysV-x86-64 idiom, and the
           only width the OS ABI speaks) */
        int long_flag = 0;
        if (*fmt == 'l') {
            long_flag = 1;
            fmt++;
            if (*fmt == 'l') fmt++;
        }

        switch (*fmt) {
        case 'd': {
            long v = long_flag ? va_arg(ap, long)
                               : (long)va_arg(ap, int);
            if (v < 0) {
                emit_char(out, cap, &pos, '-');
                emit_unsigned(out, cap, &pos,
                              (uint64_t)(-v), 10);
            } else {
                emit_unsigned(out, cap, &pos, (uint64_t)v, 10);
            }
            break;
        }
        case 'x': {
            unsigned long v = long_flag ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned int);
            emit_unsigned(out, cap, &pos, v, 16);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            emit_str(out, cap, &pos, s ? s : "(null)");
            break;
        }
        case 'c': {
            int v = va_arg(ap, int);
            emit_char(out, cap, &pos, (char)v);
            break;
        }
        case '%':
            emit_char(out, cap, &pos, '%');
            break;
        default:
            /* unknown verb: print it literally as the pair it was */
            emit_char(out, cap, &pos, '%');
            if (*fmt)
                emit_char(out, cap, &pos, *fmt);
            break;
        }
        /* M5.12: step is conditional on the format still being there -
           the bare tail fmt++ stepped over the NUL when the format
           ended in '%' / '%l' (flag consumed, no verb), and the loop
           kept scanning whatever followed the string in memory. For
           every in-string case *fmt names the consumed verb here. */
        if (*fmt)
            fmt++;
    }
    if (cap)
        out[pos < cap - 1 ? pos : cap - 1] = '\0';
    return (int)pos;
}
