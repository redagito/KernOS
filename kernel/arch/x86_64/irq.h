#pragma once

#include <kernel/types.h>
#include "trap.h"

/*
 * Legacy 8259A PIC stay controller (M2.2): remapped to vectors 32..47,
 * interrupt handler registry + spurious-IRQ bookkeeping, EOI policy.
 * The APIC takes over in M2.5; the PIC is the teaching wheels.
 */

#define IRQ_VEC_BASE  32    /* master 32..39, slave 40..47 */
#define IRQ_COUNT     16

typedef void (*irq_handler_t)(struct trapframe *tf);

void irq_init(void);                             /* remap + mask-all */
void irq_enable(u8 irq);                         /* unmask exactly one */
void irq_set_handler(u8 irq, irq_handler_t fn);  /* NULL = spurious sink */
void irq_eoi(u8 irq);                            /* early-EOI for switching handlers */
