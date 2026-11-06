#include <kernel/syscall.h>

#include <stddef.h>
#include <unistd.h>

/*
 * unistd.c - the syscall front. One barrier shape, reused everywhere
 * (docs/syscall-abi.md): RAX = number, RDI/RSI/RDX = args, RAX back.
 * Everything except RAX survives; that is what "preserved" means.
 */

static long gate(long nr, long a1, long a2, long a3)
{
    long ret;
    register long sc __asm__("rax") = nr;   /* r10 is arg4 in the ABI */
    register long r1 __asm__("rdi") = a1;   /* shape: never maps slots
                                               r10+ from C except vx  */
    register long r2 __asm__("rsi") = a2;
    register long r3 __asm__("rdx") = a3;
    (void)r1; (void)r2; (void)r3;
    __asm__ __volatile__("int $0x80"
                         : "=a"(ret)
                         : "0"(sc), "r"(r1), "r"(r2), "r"(r3)
                         : "memory");
    return ret;
}

ssize_t write(int fd, const void *buf, size_t n)
{
    long r = gate(SYSCALL_WRITE, fd, (long)buf, (long)n);
    if (r < 0)
        return -1;
    return (ssize_t)r;
}

long surface_claim(long row, long h, long claim)
{
    long r = gate(SYSCALL_SURFACE, row, h, claim);
    return r < 0 ? -1 : r;              /* negated errno -> -1, v0 style */
}

long face_geometry(void)
{
    return gate(SYSCALL_SURFACE, (long)-1, 0, 0);
}

ssize_t read(int fd, void *buf, size_t n)
{
    long r = gate(SYSCALL_READ, fd, (long)buf, (long)n);
    if (r < 0)
        return -1;
    return (ssize_t)r;
}

long getpid(void)
{
    return gate(SYSCALL_GETPID, 0, 0, 0);
}

long spawn(const char *name)
{
    return gate(SYSCALL_SPAWN, (long)name, 0, 0);
}

long wait(void)
{
    return gate(SYSCALL_WAIT, 0, 0, 0);
}

/* M6.1: the event channel (docs/syscall-abi.md nr 7). */
long input(void *buf, long max)
{
    return gate(SYSCALL_INPUT, (long)buf, max, 0);
}

ssize_t file_load(const char *name, void *buf, size_t cap)
{ return gate(SYSCALL_FILE_LOAD, (long)name, (long)buf, cap); }
ssize_t file_save(const char *name, const void *buf, size_t size)
{ return gate(SYSCALL_FILE_SAVE, (long)name, (long)buf, size); }
long spawn_args(const char *name, const char *args)
{ return gate(SYSCALL_SPAWN_ARGS, (long)name, (long)args, 0); }
long process_args(char *buf, size_t cap)
{ return gate(SYSCALL_ARGS, (long)buf, cap, 0); }
long term_clear(void) { return gate(SYSCALL_TERM, 0, 0, 0); }
long term_move(long col, long row) { return gate(SYSCALL_TERM, 1, col, row); }
long term_geometry(void) { return gate(SYSCALL_TERM, 2, 0, 0); }
long term_caret(void) { return gate(SYSCALL_TERM, 3, 0, 0); }
long waitpid(long pid) { return gate(SYSCALL_WAITPID, pid, 0, 0); }
long term_paint(const char *buf, size_t len) { return gate(SYSCALL_PAINT, (long)buf, len, 0); }
