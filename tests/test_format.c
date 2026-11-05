/*
 * test_format.c — host unit tests for the portable vsnprintf core.
 *
 * The SAME object file the userland links with (libc/src/format.c)
 * runs on the host — printf's formatting is pure text and answers for
 * itself. Every verb the libc promises gets a byte-exact check here,
 * so the boot gate can assume formatting lawfulness and spend its
 * evidence on the scheduler instead.
 *
 * Exit code 0 means the whole suite passed.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

int vsnprintf(char *out, size_t cap, const char *fmt, va_list ap);

static int t_no = 0, n_fail = 0;

static void check(int cond, const char *title)
{
    ++t_no;
    printf("%2d. %-52s %s\n", t_no, title, cond ? "PASS" : "FAIL");
    if (!cond)
        ++n_fail;
}

static char buf[256];

static int fmt(const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int n = vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return n;
}

/* same shape, caller-sized sink: the cap-0/cap-1 contract prober */
static int fmtsize(char *out, size_t cap, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int n = vsnprintf(out, cap, f, ap);
    va_end(ap);
    return n;
}

int main(void)
{
    check(fmt("") == 0 && buf[0] == '\0', "empty format yields empty string");
    check(fmt("plain") == 5 && !strcmp(buf, "plain"),
          "plain text passes through");

    check(fmt("%d", 0) == 1 && !strcmp(buf, "0"), "%d of 0");
    check(fmt("%d", 12345) == 5 && !strcmp(buf, "12345"), "%d multi-digit");
    check(fmt("%d", -7) == 2 && !strcmp(buf, "-7"), "%d negative");
    check(fmt("%d", -2147483647 - 1) == 11 &&
          !strcmp(buf, "-2147483648"), "%d of INT_MIN");

    check(fmt("%ld", 123456789L) == 9 && !strcmp(buf, "123456789"),
          "%ld longs ride the AAP64 width");
    check(fmt("%x", 0xdeadBEEF) == 8 && !strcmp(buf, "deadbeef"),
          "%x lowercase, full width");
    check(fmt("%x", 0) == 1 && !strcmp(buf, "0"), "%x of 0");
    check(fmt("0x%lx", 0xFFFFFFFFFFFFFFFFUL) == 18 &&
          !strcmp(buf, "0xffffffffffffffff"), "%lx max u64");

    check(fmt("%s", "text") == 4 && !strcmp(buf, "text"), "%s");
    check(fmt("%s", (char *)0) == 6 && !strcmp(buf, "(null)"), "%s NULL");
    check(fmt("%s%s", "a", "b") == 2 && !strcmp(buf, "ab"), "%s twice");

    check(fmt("%c%c", 'A', 'B') == 2 && !strcmp(buf, "AB"), "%c");
    check(fmt("%%") == 1 && buf[0] == '%', "%%");
    check(fmt("x%dy", 3) == 3 && !strcmp(buf, "x3y"),
          "verbs inline with text");

    /* truncation: no overflow, return = length it WOULD have written */
    {
        char tight[8];
        memset(tight, 'Z', sizeof tight);
        int n = vsnprintf(tight, sizeof tight, "abcdefgh", (char *)0);
        check(n == 8, "truncated return is the would-be length");
        check(strlen(tight) == 7 && tight[7] == '\0', "truncation leaves room for NUL");
        check(tight[0] == 'a' && tight[6] == 'g', "truncated content is the prefix");
    }
    {
        char tiny[1];
        int n = vsnprintf(tiny, 0, "anything", (char *)0);
        check(n == 8, "cap=0 writes nothing, return still honest");
    }
    {
        /* M5.11: the cap==0 contract with REAL formatting work and a
           NULL buffer - and cap=1, where only the NUL may land. Both
           ride a helper: a va_list is an array (decays to a pointer),
           so it cannot be "passed as the value 5" like an int. */
        char one[1];
        memset(one, 'Z', sizeof one);
        check(fmtsize((char *)0, 0, "%d apples", 5) == 8,
              "cap=0 NULL buffer formats and still counts");
        check(fmtsize(one, 1, "abcdef") == 6 && one[0] == '\0',
              "cap=1 parks only the NUL, return honest");
    }

    /* the exact shape the boot gate will print: two ints, closing nl */
    check(fmt("u1: pid=%d tick=%d\n", 3, 2) == 17 &&
          !strcmp(buf, "u1: pid=3 tick=2\n"), "the u1 gate line, formatted");

    printf("%d checks, %d failures\n", t_no, n_fail);
    return n_fail ? 1 : 0;
}
