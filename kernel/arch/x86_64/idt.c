#include "idt.h"
#include "trap.h"

/*
 * Kernel GDT + TSS + IDT.
 *
 * The GDT mirrors stage 2's layout exactly (null, code32, data, code16,
 * code64) with the same descriptor VALUES: CS=0x20/DS=ES=SS=0x10 loaded
 * from stage 2 therefore stay valid when we replace GDTR. M5.2 adds
 * CPL3 code/data descriptors at 0x30/0x38 for the process model; the
 * TSS moved down the list to 0x40 to make room (nothing outside this
 * file ever named the old 0x28 selector). The TSS exists for IST1 -
 * the dedicated fault stack so #DF/#NMI/#MC land on known-good ground
 * - and since M5.2 also carries a *variable* rsp0: the scheduler
 * re-points it at each user thread's kernel stack so gate/IRQ frames
 * arriving from CPL3 land in that thread's own stack.
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
#define SEL_TSS      0x40               /* moved below the user segs    */
#define SEL_USER_CS  0x30               /* CPL3 code (selector 0x33)    */
#define SEL_USER_DS  0x38               /* CPL3 data (selector 0x3B)    */

/* CPL3 (M5.2): user CS: L=1, DPL3. user DS: DPL3. */
#define GDT_USER_CS  0x00A0FA000000FFFFULL
#define GDT_USER_DS  0x00A0F2000000FFFFULL

static u64 kernel_gdt[10];           /* 5 legacy slots + 1 pad + 2 user + 2 tss */

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
extern u64 trap_stub_table[];

extern const char *const trap_names[32];
extern const u8 trap_ist[32];

void gdt_init(void)
{
    kernel_gdt[0] = GDT_NULL;
    kernel_gdt[1] = GDT_CODE32;
    kernel_gdt[2] = GDT_DATA;
    kernel_gdt[3] = GDT_CODE16;
    kernel_gdt[4] = GDT_CODE64;
    kernel_gdt[5] = 0;               /* pad: keeps numbering readable  */
    kernel_gdt[6] = GDT_USER_CS;
    kernel_gdt[7] = GDT_USER_DS;

    /* 64-bit TSS system descriptor (SYS, type 0x89), limit = size-1.
       Moved from 0x28 to 0x40 in M5.2: SEL_TSS is the only reference. */
    u64 base = (u64)&kernel_tss;
    u32 tss_bytes = sizeof kernel_tss;   /* must be 104 */
    kernel_gdt[8] = (u64)((tss_bytes - 1) & 0xFFFF)
                  | ((base & 0xFFFFFFULL) << 16)
                  | (0x89ULL << 40)
                  | (((base >> 24) & 0xFFULL) << 56);
    kernel_gdt[9] = (base >> 32) & 0xFFFFFFFFULL;

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

    /* Vectors 0..47: 32 exceptions + 16 remapped IRQs all in the
       stub table (isr.s .rodata). */
    for (int v = 0; v < 48; ++v) {
        u64 addr = stubs[v];
        idt[v].off_lo  = addr & 0xFFFF;
        idt[v].cs      = SEL_CODE64;
        idt[v].ist     = (v < 32) ? trap_ist[v] : 0;   /* IRQs: no IST */
        idt[v].attr    = 0x8E;             /* interrupt gate, DPL0, present */
        idt[v].off_mid = (addr >> 16) & 0xFFFF;
        idt[v].off_hi  = (addr >> 32) & 0xFFFFFFFF;
    }

    /* v0x80: the syscall gate (docs/syscall-abi.md). Same interrupt
       gate type as above but DPL 3 from day one: v0 callers are CPL0
       kernel threads (DPL never raises the entry bar for them), and
       M5.2's CPL3 callers enter the identical protocol unchanged. */
    {
        u64 saddr = stubs[0x80];
        idt[0x80].off_lo  = saddr & 0xFFFF;
        idt[0x80].cs      = SEL_CODE64;
        idt[0x80].ist     = 0;
        idt[0x80].attr    = 0xEE;      /* int gate, DPL 3, present */
        idt[0x80].off_mid = (saddr >> 16) & 0xFFFF;
        idt[0x80].off_hi  = (saddr >> 32) & 0xFFFFFFFF;
    }

    union dt_ptr idtr;
    idtr.limit = sizeof idt - 1;
    idtr.base  = (u64)idt;

    __asm__ volatile("lidt (%0)" : : "r"(&idtr) : "memory");
}

void tss_rsp0_set(u64 rsp0)
{
    /* On 64-bit the CPU reads rsp0 from the TSS *memory* at every
       privilege transition (the descriptor is only a base/limit);
       assignments after ltr simply take effect. */
    kernel_tss.rsp0 = rsp0;
}
