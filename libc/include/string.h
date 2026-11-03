#pragma once

#include <stddef.h>

/*
 * string.h - the four first primitives. All written by hand (no host
 * libc): they double as the names gcc may emit calls to even under
 * -fno-builtin, so the user link must always resolve them.
 */

void  *memcpy(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
size_t strlen(const char *s);
/* 0 = equal, nonzero = first differing comparison (negative for <) */
int    strcmp(const char *a, const char *b);