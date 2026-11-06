#pragma once
#include <kernel/types.h>

/* PS/2 reports Y positive upwards. The input ABI uses screen coordinates:
   positive X goes right and positive Y goes down. Keep the conversion at
   the hardware boundary rather than compensating in QEMU injectors. */
static inline void ps2_mouse_motion(const u8 packet[3], i16 *dx, i16 *dy)
{
    *dx = (i16)packet[1] - ((packet[0] & 0x10) ? 256 : 0);
    *dy = -((i16)packet[2] - ((packet[0] & 0x20) ? 256 : 0));
}
