#include <stdlib.h>

/*
 * crt0.c - the C runtime stub. The kernel set up everything a user
 * process is owed: CR3 space, CPL3 selectors, 16 KiB of zeroed stack
 * ending at 0x600000, and the entry VA from the ELF header. There is
 * no argc/argv until exec grows an environment (M5.5); main() takes
 * no arguments, and its return value IS the exit code.
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
