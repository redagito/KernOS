#include <string.h>

char *strcpy(char *dst, const char *src)
{
    char *out = dst;
    while ((*dst++ = *src++)) {}
    return out;
}

/*
 * string.c - hand-rolled word-at-a-time-able, but kept byte-loop
 * simple: 4 KiB user pages and a 100 Hz world make -O2 loops plenty,
 * and honest beats clever in a kernel you must debug over serial.
 */

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        ++n;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        ++a;
        ++b;
        --n;
    }
    if (n == 0)
        return 0;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
