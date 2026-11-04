#include <kernel/types.h>
#include "sched.h"
#include "proc.h"
#include "idt.h"
#include "serial.h"
#include "console.h"
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

enum { T_FREE = 0, T_RUNNABLE = 1, T_ZOMBIE = 2, T_WAITING = 3 };

struct thread {
    int          id;
    const char  *name;
    u64         *ctx;                  /* saved rsp (swtch) / fabricated */
    int          state;
    u64          cr3;                  /* 0 = boot space (kernel thread) */
    u64          frames[MAX_PFRAMES];  /* frames of the space (exit)     */
    int          nframes;
    int          ppid;                 /* spawner's tid (user procs; 0)  */
    int          waitq;                /* queue if T_WAITING             */
};

static struct thread threads[MAX_THREADS];
static struct thread *cur;
static u64 switches;

/* Child-exit ledger (M5.5): indexed by tid - the permanent small-int
   namespace. A slot can be FREE again while its exit result is still
   uncollected: the exit tables outlive the thread record. */
static u8  exited[MAX_THREADS];
static u64 exit_codes[MAX_THREADS];

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

void swtch(u64 **save_slot, u64 *new_sp);       /* swtch.s */
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
        console_puts("sched: spawn failed, no free slot\n");
        return -1;
    }
    struct thread *t = &threads[i];
    t->id    = i;
    t->name  = name;
    t->cr3   = 0;
    t->ppid  = 0;                    /* kernel thread: not a child proc */
    t->waitq = 0;
    fill_ctx(t, (u64)entry, (u64)arg);
    t->state = T_RUNNABLE;          /* LAST: tick must never see a half-built slot */

    console_puts("spawn ");
    console_puts(name);
    console_puts(" ctx=");
    console_puts_hex((u64)t->ctx);
    console_puts("\n");
    return i;
}

int thread_spawn_user(const char *name, const void *elf, u64 elf_len)
{
    int i = find_free_slot();
    if (i < 0) {
        console_puts("sched: spawn-user failed, no free slot\n");
        return -1;
    }

    struct thread *t = &threads[i];
    t->nframes = 0;                  /* ledger starts empty            */
    u64 entry = 0;
    u64 cr3 = proc_build_space_elf(elf, elf_len, t->frames,
                                   &t->nframes, &entry);
    if (!cr3)
        return -1;

    t->id   = i;
    t->name = name;
    t->cr3  = cr3;
    t->ppid = cur ? (int)cur->id : 0;    /* spawner link (WAIT collects) */
    t->waitq = 0;
    fill_ctx(t, (u64)proc_user_entry, entry);
    t->state = T_RUNNABLE;           /* LAST: see above, no half-built slot */

    console_puts("spawn-user ");
    console_puts(name);
    console_puts("\n");
    return i;
}

/* The one true switch-in: cur/goal bookkeeping, address space + rsp0,
   (bounded) bring-up trace. Caller then swtch()s into it. Runs IF off
   (tick, gate, exit path all guarantee it). */
static void switch_into(struct thread *from, struct thread *to)
{
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

    if (switches <= 8) {           /* bring-up trace */
        console_puts("sw#");
        console_puts_hex(switches);
        console_puts(": ");
        console_puts(from->name);
        console_puts(" -> ");
        console_puts(to->name);
        console_puts(" cr3=");
        console_puts_hex(to->cr3 ? to->cr3 : boot_cr3);
        console_puts("\n");
    }
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
        switch_into(from, to);
        swtch(&from->ctx, to->ctx);
        return;                    /* resumed later on our own stack */
    }
    /* only cur runnable: keep its slice */
}

void sched_exit_now(u64 exit_code)
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

    /* Remember the result for whoever waits on this tid (the tables
       outlive the slot), and let a parent blocked in WAIT/join see it. */
    exited[from->id]     = 1;
    exit_codes[from->id] = exit_code;
    sched_wake_all(SCHED_WQ_CHILD);

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
    console_puts("sched: tid=");
    console_puts_hex((u64)from->id);
    console_puts(" zombie -> ");
    console_puts(to->name);
    console_puts("\n");

    load_cr3(boot_cr3);                       /* safe house */
    proc_space_destroy(from->frames, from->nframes);
    from->nframes = 0;
    from->cr3     = 0;
    from->state   = T_FREE;                   /* slot reusable        */

    if (to->cr3) {
        tss_rsp0_set((u64)(kstacks[to->id - 1] + KSTACK_SZ));
        load_cr3(to->cr3);
    }

    static u64 *zombie_sp;                    /* parked forever: the
                                                 save slot type matches
                                                 every thread's ctx slot */
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

/* ---- blocking (M5.5): the substrate pays off ----------------------- */

void sched_wake_all(int wq)
{
    /* IRQ-safe: no switching, no locks - single core, IF off on every
       path that mutates thread state. Woken threads re-check their
       own condition; a stale wake just re-blocks on the next pass. */
    for (int i = 0; i < MAX_THREADS; ++i)
        if (threads[i].state == T_WAITING && threads[i].waitq == wq)
            threads[i].state = T_RUNNABLE;
}

void sched_block(int wq)
{
    /*
     * The parked-thread contract (M5.10 rewrite), record vs CPU:
     *
     *   - state T_WAITING means "not on any CPU, parked on waitq";
     *     while this thread is ON a CPU it is cur and (in a world it
     *     can see) the record reads RUNNABLE. A record that lies is
     *     the bug this rewrite removes.
     *   - wake is a state flip only (wake_all never switches). The
     *     woken thread re-runs this loop: peers first, own wake as
     *     the in-place resume, everyone else keeps parking.
     *   - the park loop is REAL: one cli;hlt;cli per iteration, and
     *     every un-hlt rescans both exits. (The previous version's
     *     loop body ended in an unconditional break - one hlt, then
     *     the empty dispatch path fell through and RETURNED IN PLACE
     *     with the record still T_WAITING: a thread running while
     *     tagged parked, waitq still armed, wake edges wasted.)
     */
    struct thread *me = cur;
    me->state = T_WAITING;
    me->waitq = wq;

    for (;;) {
        /* peer dispatch first: hand the CPU on, keep waiting */
        for (int off = 1; off <= MAX_THREADS; ++off) {
            int i = (me->id + off) % MAX_THREADS;
            struct thread *to = &threads[i];
            if (to->state != T_RUNNABLE || i == me->id)
                continue;

            switch_into(me, to);
            swtch(&me->ctx, to->ctx);
            /* Resumed: woken and scheduled again. The wake made the
               record RUNNABLE before we ran; our waitq is spent. */
            me->waitq = 0;
            /* IF is still OFF by the block contract - a gate caller
               continues its dispatch (the stub's iretq restores IF),
               a kernel caller sti's itself. */
            return;
        }

        /* no peer: did OUR own wake already land? (A wake that flips
           only `me` exits here - the park has no one to hand the CPU
           to, and resuming in place is the only truthful move: the
           record is already RUNNABLE, so heal the queue claim and
           go.) Resumed IF off, exactly like the switch path. */
        if (me->state == T_RUNNABLE) {
            me->waitq = 0;             /* no longer parked anywhere */
            return;
        }

        /* still genuinely waiting on IO: park with IF open until an
           IRQ (a wake or a tick) un-hlts us - then loop and rescan
           both exits. A tick that woke nobody parks again. */
        __asm__ volatile("sti\n\thlt\n\tcli" ::: "memory");
    }
}

int sched_join(int tid)
{
    if (tid < 0 || tid >= MAX_THREADS)
        return -1;

    __asm__ volatile("cli");
    while (!exited[tid]) {
        sched_block(SCHED_WQ_CHILD);
        /* wake_all(WQ_CHILD) may fire spuriously (another thread's
           child exited); the while re-checks our own tid. */
    }
    __asm__ volatile("sti");       /* kernel-side callers resume IF off */
    return (int)exit_codes[tid];
}

int sched_collect_child(u64 *code)
{
    int me = cur ? (int)cur->id : -1;
    if (me < 0)
        return 0;
    for (int i = 0; i < MAX_THREADS; ++i) {
        if (exited[i] && threads[i].ppid == me) {
            exited[i]      = 0;    /* one collection, like wait(2) */
            if (code)
                *code = exit_codes[i];
            return 1;
        }
    }
    return 0;
}

int sched_any_children(void)
{
    int me = cur ? (int)cur->id : -1;
    if (me < 0)
        return 0;
    for (int i = 0; i < MAX_THREADS; ++i) {
        /* live child, or a FREE slot whose result is uncollected: the
           exit tables keep both visible after the record is reused. */
        if (threads[i].ppid == me && (threads[i].state != T_FREE || exited[i]))
            return 1;
    }
    return 0;
}
