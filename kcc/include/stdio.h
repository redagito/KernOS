#ifndef KCC_STDIO_H
#define KCC_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct KccFile FILE;
extern FILE *stdout, *stderr, *stdin;
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
FILE *fopen(const char*,const char*); int fclose(FILE*); size_t fread(void*,size_t,size_t,FILE*);
size_t fwrite(const void*,size_t,size_t,FILE*); int fseek(FILE*,long,int); long ftell(FILE*);
FILE *fdopen(int,const char*); int fgetc(FILE*); int fputc(int,FILE*); int getc(FILE*); int ungetc(int,FILE*); int fflush(FILE*); int feof(FILE*);
int kcc_printf(const char*,...); int kcc_fprintf(FILE*,const char*,...); int kcc_vfprintf(FILE*,const char*,va_list);
int kcc_snprintf(char*,size_t,const char*,...); int kcc_vsnprintf(char*,size_t,const char*,va_list);
int kcc_sprintf(char*,const char*,...); int kcc_sscanf(const char*,const char*,...);
int kcc_puts(const char*); int kcc_fputs(const char*,FILE*); void kcc_perror(const char*);
#define printf kcc_printf
#define fprintf kcc_fprintf
#define vfprintf kcc_vfprintf
#define snprintf kcc_snprintf
#define vsnprintf kcc_vsnprintf
#define sprintf kcc_sprintf
#define sscanf kcc_sscanf
#define puts kcc_puts
#define fputs kcc_fputs
#define perror kcc_perror
#endif
