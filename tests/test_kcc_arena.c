#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
void *kcc_malloc(size_t);
void kcc_free(void *);
void *kcc_realloc(void *, size_t);
void *kcc_calloc(size_t, size_t);
static int checks, failed;
static void check(int good, const char *what)
{ ++checks; if (!good) ++failed; printf("%s %s\n", good ? "PASS" : "FAIL", what); }
int main(void)
{
    unsigned char *p = kcc_malloc(37);
    check(p && !((uintptr_t)p & 15), "compiler allocations meet x86-64 alignment");
    for (int i = 0; i < 37; ++i) p[i] = i;
    unsigned char *q = kcc_realloc(p, 3000);
    int good = q != 0;
    for (int i = 0; i < 37 && good; ++i) good = q[i] == i;
    check(good, "growing allocations preserve content");
    kcc_free(q);
    void *parts[400];
    for (int i = 0; i < 400; ++i) parts[i] = kcc_malloc(1000);
    for (int i = 0; i < 400; i += 2) kcc_free(parts[i]);
    for (int i = 1; i < 400; i += 2) kcc_free(parts[i]);
    p = kcc_malloc(1000000);
    check(p != 0, "fragmented freed storage coalesces for a large compile buffer");
    kcc_free(p);
    p = kcc_calloc(100, 3); good = p != 0;
    for (int i = 0; i < 300 && good; ++i) good = p[i] == 0;
    check(good, "calloc clears reused storage"); kcc_free(p);
    check(!kcc_malloc(SIZE_MAX) && !kcc_calloc(SIZE_MAX, 2), "oversized allocations refuse without arithmetic wraparound");
    printf("%d checks, %d failures\n", checks, failed);
    return failed != 0;
}
