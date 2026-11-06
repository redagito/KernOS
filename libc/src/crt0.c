#include <stdlib.h>

/*
 * crt0.c - the C runtime stub. The kernel set up everything a user
 * process is owed: CR3 space, CPL3 selectors, a 64 KiB stack ending at
 * 0x600000, and SysV-aligned entry at RSP 0x5ffff8. main() takes no
 * arguments; process_args retrieves the explicit command line, and
 * main's return value is the exit code.
 */

int main(void);

/* Own section so the link script can order it first without naming
   archive member files (the object lives in libc.a as lc_crt0.o). */
void _start(void)
    __attribute__((noreturn, noinline, section(".text.crt0")));

void _start(void)
{
    _exit(main());
}
