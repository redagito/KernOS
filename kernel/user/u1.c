/*
 * u1 - the first KernOS userland program (M5.2).
 *
 * Freestanding C (no libc yet - that is M5.4), linked flat at
 * 0x400000 by user.ld, embedded into the kernel image, copied into a
 * fresh address space by proc_build_space and entered at CPL3.
 *
 * The whole surface it uses is the v0 syscall ABI: int 0x80 with
 * RAX = number (docs/syscall-abi.md). It speaks (write), identifies
 * itself (getpid), and terminates honestly (exit) - the first
 * synchronized interleave of two processes on this OS.
 */

#include <kernel/syscall.h>

static long sys(long nr, long a1, long a2, long a3)
{
    long r;
    __asm__ __volatile__("int $0x80"
                         : "=a"(r)
                         : "0"(nr), "D"(a1), "S"(a2), "d"(a3)
                         : "memory");
    return r;
}

static long puts_len(const char *s)
{
    long n = 0;
    while (s[n])
        n++;
    return sys(SYSCALL_WRITE, STDOUT_FILENO, (long)s, n);
}

void _start(void)
{
    long me = sys(SYSCALL_GETPID, 0, 0, 0);

    char hello[] = "u1: pid=0 hello from CPL3\n";
    hello[8] = (char)('0' + me);
    puts_len(hello);

    for (int i = 1; i <= 4; ++i) {
        /* busy wait ~2e7 cycles so both users visibly interleave
           under PIT preemption rather than zip through */
        for (volatile long v = 0; v < 20000000; ++v)
            ;

        char line[] = "u1: pid=0 tick=0\n";
        line[8]  = (char)('0' + me);
        line[15] = (char)('0' + i);
        puts_len(line);
    }

    sys(SYSCALL_EXIT, 0, 0, 0);
    for (;;)
        ;                                  /* not reached */
}
