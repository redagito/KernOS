#ifndef KCC_SETJMP_H
#define KCC_SETJMP_H
typedef long jmp_buf[8];
int kcc_setjmp(jmp_buf) __attribute__((returns_twice));
void kcc_longjmp(jmp_buf,int) __attribute__((noreturn));
#define setjmp(env) kcc_setjmp(env)
#define longjmp(env,val) kcc_longjmp(env,val)
#endif
