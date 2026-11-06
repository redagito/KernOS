#ifndef KCC_SYS_MMAN_H
#define KCC_SYS_MMAN_H
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
int mprotect(void*,unsigned long,int);
#endif
