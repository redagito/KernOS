#include <kernel/types.h>
#include <kernel/input.h>
#include <kernel/ps2_mouse.h>
#include "mouse.h"
#include "irq.h"
#include "sched.h"

/*
 * mouse.c — PS/2 aux channel of the i8042 (M5.8).
 *
 * Init handshakes in while IRQ12 is still masked, so the ack byte is
 * consumed synchronously and the packet state machine starts clean;
 * handlers are wired only after the device said yes. The IRQ drains
 * every aux byte the controller owes (loop over the status register)
 * and feeds a 3-byte state machine: bit-3 sync on byte 0 required,
 * overflow-bit packets dropped and counted (lost motion is named,
 * never guessed), 9-bit signed deltas decoded from the sign bits.
 */

#define PS2_DATA   0x60
#define PS2_STAT   0x64
#define PS2_CMD    0x64

#define STAT_OBF   0x01          /* output buffer has a byte      */
#define STAT_AUX   0x20          /* 1 = the byte came from aux    */

#define CMD_ENABLE_AUX   0xA8    /* controller: un-disable aux    */
#define CMD_CONFIG_READ  0x20
#define CMD_CONFIG_WRITE 0x60
#define CMD_MOUSE_WRITE  0xD4    /* next 0x60 write goes to the mouse */
#define MOUSE_ENABLE     0xF4    /* device: start data reporting; ack 0xFA */

static u8 in8(u16 port)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void out8(u16 port, u8 val)
{
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

static volatile u64 packets, dx_total, dy_total, overflows;
static volatile u8  buttons;
static volatile u8  armed;
static u8  pack_i;               /* state machine (IF off only)   */
static u8  pack[3];

static void mouse_handler(struct trapframe *tf)
{
    (void)tf;
    for (;;) {
        u8 st = in8(PS2_STAT);
        u8 b;
        if (!(st & STAT_OBF) || !(st & STAT_AUX))
            return;              /* owed nothing, or a kbd byte   */

        b = in8(PS2_DATA);
        if (pack_i == 0) {
            if (!(b & 0x08))     /* bit 3 = packet start sync; stray
                                    resync noise is dropped and named */
                continue;
        }
        pack[pack_i++] = b;
        if (pack_i < 3) continue;
        pack_i = 0;

        if (pack[0] & 0xC0) {    /* overflow flags set: the motion */
            overflows++;         /* the wire dropped - counted, never
                                    decoded as seen motion */
            continue;
        }
        /* 9-bit signed deltas: sign bit 4/5 of byte 0 + 8-bit body */
        {
            i16 dx, dy;
            ps2_mouse_motion(pack, &dx, &dy);
            u8 now_buttons = (u8)(pack[0] & 0x07);
            u8 prev = buttons;
            buttons = now_buttons;
            dx_total = dx_total + (u64)(i64)dx;
            dy_total = dy_total + (u64)(i64)dy;
            packets++;
            /* M6.1: the event core sees everything the totals see -
               two REL events per packet, BTN events on transitions. */
            input_push_rel(dx, dy);
            if (now_buttons != prev)
                input_push_buttons(prev, now_buttons);
            /* M6.5 (commit 2): every push is a possible re-home
               trigger, and parked readers are woken to pull the ring.
               Without this wake the ring's only pullers would be the
               shells' own loop turns - and a read()-blocked owner
               (kbd IRQ only wakes WQ_KBD) plus an unfocused parker
               (WQ_FOCUS only) left clicks queued forever: the second
               focus deadlock, ended at the source. Wake DOCTRINE is
               kbd.c's: a flip, not a dispatch - the woken thread
               re-drains the ring and re-parks by its own loop-head
               re-check (ordered ring drain makes the post-apply
               hit-test see the button at the RIGHT cursor row).
               Unconditional per packet: pointer floods make this the
               churny hot path (30 moves = 30 wakes), and pointer
               floods are exactly when the ring wants draining; an
               empty wait queue makes the flip near-free. Which
               INPUT carries the button state is invisible here -
               the wakes ride the packet, not the transition. */
            sched_wake_all(SCHED_WQ_FOCUS);
            sched_wake_all(SCHED_WQ_KBD);
        }
    }
}

void mouse_init(void)
{
    u8 cfg;
    u16 i;
    u8 acked = 0;

    armed = 0;
    packets = dx_total = dy_total = overflows = 0;
    buttons = 0;
    pack_i = 0;

    /* drain any stale byte the BIOS left in the way (bounded) */
    for (i = 0; i < 1024 && (in8(PS2_STAT) & STAT_OBF); ++i)
        in8(PS2_DATA);

    out8(PS2_CMD, CMD_ENABLE_AUX);

    /* controller config: IRQ12 on, aux clock running. The
       TRANSLATION bit (6) belongs to the KEYBOARD port only - mouse
       bytes on the aux port are never translated, QEMU and real
       chips agree - so we leave it exactly as the firmware set it.
       (M5.8 cleared it and silently broke the keyboard: with
       translation off the native scan set leaks through - QEMU's
       keyboard defaults to set 3, real boards to set 2 - and kbd.c,
       a set-1 reader, decoded gibberish. The M6.1 typed gate caught
       it; clearing stays off the books.) */
    out8(PS2_CMD, CMD_CONFIG_READ);
    for (i = 0; i < 65535 && !(in8(PS2_STAT) & STAT_OBF); ++i) ;
    if (i == 65535) return;
    cfg = in8(PS2_DATA);
    cfg |= 0x02;                 /* enable int 12           */
    cfg &= (u8)~0x20;            /* aux clock not inhibited */
    out8(PS2_CMD, CMD_CONFIG_WRITE);
    out8(PS2_DATA, cfg);

    /* enable reporting + collect the ack BEFORE arming IRQ12 (when
       nothing else competes for 0x60). No ack = no device: named
       absence, the driver stays asleep. */
    out8(PS2_CMD, CMD_MOUSE_WRITE);
    out8(PS2_DATA, MOUSE_ENABLE);
    for (i = 0; i < 65535; ++i) {
        if (!(in8(PS2_STAT) & STAT_OBF)) continue;
        if (in8(PS2_DATA) == 0xFA) { acked = 1; break; }
    }
    if (!acked) return;

    irq_set_handler(12, mouse_handler);
    irq_enable(12);
    armed = 1;
}

int  mouse_armed(void)     { return armed; }
u64  mouse_packets(void)   { return packets; }
u64  mouse_dx_total(void)  { return dx_total; }
u64  mouse_dy_total(void)  { return dy_total; }
u64  mouse_overflows(void) { return overflows; }
u8   mouse_buttons(void)   { return buttons; }
