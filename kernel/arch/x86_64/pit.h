#pragma once

#include <kernel/types.h>

/*
 * 8254 PIT channel 0 as the system heartbeat (M2.2). ~100 Hz.
 * The APIC timer replaces this as the tick source per the roadmap;
 * the PIT stays as the calibration reference + pre-APIC source.
 */

void pit_init(void);          /* program channel 0, hookup IRQ0 */
u64  pit_ticks(void);
