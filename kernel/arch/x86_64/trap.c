#include <kernel/types.h>
#include "trap.h"
#include "idt.h"
#include "serial.h"

/*
 * Exception dump: M2.1 policy is print-and-park. There is no recovery
 * path yet; stacks and scheduler come later. The point of this stage is
 * that any kernel fault prints a *mandatory* and *complete* reflection
 * over serial instead of the machine silently resetting.
 */

const char *const trap_names[32] = {
    "#DE divide error",  "#DB debug",       "NMI",            "#BP breakpoint",
    "#OF overflow",      "#BR bound range", "#UD invalid op", "#NM dev not avail",
    "#DF double fault",  "#SO seg overrun", "#TS invalid TSS","#NP no segment",
    "#SS stack fault",   "#GP prot fault",  "#PF page fault", "(reserved)",
    "#MF x87",           "#AC alignment",   "#MC machine",    "#XM SIMD",
    "#VE virt",          "#CP control",     "(res 22)",       "(res 23)",
    "(res 24)",          "(res 25)",        "(res 26)",       "(res 27)",
    "(res 28)",          "(res 29)",        "(res 30)",       "(res 31)",
};

/* IST stack (index 1) for faults that may strike on an unstable stack. */
const u8 trap_ist[32] = {
    [2]  = 1,   /* NMI */
    [8]  = 1,   /* #DF */
    [18] = 1,   /* #MC */
};

static const char *reg_names[15] = {
    /* struct order = stack order: r15 pushed last (lowest address) */
    "r15", "r14", "r13", "r12", "rbp", "rbx", "rax", "r11", "r10", "r9",
    "r8",  "rcx", "rdx", "rsi", "rdi",
};

static void prints(const char *s) { serial_puts(s); }

static void prnum(u64 v)
{
    serial_puts_hex(v);
}

static u64 cr_read(int which)
{
    u64 v;
    switch (which) {
    case 0: __asm__ volatile("mov %%cr0, %0" : "=r"(v)); break;
    case 2: __asm__ volatile("mov %%cr2, %0" : "=r"(v)); break;
    case 3: __asm__ volatile("mov %%cr3, %0" : "=r"(v)); break;
    default: __asm__ volatile("mov %%cr4, %0" : "=r"(v)); break;
    }
    return v;
}

/* M2.1 bring-up: own GDT/TSS first (fault stack), then the IDT. */
void trap_init(void)
{
    gdt_init();
    idt_init();
}

void dump_trap(struct trapframe *tf)
{    prints("\n*** KernOS FAULT: ");
    prints(trap_names[tf->vector] ? trap_names[tf->vector] : "?");
    prints(" (vec ");
    prnum(tf->vector);
    prints(", err ");
    prnum(tf->err);
    prints(") ***\n");

    const u64 *regs = &tf->r15;
    for (int i = 0; i < 15; ++i) {
        prints("\n  ");
        prints(reg_names[i]);
        prints("=");
        prnum(regs[i]);
    }
    prints("\n");

    prints("  rip="); prnum(tf->rip);
    prints(" cs=");  prnum(tf->cs);
    prints(" rfl="); prnum(tf->rflags);
    prints(" rsp="); prnum(tf->rsp);
    prints("\n  cr0="); prnum(cr_read(0));
    prints(" cr3="); prnum(cr_read(3));
    prints(" cr4="); prnum(cr_read(4));
    if (tf->vector == 14) {
        prints("\n  CR2 (fault addr)=");
        prnum(cr_read(2));
    }
    prints("\n");

    prints("halting (M2.1: dump-and-park)\n");
    while (1)
        __asm__ volatile("cli; hlt");
}
