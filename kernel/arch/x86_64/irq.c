#include <kernel/types.h>
#include "irq.h"
#include "serial.h"
#include "console.h"

/*
 * 8259A cascade reprogrammed inside the kernel. BIOS got us here with
 * some unknown vector base and mask state; stage 2 masked everything,
 * so we own a quiet controller: init (ICW1..4), base 32, then unmask
 * only what the kernel explicitly enables.
 *
 * Insider hazard we keep on the books: after the ICW sequence the IMR
 * is zero until the OCW1 we write below - that window is why we do the
 * masks FIRST.
 */

#define MASTER_CMD   0x20
#define MASTER_DATA  0x21
#define SLAVE_CMD    0xA0
#define SLAVE_DATA   0xA1
#define SPURIOUS_NONE 0

static void out8(u16 port, u8 val) { __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port)); }
static u8   in8(u16 port)          { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }

static irq_handler_t handlers[IRQ_COUNT];
static u64 spurious_master, spurious_slave;

void irq_init(void)
{
    /* 1. Mask everything pending the ICW sequence. */
    out8(MASTER_DATA, 0xFF);
    out8(SLAVE_DATA,  0xFF);

    /* 2. Four ICWs: init, cascade, vectors 32/40, 8086 mode. */
    out8(MASTER_CMD, 0x11);
    out8(MASTER_DATA, IRQ_VEC_BASE);        /* 32 */
    out8(MASTER_DATA, 0x04);                /* slave on line 2 */
    out8(MASTER_DATA, 0x01);                /* 8086 mode, no auto-EOI */
    out8(SLAVE_CMD, 0x11);
    out8(SLAVE_DATA, IRQ_VEC_BASE + 8);     /* 40 */
    out8(SLAVE_DATA, 0x02);                 /* cascade id = 2 */
    out8(SLAVE_DATA, 0x01);

    /* 3. All masked again; irq_enable() will carve out. */
    out8(MASTER_DATA, 0xFF);
    out8(SLAVE_DATA, 0xFF);
}

void irq_enable(u8 irq)
{
    u16 port;
    u8 mask;

    if (irq >= IRQ_COUNT) return;
    port  = (irq < 8) ? MASTER_DATA : SLAVE_DATA;
    mask  = (u8)in8(port) & ~(u8)(1u << (irq % 8));
    out8(port, mask);
    if (irq >= 8) {
        /* slave lines ride master pin 2 (cascade); if master's own
           mask still holds that line shut, unmasking the slave is
           noise - unmask the cascade with the slave line. */
        mask = (u8)in8(MASTER_DATA) & (u8)~0x04;
        out8(MASTER_DATA, mask);
    }
}

void irq_set_handler(u8 irq, irq_handler_t fn)
{
    if (irq < IRQ_COUNT) handlers[irq] = fn;
}

/* RFLAGS.IF is bit 9. */
#define RFLAGS_IF 0x200u

u64 irq_save(void)
{
    u64 flags;
    __asm__ volatile("pushfq\n\tpopq %0" : "=r"(flags));
    if (flags & RFLAGS_IF)
        __asm__ volatile("cli");
    return flags;
}

void irq_restore(u64 flags)
{
    if (flags & RFLAGS_IF)
        __asm__ volatile("sti");
}

/* End-of-interrupt, for handlers that preemptively switch stacks away
   mid-handler and so never reach irq_dispatch's tail. Safe to send
   again later: with no ISR bit set the PIC ignores the EOI. */
void irq_eoi(u8 irq)
{
    if (irq >= 8) out8(SLAVE_CMD, 0x20);
    out8(MASTER_CMD, 0x20);
}

void irq_dispatch(struct trapframe *tf)
{
    u32 vec = (u32)tf->vector;
    /* Whatever we did not set a handler for is either spurious or
       something enabled later; EOI either way so the PIC unplugs. */
    u8 irq = (u8)(vec - IRQ_VEC_BASE);

    if (vec >= IRQ_VEC_BASE && vec < IRQ_VEC_BASE + IRQ_COUNT) {
        if (handlers[irq]) handlers[irq](tf);
        /* EOI: master always; slave additionally for lines 8-15. */
        if (irq >= 8) out8(SLAVE_CMD, 0x20);
        out8(MASTER_CMD, 0x20);
        return;
    }

    /* spurious: master IRQ7 (line 7 check via ISR would be exact; we
       just count and EOI master only). */
    if (vec == 39) { spurious_master++; out8(MASTER_CMD, 0x20); }
    else if (vec == 47) { spurious_slave++; out8(SLAVE_CMD, 0x20); }
    else {
        console_puts("IRQ: unmapped vec ");
        console_puts_hex(vec);
        console_puts("\n");
    }
}
