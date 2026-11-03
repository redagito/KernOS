/*
 * shell.c - KernOS's first shell (M5.5): the program that reads the
 * user. Freestanding C on our libc; every line it prints goes through
 * the write gate, every keypress arrives through the read gate (which
 * blocks until the keyboard IRQ has something - the first IRREVERSIBLY
 * blocking thing userland has ever done in this kernel).
 *
 * Vocabulary, v0:
 *   run <name>   spawn name.ELF as a child, wait it out, report code
 *   echo <rest>  print the rest back
 *   exit         end the shell (main.c joins it; boot gate ends)
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define LINE_MAX 40

/* One line from the gate. Echoes as it goes (the kernel does not). */
static int getline(char *b, int cap)
{
    int n = 0;
    for (;;) {
        char c;
        ssize_t k = read(STDIN_FILENO, &c, 1);
        if (k <= 0)
            continue;              /* read blocks; 0 never returns here */
        if (c == '\n') {
            putchar('\n');
            b[n] = '\0';
            return n;
        }
        if (c == '\b') {           /* erase: honest terminal is honest  */
            if (n) {
                --n;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
            continue;
        }
        if (n < cap - 1) {
            b[n++] = c;
            putchar(c);
        }
    }
}

int main(void)
{
    printf("ksh: kernos shell ready\n");

    for (;;) {
        printf("ksh> ");

        char line[LINE_MAX];
        getline(line, LINE_MAX);

        if (!strcmp(line, ""))
            continue;

        if (!strcmp(line, "exit"))
            return 0;              /* crt0 -> exit(0); the kernel joins */

        if (!strncmp(line, "run ", 4)) {
            const char *arg = line + 4;
            if (!*arg) {
                printf("ksh: run what?\n");
                continue;
            }
            long pid = spawn(arg);
            if (pid < 0) {
                printf("ksh: run refused (%s)\n", arg);
                continue;
            }
            printf("ksh: spawned %s pid=%ld\n", arg, pid);
            long code = wait();
            if (code < 0)
                printf("ksh: wait failed\n");
            else
                printf("ksh: pid %ld exited code=%ld\n", pid, code);
            continue;
        }

        if (!strncmp(line, "echo ", 5)) {
            printf("%s\n", line + 5);
            continue;
        }
        if (!strcmp(line, "echo")) {
            putchar('\n');
            continue;
        }

        printf("ksh: unknown command (%s)\n", line);
    }
}
