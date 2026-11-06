#pragma once

#include <kernel/types.h>
#include <kernel/input.h>

/*
 * focus (M6.5 commit 1): the registry that turns "who does the
 * keyboard serve next" from an accident of scheduling into kernel
 * state. Core v0 law: ONE owner tid holds keyboard focus; owner 0 is
 * OPEN MODE - before the first surface claim, every claimant with a
 * read() gets served, which keeps the pre-focus era honest (the boot
 * shell owns the machine until it claims the face).
 *
 * What focus is NOT (doctrine, v0):
 *   - not a window system: claims are row BANDS of the console face
 *     (the rectangle arrives with commit 3's band-scoped rendering);
 *   - not mouse routing: the pointer never routes to programs - it
 *     composites the sprite (one writer, cursor_apply). A button
 *     PRESS is merely a focus COMMAND: its click re-homes the owner
 *     by hit-testing the claim bands;
 *   - not byte ownership: the keyboard byte FIFO is untouched - an
 *     unfocused reader PARKS (WQ_FOCUS) with nothing popped, so keys
 *     typed while A holds focus wait intact for whoever the click
 *     names next. Serial/echo honesty is unchanged (unedited).
 *
 * Keys keep TWO honest channels (M6.1 doctrine): the event ring for
 * the polled GUI view (u1's gate counters stay put), and the
 * focus-routed terminal read. The window-manager era collapses them;
 * v0 carries both and counts them.
 *
 * Concurrency: every mutator below runs CALLER-IF-OFF (boot gate
 * solo for init, the 0x80 syscall gate for claims/clicks/reads) -
 * single core, IF-off is mutual exclusion, same discipline the
 * cursor and the event ring live under.
 */

#define FOCUS_MAX_CLAIMS 8        /* bands the registry holds at once */
#define FOCUS_MAX_TIDS   32       /* per-owner served[] bookkeeping;
                                     tids are sched slots (max 6 live)
                                     - exit records are read before
                                     slot reuse, so 32 is safe ledger
                                     depth, not a guess                */

/* claims: which band belongs to whom. Multiple tids never share a
   claim slot; overlapping BANDS are legal by registration and the
   FIRST matching band in scan order wins a hit-test (named here, not
   invented at the click). */
struct focus_claim {
    int tid;                      /* 0 = slot empty                    */
    int row, h;                   /* the band [row, row+h) in cells    */
};

/* The kernel-side boot init: zero the registry, open mode, named
   cue - 'focus: substrate ok open' ('open' literally: owner 0). */
void focus_init(void);

/* SURFACE body. row<0 = probe: geometry, nothing else. Returns the
   syscall's RAX value (>= 0 ok, SYS_EINVAL on refusal). claim!=0
   takes focus; a claim always wakes parked readers (they re-check,
   sched doctrine rule e). */
long focus_surface_call(long row, long h, long claim, int tid);

/* READ-button re-home: scan an nr7 pull batch for button PRESSES and
   hit-test the cursor cell against the claim bands. A press outside
   every band is a no-op (the backdrop is not a program - the
   negative control). */
void focus_clicks(const struct input_event *ev, long n);

/* READ routing: the one question the read gate asks each loop turn. */
int  focus_allows_read(int tid);

/* Parked-reader + served-bytes accounting (read gate calls both). */
void focus_note_park(void);
void focus_note_served(int tid, long bytes);

/* Exit hook: drop the tid's claim AND ownership, wake parked readers
   (open mode serves them again), and print the named line
   'focus: exit owner=N served=K' - K = bytes the read gate delivered
   to this tid the whole registry life of the tid. Silent for a tid
   that owned nothing (the boot workers exit unclaimed). */
void focus_thread_exit(int tid);

/* Counters for gates: parked reader turns, claims live, owner tid. */
u64  focus_parks(void);
u64  focus_claims(void);
int  focus_owner_tid(void);
int focus_band(int tid, int *row, int *height);
void focus_thread_start(int tid);
