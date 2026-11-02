#include <kernel/types.h>
#include "sched.h"
#include "proc.h"
#include "idt.h"
#include "serial.h"
#include "pmm.h"

/*
 * Round-robin over a fixed set (M2.4, extended M5.2). Thread records
 * are static .bss; kernel thread stacks too (slot 0 runs on the boot
 * stack start.s gave us). User threads (slots 3/4) get their address
 * space built at spawn time; their CR3 is loaded on switch-in, and
 * rsp0 follows so interrupts arriving from CPL3 land on the right
 * kernel stack. swtch() and thread_entry() live in swtch.s - swtch
 * must be called only with interrupts OFF (it is, from the tick).
 */

#define MAX_THREADS 6
#define KSTACK_SZ   8192

enum { T_FREE = 0, T_RUNNABLE = 1, T_ZOMBIE = 2 };

struct thread {
    int          id;
    const char  *name;
    u64         *ctx;                  /* saved rsp (swtch) / fabricated */
    int          state;
    u64          cr3;                  /* 0 = boot space (kernel thread) */
    u64          frames[MAX_PFRAMES];  /* frames of the space (exit)     */
    int          nframes;
};

static struct thread threads[MAX_THREADS];
static struct thread *cur;
static u64 switches;

static u8 kstacks[MAX_THREADS - 1][KSTACK_SZ] __attribute__((aligned(16)));

/* CR3 bookkeeping: switching costs nothing logically (kernel VAs are
   identical in every root), but we still avoid pointless flushes. */
static u64 boot_cr3, cr3_loaded;

static void load_cr3(u64 pa)
{
    if (pa == cr3_loaded)
        return;
    __asm__ volatile("mov %0, %%cr3" : : "r"(pa) : "memory");
    cr3_loaded = pa;
}

void swtch(u64 *save_slot, u64 *new_sp);        /* swtch.s  */
void thread_entry(void);                        /* swtch.s  */

void sched_init(void)
{
    boot_cr3    = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(boot_cr3));
    boot_cr3   &= ~0xFFFULL;
    cr3_loaded  = boot_cr3;

    threads[0].id     = 0;
    threads[0].name   = "main";
    threads[0].ctx    = 0;             /* first swtch-out fills it */
    threads[0].state  = T_RUNNABLE;
    threads[0].cr3    = 0;
    cur = &threads[0];
}

static int find_free_slot(void)
{
    for (int i = 1; i < MAX_THREADS; ++i)
        if (threads[i].state == T_FREE)
            return i;
    return -1;
}

static void fill_ctx(struct thread *t, u64 entry, u64 arg)
{
    /* Fabricate the context swtch will pop: memory low->high is
       [r15][r14][r13][r12][rbp][rbx][ret] ; writes go high->low
       so ret first, then rbx, rbp, r12 (thread_entry reads
       r12 = entry, rbx = arg via swtch's ret). */
    u64 *top = (u64 *)(kstacks[t->id - 1] + KSTACK_SZ);
    *--top = (u64)thread_entry;    /* ret from swtch            */
    *--top = arg;                  /* rbx: thread_entry -> rdi  */
    *--top = 0;                    /* rbp                       */
    *--top = entry;                /* r12: thread_entry callee  */
    *--top = 0;                    /* r13                       */
    *--top = 0;                    /* r14                       */
    *--top = 0;                    /* r15                       */
    t->ctx = top;
}

int thread_spawn(const char *name, thread_fn entry, void *arg)
{
    int i = find_free_slot();
    if (i < 0) {
        serial_puts("sched: spawn failed, no free slot\n");
        return -1;
    }
    struct thread *t = &threads[i];
    t->id    = i;
    t->name  = name;
    t->cr3   = 0;
    fill_ctx(t, (u64)entry, (u64)arg);
    t->state = T_RUNNABLE;          /* LAST: tick must never see a half-built slot */

    serial_puts("spawn ");
    serial_puts(name);
    serial_puts(" ctx=");
    serial_puts_hex((u64)t->ctx);
    serial_puts("\n");
    return i;
}

int thread_spawn_user(const char *name, const void *blob, u64 blob_len)
{
    int i = find_free_slot();
    if (i < 0) {
        serial_puts("sched: spawn-user failed, no free slot\n");
        return -1;
    }

    struct thread *t = &threads[i];
    t->nframes = 0;                  /* ledger starts empty            */
    u64 cr3 = proc_build_space(blob, blob_len, t->frames, &t->nframes);
    if (!cr3)
        return -1;

    t->id     = i;
    t->name   = name;
    t->cr3    = cr3;
    fill_ctx(t, (u64)proc_user_entry, 0);
    t->state  = T_RUNNABLE;          /* LAST: see above, no half-built slot */

    serial_puts("spawn-user ");
    serial_puts(name);
    serial_puts("\n");
    return i;
}

void schedule(void)
{
    if (!cur)
        return;

    /* First runnable slot after cur, strictly round-robin. */
    for (int off = 1; off <= MAX_THREADS; ++off) {
        int i = (cur->id + off) % MAX_THREADS;
        struct thread *to = &threads[i];
        if (to->state != T_RUNNABLE || i == cur->id)
            continue;

        struct thread *from = cur;
        cur = to;
        switches++;

        /* Address space follows the switch: userland gets its CR3 and
           an rsp0 that points at its own kernel stack, so both the
           syscall gate and every PIT tick from CPL3 arrive on-stack. */
        if (to->cr3) {
            tss_rsp0_set((u64)(kstacks[to->id - 1] + KSTACK_SZ));
            load_cr3(to->cr3);
        } else {
            load_cr3(boot_cr3);
        }

        if (switches <= 8) {       /* bring-up trace */
            serial_puts("sw#");
            serial_puts_hex(switches);
            serial_puts(": ");
            serial_puts(from->name);
            serial_puts(" -> ");
            serial_puts(to->name);
            serial_puts(" cr3=");
            serial_puts_hex(to->cr3 ? to->cr3 : boot_cr3);
            serial_puts("\n");
        }
        swtch(&from->ctx, to->ctx);
        return;                    /* resumed later on our own stack */
    }
    /* only cur runnable: keep its slice */
}

void sched_exit_now(void)
{
    /*
     * Called from the 0x80 gate with IF off, on the exiting thread's
     * kernel stack. Order matters:
     *   1. mark zombie, pick the successor (cur semantics removed),
     *   2. hop to the boot space - frame PAs of the dead space may sit
     *      inside the unmapped 4..6 MiB user window, and only the boot
     *      CR3 identity-maps all free RAM safely,
     *   3. free the dead space,
     *   4. switch CR3/rsp0 to the successor, then swtch into it and
     *      never come back (this stack is parked with us).
     */
    struct thread *from = cur;
    from->state = T_ZOMBIE;

    struct thread *to = 0;
    for (int off = 1; off <= MAX_THREADS; ++off) {
        int i = (from->id + off) % MAX_THREADS;
        if (threads[i].state == T_RUNNABLE) {
            to = &threads[i];
            break;
        }
    }
    if (!to) {
        /* nothing else runs: the machine is ours alone - park */
        for (;;)
            __asm__ volatile("hlt");
    }

    cur = to;
    switches++;
    serial_puts("sched: tid=");
    serial_puts_hex((u64)from->id);
    serial_puts(" zombie -> ");
    serial_puts(to->name);
    serial_puts("\n");

    load_cr3(boot_cr3);                       /* safe house */
    proc_space_destroy(from->frames, from->nframes);
    from->nframes = 0;
    from->cr3     = 0;
    from->state   = T_FREE;                   /* slot reusable        */

    if (to->cr3) {
        tss_rsp0_set((u64)(kstacks[to->id - 1] + KSTACK_SZ));
        load_cr3(to->cr3);
    }

    static u64 zombie_sp;                     /* parked forever       */
    swtch(&zombie_sp, to->ctx);
    for (;;) { }                              /* not reached          */
}

u64 sched_switches(void)
{
    return switches;
}

u64 sched_tid(void)
{
    return cur ? (u64)cur->id : (u64)-1;
}
