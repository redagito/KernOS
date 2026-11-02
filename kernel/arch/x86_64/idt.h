#pragma once

#include <kernel/types.h>

/*
 * Kernel GDT (mirror of the stage-2 layout so the selectors stage 2
 * loaded, 0x10/0x20, stay valid across lgdt), one 64-bit TSS for the
 * fault IST stack, and CPL3 code/data descriptors for the process
 * model. Also the IDT management.
 */

void gdt_init(void);   /* lgdt + fill TSS (rsp0, IST1) + ltr + reload data segs */
void idt_init(void);   /* fill the IDT from trap_stub_table + lidt */

/* CPL3 selectors (M5.2): user code 0x30/0x33, user data 0x38/0x3B. */
#define SEL_USER_CODE  0x30
#define SEL_USER_CS3   0x33
#define SEL_USER_DATA  0x38
#define SEL_USER_DS3   0x3B

/* Point TSS.rsp0 at a new kernel stack (called by the scheduler when
   switching to/from a user-carrying thread). */
void tss_rsp0_set(u64 rsp0);
