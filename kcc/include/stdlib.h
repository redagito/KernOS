#ifndef KCC_STDLIB_H
#define KCC_STDLIB_H
#include <stddef.h>
void *malloc(size_t); void *calloc(size_t,size_t); void *realloc(void*,size_t); void free(void*);
void exit(int); void _exit(int); void abort(void);
long strtol(const char*,char**,int); unsigned long strtoul(const char*,char**,int);
long long strtoll(const char*,char**,int); unsigned long long strtoull(const char*,char**,int);
double strtod(const char*,char**); float strtof(const char*,char**); long double strtold(const char*,char**);
char *getenv(const char*); int abs(int); long labs(long);
int atoi(const char*); void qsort(void*,size_t,size_t,int(*)(const void*,const void*));
#endif
