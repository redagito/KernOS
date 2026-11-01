#pragma once

/*
 * Kernel GDT (mirror of the stage-2 layout so CSI selectors 0x10/0x20
 * stay valid across lgdt, plus a 64-bit TSS for the fault IST stack),
 * and the IDT management.
 */

void gdt_init(void);   /* lgdt + fill TSS (rsp0, IST1) + ltr + reload data segs */
void idt_init(void);   /* fill the IDT from trap_stub_table + lidt */
