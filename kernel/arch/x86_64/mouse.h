#pragma once

#include <kernel/types.h>

/*
 * PS/2 mouse via the i8042's secondary channel, IRQ12 (M5.8). The
 * driver arms the aux port at init (enable IRQ12 in the controller
 * config, un-disable the aux clock, 0xF4 data reporting) and decodes
 * standard 3-byte packets in the IRQ; consumers read only totals
 * (motion the mouse produced since armed) - the per-event API waits
 * for the M6 input layer, dead code never lands here.
 *
 * The keyboard shares ports 0x60/0x64: kbd.c's handler now leaves
 * aux-origin bytes (status bit 5) alone and this driver consumes only
 * aux-origin bytes, so neither can steal the other's queue.
 */

void mouse_init(void);      /* aux handshake; outcome via mouse_armed */

int  mouse_armed(void);     /* 1 after 0xF4 was acknowledged          */

/* Deltas are the signed running sums of decoded packet motion;
   overflow-tagged packets are dropped and counted, never decoded
   (motion the wire lost is not motion anyone saw). */
u64  mouse_packets(void);
u64  mouse_dx_total(void);
u64  mouse_dy_total(void);
u64  mouse_overflows(void);
u8   mouse_buttons(void);   /* button bitmap of the newest packet     */
