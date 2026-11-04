#include <kernel/types.h>
#include "mouse.h"
#include "irq.h"

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
            i16 dx = (i16)(u16)pack[1];
            i16 dy = (i16)(u16)pack[2];
            if (pack[0] & 0x10) dx = (i16)(dx - 256);
            if (pack[0] & 0x20) dy = (i16)(dy - 256);
            buttons = pack[0] & 0x07;
            dx_total = dx_total + (u64)(i64)dx;
            dy_total = dy_total + (u64)(i64)dy;
            packets++;
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

    /* controller config: IRQ12 on, aux clock running, no translation
       (translate-to-intelli-mouse would rewrite the packets). */
    out8(PS2_CMD, CMD_CONFIG_READ);
    for (i = 0; i < 65535 && !(in8(PS2_STAT) & STAT_OBF); ++i) ;
    if (i == 65535) return;
    cfg = in8(PS2_DATA);
    cfg |= 0x02;                 /* enable int 12           */
    cfg &= (u8)~0x20;            /* aux clock not inhibited */
    cfg &= (u8)~0x40;            /* not in translation mode */
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
