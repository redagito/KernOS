#include <kernel/types.h>
#include "sched.h"
#include "serial.h"

/*
 * Round-robin over a fixed set. No malloc yet (M3), so spawned thread
 * stacks are static .bss arrays; slot 0 is the boot stack that
 * kernel_main already runs on. swtch() and thread_entry() live in
 * swtch.s - swtch must be called only with interrupts OFF (it is, from
 * the tick handler).
 */

#define MAX_THREADS 4
#define KSTACK_SZ   8192

enum { T_FREE = 0, T_RUNNABLE = 1 };

struct thread {
    int          id;
    const char  *name;
    u64         *ctx;                  /* saved rsp (swtch) / fabricated base */
    int          state;
};

static struct thread threads[MAX_THREADS];
static struct thread *cur;
static u64 switches;

static u8 kstacks[MAX_THREADS - 1][KSTACK_SZ] __attribute__((aligned(16)));

void swtch(u64 *save_slot, u64 *new_sp);        /* swtch.s  */
void thread_entry(void);                        /* swtch.s  */

void sched_init(void)
{
    threads[0].id     = 0;
    threads[0].name   = "main";
    threads[0].ctx    = 0;             /* first swtch-out fills it */
    threads[0].state  = T_RUNNABLE;
    cur = &threads[0];
}

int thread_spawn(const char *name, thread_fn entry, void *arg)
{
    for (int i = 1; i < MAX_THREADS; ++i) {
        if (threads[i].state != T_FREE)
            continue;

        /* Fabricate the context swtch will pop: memory low->high is
           [r15][r14][r13][r12][rbp][rbx][ret] ; writes go high->low
           so ret first, then rbx, rbp, r12 (thread_entry reads
           r12 = entry, rbx = arg via swtch's ret). */
        u64 *top = (u64 *)(kstacks[i - 1] + KSTACK_SZ);
        *--top = (u64)thread_entry;    /* ret from swtch            */
        *--top = (u64)arg;             /* rbx: thread_entry -> rdi  */
        *--top = 0;                    /* rbp                       */
        *--top = (u64)entry;           /* r12: thread_entry callee  */
        *--top = 0;                    /* r13                       */
        *--top = 0;                    /* r14                       */
        *--top = 0;                    /* r15                       */

        threads[i].id    = i;
        threads[i].name  = name;
        threads[i].ctx   = top;
        threads[i].state = T_RUNNABLE;
        serial_puts("spawn ");
        serial_puts(name);
        serial_puts(" ctx=");
        serial_puts_hex((u64)top);
        serial_puts("\n");
        return i;
    }
    serial_puts("sched: spawn failed, no free slot\n");
    return -1;
}

void schedule(void)
{
    if (!cur)
        return;

    /* First runnable slot after cur, strictly round-robin. */
    for (int off = 1; off <= MAX_THREADS; ++off) {
        int i = (cur->id + off) % MAX_THREADS;
        if (threads[i].state == T_RUNNABLE && i != cur->id) {
            struct thread *from = cur, *to = &threads[i];
            cur = to;
            switches++;
            if (switches <= 8) {       /* bring-up trace */
                serial_puts("sw#");
                serial_puts_hex(switches);
                serial_puts(": ");
                serial_puts(from->name);
                serial_puts(" -> ");
                serial_puts(to->name);
                serial_puts(" toctx=");
                serial_puts_hex((u64)to->ctx);
                serial_puts("\n");
            }
            swtch(&from->ctx, to->ctx);
            return;                    /* resumed later on our own stack */
        }
    }
    /* only cur runnable: keep its slice */
}

u64 sched_switches(void)
{
    return switches;
}
