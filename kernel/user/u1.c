/*
 * u1 - the first KernOS userland program, now written like one (M5.4).
 *
 * Freestanding C living on the KernOS libc (printf/puts/getpid over
 * the int 0x80 gate, exit via crt0's _exit). Its job in the boot gate
 * is unchanged: prove that two processes can speak C, interleave
 * under PIT preemption, and die honestly - now with the formatting
 * work no longer hand-baked into each line.
 */

#include <stdio.h>
#include <unistd.h>

static void chatter(int me, int n)
{
    /* busy wait ~2e7 cycles so both users visibly interleave under
       PIT preemption rather than zip through */
    for (volatile long v = 0; v < 20000000; ++v)
        ;
    printf("u1: pid=%d tick=%d\n", me, n);
}

int main(void)
{
    int me = getpid();
    printf("u1: pid=%d hello from CPL3\n", me);

    for (int i = 1; i <= 4; ++i)
        chatter(me, i);

    return 0;                       /* crt0 turns this into exit(0) */
}
