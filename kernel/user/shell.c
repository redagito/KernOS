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

/*
 * M6.5 (commit 2): the ksh band policy - the typed two-owner testbed.
 * Every ksh instance parks on the BOTTOM half of the face (the "ksh
 * band"); a ksh about to spawn makes ROOM first by re-claiming the
 * TOP half, so the child's own boot claim lands without overlap; when
 * a child exits, the parent re-takes the bottom. No argv, no role
 * handshake needed: identical code on both instances, the claim
 * REPLACEMENT rule ("a tid re-claiming replaces its own band") does
 * the rest. Bands are ROUTING geometry in commit 2 - the shared
 * console still renders whole-face (band-scoped rendering is commit
 * 3); the prompts overwrite each other on screen because the face is
 * one truth, and that is named, not hidden.
 */
static void claim_band(int top)
{
    long face = face_geometry();            /* rows<<16 | cols        */
    long rows = face >> 16;
    if (rows < 2)
        return;                             /* no band available      */
    long lo = rows / 2;
    if (surface_claim(top ? 0 : lo, top ? lo : rows - lo, 1) < 0)
        printf("ksh: band claim refused (%s)\n", top ? "top" : "bot");
}

/* M6.5 (commit 2): one nonblocking pointer poll per loop revolution.
 * The pointer reaches the kernel only through nr 7 pulls - a shell
 * blocked in read() is NOT a puller, so without this drain a click
 * sits in the ring forever while both shells wait for keys (the
 * deadlock the read gate's parked-pull complements: the kernel drains
 * for UNFOCUSED readers, the shell drains at its own loop top).
 */
static void drain_events(void)
{
    static char evbuf[16 * 8];              /* 8-byte records         */
    long n = input(evbuf, 16);
    if (n > 0)
        printf("ksh: drained=%ld\n", n);    /* calibration telemetry  */
}

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

    /* M6.5 (commit 2): band policy - bottom half is the ksh band.
       The kernel's claim line ('focus: claim owner=N band=24..48')
       is the gate's cue, not this print. A refusal still boots: the
       OS just stays open mode. */
    claim_band(0);

    for (;;) {
        printf("ksh> ");
        drain_events();

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
            /* M6.5 (commit 2): a parent makes room BEFORE spawning -
               top half becomes the parent's band, bottom goes vacant
               for the child's boot claim */
            claim_band(1);
            long pid = spawn(arg);
            if (pid == -2) {          /* SYS_ENAME: shape refusal      */
                printf("ksh: bad program name (%s)\n", arg);
                claim_band(0);
                continue;
            }
            if (pid < 0) {
                /* M5.11: pointed message - the missing-file meaning
                   the gate used to sign for EVERY refusal, including
                   its own name truncations */
                printf("ksh: no such program (%s)\n", arg);
                claim_band(0);
                continue;
            }
            printf("ksh: spawned %s pid=%ld\n", arg, pid);
            long code = wait();
            if (code < 0)
                printf("ksh: wait failed\n");
            else
                printf("ksh: pid %ld exited code=%ld\n", pid, code);
            /* the child's claim died with it; the parent re-takes
               the ksh band (served= totals for both tid printed at
               their own exits) */
            claim_band(0);
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
