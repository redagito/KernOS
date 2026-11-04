/*
 * u1 - the first KernOS userland program, now written like one (M5.4).
 *
 * Freestanding C living on the KernOS libc (printf/puts/getpid over
 * the int 0x80 gate, exit via crt0's _exit). Its job in the boot gate
 * is unchanged: prove that two processes can speak C, interleave
 * under PIT preemption, and die honestly - now with the formatting
 * work no longer hand-baked into each line.
 *
 * M6.1 adds an observatory phase: after the chatter, u1 drains the
 * per-event input ring through syscall nr 7 and classifies what it
 * saw - the typed run `run u1` becomes the input gate:
 *   positive: rel motion (QMP-injected) staged in the ring drains on
 *   the first polls -> "input: gate ok rx=.. ry=.. keys=.." exit 0;
 *   negative: no injection -> "input: idle - no rel motion after N
 *   polls (negative control ok)" exit 0. Key events are counted, not
 *   gating criteria: the driver's own `run u1` typing legitimately
 *   rides the ring.
 */

#include <stdio.h>
#include <unistd.h>
#include <kernel/input.h>

#define IN_POLL_CAP   300000L  /* polls before declaring idle; the
                                  positive exits on the first batch   */
#define IN_PASS_RX    10L      /* rel-x deltas needed to confirm     */

static int input_phase(void)
{
    struct input_event ev[8];
    long rx = 0, ry = 0;
    long keys = 0, btn = 0;

    for (long poll = 0; poll < IN_POLL_CAP; ++poll) {
        long n = input(ev, 8);
        for (long i = 0; i < n; ++i) {
            switch (ev[i].type) {
            case IN_EVT_KEY:  keys++; break;
            case IN_EVT_RELX: rx += ev[i].value; break;
            case IN_EVT_RELY: ry += ev[i].value; break;
            case IN_EVT_BTN:  btn++; break;
            default: break;    /* unknown type: counted nowhere - the
                                     ABI would never have sent it */
            }
        }
        if (rx >= IN_PASS_RX && ry != 0) {
            printf("input: gate ok rx=%ld ry=%ld keys=%ld btn=%ld poll=%ld\n",
                   rx, ry, keys, btn, poll + 1);
            return 0;
        }
    }
    printf("input: idle - no rel motion after %ld polls (negative control ok)\n",
           IN_POLL_CAP);
    return 0;
}

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

    return input_phase();            /* M6.1: classify, then exit */
}
