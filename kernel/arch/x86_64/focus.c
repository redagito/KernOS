#include <kernel/types.h>
#include "focus.h"
#include "sched.h"
#include "fbcon.h"
#include "cursor.h"
#include "console.h"

/*
 * focus.c (M6.5 commit 1) - the substrate. The whole state machine
 * fits in one screen: a claim table, an owner tid, two counters.
 * Every named line this module prints lands on serial AND the face
 * (the console mirror), so the driver decks cue on the exact text.
 *
 * Lines (exact, on every transition - never on no-ops):
 *   focus: substrate ok open            (init: owner 0, empty table)
 *   focus: claim owner=N band=r..r2     (self-claim through SURFACE)
 *   focus: rehome owner=N band=r..r2    (click h-focus command)
 *   focus: exit owner=N served=K        (exit hook of an owner)
 */

static struct focus_claim claims[FOCUS_MAX_CLAIMS];
static int owner;                             /* focus_owner tid; 0 open */
static u64 parks;                             /* reader park turns       */
static u64 served_by[FOCUS_MAX_TIDS];         /* bytes the gate served   */

/* decimal, the boot-log way (console.h has no putdec; every gate
   module that prints counters carries its own tiny one - cursor.c,
   sched.c ledgers, and now here). */
static void putdec(u64 v)
{
    char b[24];
    int i = (int)sizeof b;
    if (v == 0) b[--i] = '0';
    while (v) { b[--i] = (char)('0' + (int)(v % 10)); v /= 10; }
    while (i < (int)sizeof b) console_putc(b[i++]);
}

void focus_init(void)
{
    for (int i = 0; i < FOCUS_MAX_CLAIMS; ++i) {
        claims[i].tid = 0;
        claims[i].row = claims[i].h = 0;
    }
    for (int i = 0; i < FOCUS_MAX_TIDS; ++i) served_by[i] = 0;
    owner = 0;
    parks = 0;

    /* The cue is conditional on the state ACTUALLY being what the
       line says - the green print can't lie (M6.1 gate doctrine). */
    if (owner == 0 && claims[0].tid == 0)
        console_puts("focus: substrate ok open\n");
    else
        console_puts("focus: substrate init FAILED\n");
}

void focus_note_park(void)
{
    parks++;
}

void focus_note_served(int tid, long bytes)
{
    if (tid >= 0 && tid < FOCUS_MAX_TIDS)
        served_by[tid] += (u64)bytes;
}

u64 focus_parks(void)   { return parks; }

u64 focus_claims(void)
{
    u64 n = 0;
    for (int i = 0; i < FOCUS_MAX_CLAIMS; ++i)
        if (claims[i].tid) ++n;
    return n;
}

int focus_owner_tid(void) { return owner; }

int focus_allows_read(int tid)
{
    /* OPEN mode serves everyone; owned mode serves exactly one. */
    return owner == 0 || owner == tid;
}

long focus_surface_call(long row, long h, long claim, int tid)
{
    if (row < 0) {
        /* PROBE: the face's geometry, rows<<16|cols - the caller's
           way to size a claim without transmitting kernel internals
           another way. Zero rows means no fb face (VGA-text-only
           boot); honest 0, and the shell simply never claims. */
        return (long)(((u64)fbcon_rows() << 16) | (u64)fbcon_cols());
    }

    if (h < 1 || row < 0)
        return (long)(u64)-1;                 /* EINVAL            */
    if (claim != 1)
        return (long)(u64)-1;                 /* v0 bands are focus
                                                 claims: intent required
                                                 (the probe is row<0)  */
    if ((u64)row >= (u64)fbcon_rows() ||
        (u64)(row + h) > (u64)fbcon_rows())
        return (long)(u64)-1;                 /* band not on face  */

    /* Replace this tid's own claim first; else take an empty slot.
       A registry full of LIVE claims refuses (8 programs is years
       of room in v0; growing it later is a libc-side same-number
       fix, the ABI never changed). */
    int slot = -1;
    for (int i = 0; i < FOCUS_MAX_CLAIMS && slot < 0; ++i)
        if (claims[i].tid == tid) slot = i;
    for (int i = 0; i < FOCUS_MAX_CLAIMS && slot < 0; ++i)
        if (claims[i].tid == 0) slot = i;
    if (slot < 0)
        return (long)(u64)-1;                 /* registry full     */

    claims[slot].tid = tid;
    claims[slot].row = (int)row;
    claims[slot].h   = (int)h;
    owner = tid;                              /* claim = take focus */
    sched_wake_all(SCHED_WQ_FOCUS);           /* parked readers
                                                 re-check doctrine */

    console_puts("focus: claim owner=");
    putdec((u64)tid);
    console_puts(" band=");
    putdec((u64)row);
    console_puts("..");
    putdec((u64)(row + h));
    console_puts("\n");
    return (long)((u64)tid);                  /* owner tid answers  */
}

void focus_clicks(const struct input_event *ev, long n)
{
    int pressed = 0;
    for (long i = 0; i < n; ++i)
        if (ev[i].type == IN_EVT_BTN && ev[i].value == 1) {
            pressed = 1;
            break;
        }
    if (!pressed) return;                     /* motion/keys: silent */

    u32 gh = fbcon_glyph_h();
    if (gh == 0) return;                      /* no face, no rows    */
    long r = (long)(cursor_y() / gh);

    /* First matching band in scan order wins (focus.h named this
       resolution for overlapping bands - deterministic, not lucky). */
    for (int i = 0; i < FOCUS_MAX_CLAIMS; ++i) {
        if (claims[i].tid == 0) continue;
        if (r < claims[i].row || r >= claims[i].row + claims[i].h)
            continue;
        if (owner != claims[i].tid) {
            owner = claims[i].tid;
            sched_wake_all(SCHED_WQ_FOCUS);   /* parked readers on
                                                 BOTH sides re-check */
            sched_wake_all(SCHED_WQ_KBD);     /* M6.5 (commit 2): the
               new owner may be parked on the kbd queue with bytes
               ALREADY in its FIFO (queued while an earlier owner
               serially held the gate): a re-home to T is exactly a
               byte's arrival for T's reader, so the kbd waiters get
               the same flip. Without it the new owner sleeps until
               the NEXT physical keystroke - a delayed wake disguised
               as working, missed in the failed green run's P8 timing. */
            console_puts("focus: rehome owner=");
            putdec((u64)owner);
            console_puts(" band=");
            putdec((u64)claims[i].row);
            console_puts("..");
            putdec((u64)(claims[i].row + claims[i].h));
            console_puts("\n");
        }
        return;                               /* hit: stop at first  */
    }
    /* No band held the click: the backdrop - a no-op BY DOCTRINE
       (the negative control; focus never falls to zero on a miss). */
}

void focus_thread_exit(int tid)
{
    int had_claim = 0;
    for (int i = 0; i < FOCUS_MAX_CLAIMS; ++i)
        if (claims[i].tid == tid) {
            claims[i].tid = 0;
            claims[i].row = claims[i].h = 0;
            had_claim = 1;
        }
    int was_owner = (owner == tid);
    if (was_owner) owner = 0;
    if (!had_claim && !was_owner)
        return;                               /* unclaimed life: silent */

    u64 k = (tid >= 0 && tid < FOCUS_MAX_TIDS) ? served_by[tid] : 0;
    sched_wake_all(SCHED_WQ_FOCUS);           /* open mode serves again */

    console_puts("focus: exit owner=");
    putdec((u64)tid);
    console_puts(" served=");
    putdec(k);
    console_puts("\n");
}
