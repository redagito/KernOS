#include <kernel/syscall.h>

#include <stdlib.h>

/*
 * stdlib.c - exit is the first call that terminates a whole process
 * and it has been load-bearing since M5.2; the libc names it exit()
 * for userland, spelled through the gate with zero fanfare.
 */

void _exit(int code) __attribute__((noreturn));

void _exit(int code)
{
    register long ret __asm__("rax") = SYSCALL_EXIT; /* the number  */
    (void)ret;
    __asm__ __volatile__("int $0x80"
                         : "+a"(ret)                 /* in/out: RAXnumber */
                         : "D"((long)code)           /* arg1: exit code  */
                         : "memory");
    for (;;)
        ;                       /* the gate switches away from us      */
}

void exit(int code) __attribute__((noreturn, alias("_exit")));
void abort(void) { _exit(127); }
