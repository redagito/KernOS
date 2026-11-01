#include "idt.h"
#include "trap.h"

/*
 * Kernel GDT + TSS + IDT.
 *
 * The GDT mirrors stage 2's layout exactly (null, code32, data, code16,
 * code64) with the same descriptor VALUES: CS=0x20/DS=ES=SS=0x10 loaded
 * from stage 2 therefore stay valid when we replace GDTR. At offset
 * 0x28 we install a 64-bit TSS跨界 descriptor only for one purpose
 * right now: IST1, the dedicated fault stack, so that #DF/#NMI/#MC can
 * switch to a known-good stack and still produce a readable dump.
 */

union dt_ptr {           /* GDTR/LIDTR operand shape */
    struct {
        u16 limit;
        u64 base;
    } __attribute__((packed));
    u64 raw[2];
};

/* Same 5 descriptors as boot/stage2.s (see its comment block). */
#define GDT_NULL     0x0000000000000000ULL
#define GDT_CODE32   0x00CF9A000000FFFFULL
#define GDT_DATA     0x00CF92000000FFFFULL
#define GDT_CODE16   0x000F9A000000FFFFULL
#define GDT_CODE64   0x00AF9A000000FFFFULL

#define SEL_DATA     0x10
#define SEL_CODE64   0x20
#define SEL_TSS      0x28

static u64 kernel_gdt[7];            /* 5 entries + 2 for the TSS pair */

static struct {
    u32 res0;
    u64 rsp0, rsp1, rsp2;
    u64 res1;
    u64 ist[7];
    u64 res2;
    u16 res3, iopb;
} __attribute__((packed)) kernel_tss;

static u64 fault_stack[512];         /* 4 KiB, IST1 for #DF/#NMI/#MC  */

/* Linker symbols from start.s/kernel.ld */
extern u8 stack_top[];
extern void *trap_stub_table[];

extern const char *const trap_names[32];
extern const u8 trap_ist[32];

void gdt_init(void)
{
    kernel_gdt[0] = GDT_NULL;
    kernel_gdt[1] = GDT_CODE32;
    kernel_gdt[2] = GDT_DATA;
    kernel_gdt[3] = GDT_CODE16;
    kernel_gdt[4] = GDT_CODE64;

    /* 64-bit TSS system descriptor (SYS, type 0x89), limit = size-1. */
    u64 base = (u64)&kernel_tss;
    u32 tss_bytes = sizeof kernel_tss;   /* must be 104 */
    kernel_gdt[5] = (u64)((tss_bytes - 1) & 0xFFFF)
                  | ((base & 0xFFFFFFULL) << 16)
                  | (0x89ULL << 40)
                  | (((base >> 24) & 0xFFULL) << 56);
    kernel_gdt[6] = (base >> 32) & 0xFFFFFFFFULL;

    kernel_tss.rsp0 = (u64)stack_top;
    kernel_tss.ist[0] = (u64)(fault_stack + 512);
    kernel_tss.iopb = sizeof kernel_tss;   /* no IO bitmap */

    union dt_ptr gdtr;
    gdtr.limit = sizeof kernel_gdt - 1;
    gdtr.base  = (u64)kernel_gdt;

    __asm__ __volatile__(
        "lgdt (%0)\n\t"
        "movw %1, %%ax\n\t"
        "ltr %%ax\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        :
        : "r"(&gdtr), "n"(SEL_TSS)
        : "rax", "memory");

    kernel_tss.rsp0 = (u64)stack_top;      /* restate: lgdt is order-agnostic */
}

void idt_init(void)
{
    static struct {                        /* SDM Vol.3 6.14.1: 16 bytes */
        u16 off_lo;
        u16 cs;
        u8  ist;
        u8  attr;
        u16 off_mid;
        u32 off_hi;
        u32 zero;
    } idt[256];

    const u64 *stubs = trap_stub_table;

    for (int v = 0; v < 32; ++v) {
        u64 addr = stubs[v];
        idt[v].off_lo  = addr & 0xFFFF;
        idt[v].cs      = SEL_CODE64;
        idt[v].ist     = trap_ist[v];
        idt[v].attr    = 0x8E;             /* interrupt gate, DPL0, present */
        idt[v].off_mid = (addr >> 16) & 0xFFFF;
        idt[v].off_hi  = (addr >> 32) & 0xFFFFFFFF;
    }

    union dt_ptr idtr;
    idtr.limit = sizeof idt - 1;
    idtr.base  = (u64)idt;

    __asm__ volatile("lidt (%0)" : : "r"(&idtr) : "memory");
}
