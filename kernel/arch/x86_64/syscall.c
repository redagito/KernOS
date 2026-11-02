#include <kernel/types.h>
#include <kernel/syscall.h>
#include "syscall.h"
#include "sched.h"
#include "serial.h"

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
            serial_putc((char)buf[i]);
        r = n;
        break;
    }

    case SYSCALL_EXIT:
        /* First exit path in the OS: code is remembered (arg1), the
           thread turns zombie and is switched away from forever. */
        serial_puts("proc: exit tid=");
        serial_puts_hex(sched_tid());
        serial_puts(" code=");
        serial_puts_hex(tf->rdi);
        serial_puts("\n");
        user_exits++;
        sched_exit_now();                  /* never returns            */
        /* not reached */

    default:
        r = (u64)SYS_EINVAL;
        break;
    }

    tf->rax = r;
}
