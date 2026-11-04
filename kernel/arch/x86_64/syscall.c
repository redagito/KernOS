#include <kernel/types.h>
#include <kernel/syscall.h>
#include "syscall.h"
#include "sched.h"
#include "kbd.h"
#include "fat32.h"
#include "serial.h"
#include "console.h"

/*
 * The 0x80 syscall gate (M5.1+, docs/syscall-abi.md).
 *
 * The dispatcher's contract: read the frame, write RAX, return - the
 * register-popping stub in isr.s is the only other hand at this frame,
 * and it owns everything else. Numbers not in the table are SYS_EINVAL,
 * silently (ABI rule).
 *
 * M5.2 additions: exit (never returns - sched_exit_now parks the
 * thread forever), and pointer validation for CPL3 callers: every page
 * of a user pointer must be P|U in the CALLER's CR3 (the dispatcher
 * runs in the caller's space, so walking live CR3 is the honest check).
 * CPL0 kernel-thread callers (v0 self-test) skip the walk.
 *
 * M5.5 additions: the first BLOCKING syscalls (read/spawn/wait, ABI
 * numbers appended 4/5/6). Blocking runs INSIDE the gate: sched_block
 * parks this thread with its full trapframe above the swtch save area;
 * wakeup resumes the dispatch loop which writes RAX and lets the stub
 * iretq (which restores the caller's own IF). This is the full-frame
 * resumability invariant of M5.1, finally load-bearing.
 */

static u64 cr3_read(void)
{
    u64 v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

/* One 4-KiB page accessible from CPL3? Every level must carry P|U. */
static int page_u_ok(u64 va)
{
    u64 *pml4 = (u64 *)(cr3_read() & ~0xFFFULL);
    u64 i4 = (va >> 39) & 0x1FF;
    u64 i3 = (va >> 30) & 0x1FF;
    u64 i2 = (va >> 21) & 0x1FF;
    u64 i1 = (va >> 12) & 0x1FF;

    u64 e = pml4[i4];
    if (!(e & 1) || !(e & 4)) return 0;
    u64 *pdpt = (u64 *)(e & ~0xFFFULL);

    e = pdpt[i3];
    if (!(e & 1) || !(e & 4)) return 0;
    u64 *pd = (u64 *)(e & ~0xFFFULL);

    e = pd[i2];
    if (!(e & 1) || !(e & 4)) return 0;
    if (e & 0x80) return (e & 7) == 7;    /* 2 MiB leaf: same rules     */
    u64 *pt = (u64 *)(e & ~0xFFFULL);

    e = pt[i1];
    return (e & 7) == 7;                  /* P|W|U                      */
}

/* Full [va, va+n) N-page check; also refuses rollover. */
/* Full [va, va+n) N-page check; also refuses rollover. */
static int user_ptr_ok(u64 va, u64 n)
{
    if (n == 0)
        return 1;
    if (va > (u64)-1 - n)
        return 0;
    u64 first = va & ~0xFFFULL;
    u64 covered = (va & 0xFFF) + n;       /* bytes from first page start */
    for (u64 a = first; ; a += 0x1000) {
        if (!page_u_ok(a))
            return 0;
        if (0x1000 >= covered)
            return 1;
        covered -= 0x1000;
    }
}

static u64 user_exits;                     /* gate evidence for main.c */

u64 sysc_user_exits(void)
{
    return user_exits;
}

/* Name intern for spawn evidence: thread->name must outlive the
   dispatch frame; the v0 gate serializes spawns, so one hold fits. */
static char prog_name_store[16];
static const char *spawn_prog_name(const char *n)
{
    u64 i = 0;
    while (n[i] && i < sizeof prog_name_store - 1) {
        prog_name_store[i] = n[i];
        ++i;
    }
    prog_name_store[i] = '\0';
    return prog_name_store;
}

void syscall_dispatch(struct trapframe *tf)
{
    u64 r;

    switch (tf->rax) {
    case SYSCALL_PROBE:
        r = tf->rdi;                       /* arg1 straight through      */
        break;

    case SYSCALL_GETPID:
        /* pid == thread id, one namespace (M5.2 keeps tids real). */
        r = sched_tid();
        break;

    case SYSCALL_WRITE: {
        u64 fd = tf->rdi;
        const u8 *buf = (const u8 *)tf->rsi;
        u64 n   = tf->rdx;

        /* fd namespace truth: 1 exists, nothing else does. */
        if (fd != STDOUT_FILENO || buf == 0) {
            r = (u64)SYS_EINVAL;
            break;
        }
        /* CPL3 callers must own the pages they name (P|U). CPL0 v0
           callers skip - the kernel is its own trusted address space. */
        if ((tf->cs & 3) == 3 && !user_ptr_ok(tf->rsi, n)) {
            r = (u64)SYS_EINVAL;
            break;
        }
        for (u64 i = 0; i < n; ++i)
            console_putc((char)buf[i]);
        r = n;
        break;
    }

    case SYSCALL_READ: {
        /* fd 0 = the keyboard ring (the first real input channel).
           Blocking by design: empty ring parks the caller on WQ_KBD
           until the IRQ handler's wake; partial counts are honest
           read(2) semantics (the shell loops). */
        u64 fd = tf->rdi;
        u8  *buf = (u8 *)tf->rsi;
        u64  n   = tf->rdx;

        if (fd != STDIN_FILENO || buf == 0 || n == 0) {
            r = (u64)SYS_EINVAL;
            break;
        }
        if ((tf->cs & 3) == 3 && !user_ptr_ok(tf->rsi, n)) {
            r = (u64)SYS_EINVAL;          /* buf must be P|U|W           */
            break;
        }
        for (;;) {
            char chunk[64];
            int got = kbd_pull(chunk, n > 64 ? 64 : (int)n);
            if (got == 0) {
                sched_block(SCHED_WQ_KBD);   /* resumes IF off mid-loop */
                continue;
            }
            for (int i = 0; i < got; ++i)
                buf[i] = (u8)chunk[i];
            r = (u64)got;
            break;                        /* one batch per read; loop in userland */
        }
        break;
    }

    case SYSCALL_SPAWN: {
        /* arg1 = user pointer to a NUL-terminated program name.
           Resolves through the FAT fixture to an ELF, which the
           ordinary loader plans and the scheduler slots as OUR child.
           Single static staging buffer: the gate serializes spawns
           (one dispatcher, IF off) - documented v0 constraint. */
        const u8 *p = (const u8 *)tf->rdi;
        char name[12];
        u64 nlen = 0;

        if (p == 0) {
            r = (u64)SYS_EINVAL;
            break;
        }
        /* copy the name through validated user memory, byte at a time
           (each byte's page re-checked; a shorter buffer cannot fault
           what we never touch) */
        while (nlen < sizeof name - 5) {
            if ((tf->cs & 3) == 3 && !user_ptr_ok((u64)(p + nlen), 1)) {
                p = 0;
                break;
            }
            char c = (char)*p++;
            if (c == '\0')
                break;
            name[nlen++] = c;
        }
        if (p == 0 || nlen == 0) {
            r = (u64)SYS_EINVAL;
            break;
        }
        /* programs live as 8.3 names with the .ELF suffix; the user
           types the stem ("run u1") and the gate completes it */
        if (nlen < 4 ||
            (name[nlen-4] | 0x20) != '.' || (name[nlen-3] | 0x20) != 'e' ||
            (name[nlen-2] | 0x20) != 'l' || (name[nlen-1] | 0x20) != 'f') {
            name[nlen++] = '.';
            name[nlen++] = 'E';
            name[nlen++] = 'L';
            name[nlen++] = 'F';
        }
        name[nlen] = '\0';

        static u8 spawn_img[16384];
        long got = fat_read_file(name, spawn_img, sizeof spawn_img);
        if (got <= 64) {
            r = (u64)SYS_EINVAL;      /* missing file = one error, v0 */
            break;
        }
        int tid = thread_spawn_user(spawn_prog_name(name), spawn_img, (u64)got);
        r = tid > 0 ? (u64)tid : (u64)SYS_EINVAL;
        break;
    }

    case SYSCALL_WAIT: {
        /* wait(): block until a child of ours has exited; return its
           exit code; -1 when we have no children at all. pid-specific
           waits arrive with real fork - the shell v0 runs one job. */
        for (;;) {
            u64 code = 0;
            if (sched_collect_child(&code)) {
                r = code;
                break;
            }
            if (!sched_any_children()) {
                r = (u64)SYS_EINVAL;
                break;
            }
            sched_block(SCHED_WQ_CHILD);  /* exit path wakes WQ_CHILD  */
        }
        break;
    }

    case SYSCALL_EXIT:
        /* First exit path in the OS: code is remembered (exit tables
           + the serial line), the thread turns zombie and is switched
           away from forever. */
        console_puts("proc: exit tid=");
        console_puts_hex(sched_tid());
        console_puts(" code=");
        console_puts_hex(tf->rdi);
        console_puts("\n");
        user_exits++;
        sched_exit_now(tf->rdi);           /* never returns            */
        /* not reached */

    default:
        r = (u64)SYS_EINVAL;
        break;
    }

    tf->rax = r;
}
