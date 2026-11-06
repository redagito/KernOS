#include <kernel/types.h>
#include <kernel/input.h>
#include "irq.h"
#include "kbd.h"
#include "sched.h"                     /* the wake hook (SCHED_WQ_KBD) */

/*
 * Scancode set 1 (the only real-world set on IBM PS/2 controllers that
 * reports function bytes 0x00..0xFF in the sam shape as the classic
 * keyboard docs). Port-level truth: status reg 0x64 bit 0 says the data
 * reg 0x60 has a byte; the controller forwards both keyboard and mouse
 * packets there; M5.8 added the aux guard, M6.1 the event push: every
 * translated keypress feeds BOTH channels - the ASCII ring fd 0 reads
 * (the text channel) and the event core (M6.1) - like the input core
 * handing vt and evdev the same keystroke.
 */

#define KBD_DATA   0x60
#define KBD_STAT   0x64

/* 2^N ring: head==tail means empty. */
#define RING_BITS  7
#define RING_SIZE  (1u << RING_BITS)
#define RING_MASK  (RING_SIZE - 1)

static volatile u8 ring[RING_SIZE];
static volatile u8 head, tail;
static volatile u64 events;

static volatile u8 shift_down;
static u8 ctrl_down;
static u8 extended_pending;

/* Set-1 make codes <= 0x7F we translate. Everything else maps to 0. */
static const char code_ascii[0x54] = {
    /* 0x00 */ 0,   0x1B, '1', '2', '3', '4', '5', '6',
    /* 0x08 */ '7', '8', '9', '0', '-', '=', 0x08, 0x09,   /* esc ... tab */
    /* 0x10 */ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    /* 0x18 */ 'o', 'p', '[', ']', 0x0A,                  /* 0x1C = enter */
    /* 0x1D */ 0,   'a', 's', 'd', 'f', 'g', 'h', 'j',    /* 0x1D = ctrl */
    /* 0x25 */ 'k', 'l', ';', '\'', '`',                  /* 0x25..0x29 */
    /* 0x2A */ 0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n',   /* 0x2A = lshift */
    /* 0x32 */ 'm', ',', '.', '/', 0,   '*',              /* 0x36=rshift */
    /* 0x38 */ 0,   ' ',                                  /* 0x38 alt, 0x39 space */
};

static u8 in8(u16 port)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void kbd_handler(struct trapframe *tf);

void kbd_init(void)
{
    head = tail = 0;
    events = 0;
    shift_down = 0;
    ctrl_down = 0;
    extended_pending = 0;
    irq_set_handler(1, kbd_handler);
    irq_enable(1);
}

static void push(char c)
{
    u8 next = (u8)((head + 1) & RING_MASK);
    if (next == tail) return;         /* full: drop, IRQ keeps flowing */
    u8 was_empty = (head == tail);
    ring[head] = (u8)c;
    head = next;
    events++;
    input_push_key(c);                /* the event core sees the same
                                         keystroke (M6.1) - full ring
                                         there is ITS counted drop */
    /* First byte of a fresh run: any thread sleeping in the gate's
       READ is hereby owed a look at the ring (M5.5). Waking is a
       state flip only - no switching, no flags games; the woken
       thread re-checks and re-blocks if the cookie was already
       eaten. Called with IF off from the IRQ; sched_wake_all is
       IRQ-safe by construction (single core, IF off everywhere). */
    if (was_empty)
        sched_wake_all(SCHED_WQ_KBD);
}

static void kbd_handler(struct trapframe *tf)
{
    (void)tf;
    u8 st = in8(KBD_STAT);
    if (!(st & 0x01)) return;          /* no byte: controller ghost */
    if (st & 0x20) return;             /* aux-origin byte (M5.8): the
                                          mouse driver's claim on
                                          IRQ12 - never steal it    */

    u8 sc = in8(KBD_DATA);

    /* E0/E1 prefixes: skip their follow-up byte (arrows, etc.). */
    if (extended_pending) {
        extended_pending = 0;
        if (sc == 0x1D || sc == 0x9D) { ctrl_down = !(sc & 0x80); return; }
        if (sc & 0x80) return;
        char nav = sc == 0x4B ? (char)0x80 : sc == 0x4D ? (char)0x81 :
                   sc == 0x48 ? (char)0x82 : sc == 0x50 ? (char)0x83 :
                   sc == 0x47 ? (char)0x84 : sc == 0x4F ? (char)0x85 :
                   sc == 0x53 ? (char)0x86 : 0;
        if (nav) push(nav);
        return;
    }
    if (sc == 0xE0 || sc == 0xE1) { extended_pending = 1; return; }

    u8 released = sc & 0x80;
    u8 plain    = sc & 0x7F;
    if (plain == 0x1D) { ctrl_down = !released; return; }

    if (plain == 0x2A || plain == 0x36) {   /* both shift keys */
        shift_down = !released;
        return;
    }
    if (released) return;                   /* break codes end here */

    if (plain < sizeof code_ascii) {
        char c = code_ascii[plain];
        if (c >= 'a' && c <= 'z' && shift_down)
            c = (char)(c - 'a' + 'A');
        else if (c && shift_down) {
            const char *plain_chars = "1234567890-=[];'/.,`\\";
            const char *shift_chars = "!@#$%^&*()_+{}:\"?><~|";
            for (int i = 0; plain_chars[i]; ++i)
                if (c == plain_chars[i]) { c = shift_chars[i]; break; }
        }
        if (ctrl_down && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
            c &= 31;
        if (c) push(c);
    }
}

/* M5.5: batch drain for the syscall gate's READ (fd 0). Caller runs
   with IF off (the gate guarantees it), so consumer vs. IRQ handler
   cannot interleave on the ring indices. Returns 0..max bytes. */
int kbd_pull(char *dst, int max)
{
    int n = 0;
    while (n < max && head != tail) {
        dst[n++] = (char)ring[tail];
        tail = (u8)((tail + 1) & RING_MASK);
    }
    return n;
}

u64 kbd_events(void)
{
    return events;
}
