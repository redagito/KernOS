#include <kernel/types.h>
#include "irq.h"
#include "serial.h"
#include "pit.h"
#include "sched.h"

/*
 * 8254 PIT channel 0 →IRQ0, mode 2 (rate generator). Base 1193182 Hz
 * (SDM-independent spec fact: this clock was NTSC-derived; it forms the
 * PC timer we calibrate everything else against).
 */

#define PIT_CH0_DATA   0x40
#define PIT_CMD        0x43

#define PIT_INPUT_HZ   1193182ULL

static volatile u64 ticks;

static void pit_handler(struct trapframe *tf)
{
    (void)tf;
    ticks++;
    /* EOI before schedule(): a preemptive switch never returns through
       irq_dispatch's tail, and an un-EOI'd PIC would go silent (ISR
       bit for IRQ0 stuck -> no further ticks -> every hlt sleeps for
       good). The switched-away invocation still EOIs again later from
       its own tail - ignored if nothing is in service. */
    irq_eoi(0);
    schedule();                        /* M2.4: preemptive round-robin */
}

static void out8(u16 port, u8 val) { __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port)); }

void pit_init(void)
{
    const u32 hz         = 100;
    const u32 divisor    = (u32)(PIT_INPUT_HZ / hz);   /* 11932 */

    out8(PIT_CMD, 0x34);   /* ch0, access L/H, mode 2, binary */
    out8(PIT_CH0_DATA, divisor & 0xFF);
    out8(PIT_CH0_DATA, (divisor >> 8) & 0xFF);

    irq_set_handler(0, pit_handler);
    irq_enable(0);

    serial_puts("tick 100hz\n");
}

u64 pit_ticks(void)
{
    return ticks;
}
