#ifndef KCC_UNISTD_H
#define KCC_UNISTD_H
#include <stddef.h>
typedef long ssize_t;
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x40
#define O_TRUNC 0x200
#define O_BINARY 0
int kcc_open(const char*,int,...); int kcc_close(int); ssize_t kcc_read(int,void*,size_t); ssize_t kcc_write(int,const void*,size_t);
long kcc_lseek(int,long,int); int kcc_unlink(const char*); int kcc_access(const char*,int);
char *getcwd(char*,size_t);
ssize_t file_load(const char*,void*,size_t); ssize_t file_save(const char*,const void*,size_t);
long process_args(char*,size_t);
#define open kcc_open
#define close kcc_close
#define read kcc_read
#define write kcc_write
#define lseek kcc_lseek
#define unlink kcc_unlink
#define access kcc_access
#define getcwd kcc_getcwd
char *kcc_getcwd(char*,size_t);
#endif
