#include <kernel/types.h>
#include "proc.h"
#include "pmm.h"
#include "serial.h"

/*
 * M5.2 process model, see proc.h for the contract.
 *
 * Everything walks the boot hierarchy through the identity map
 * (PA == VA below 1 GiB): a P word IS its target's address. User PTEs
 * are P|U|W = 0x07. No NX anywhere yet (no FPU/xstate work; NX arrives
 * with the W^X split together with demand paging for userland).
 */

static u64 cr3_read(void)
{
    u64 v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static void write_cr3(u64 v)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory");
}

static void zero_frame(u64 pa)
{
    volatile u64 *p = (volatile u64 *)pa;
    for (u32 i = 0; i < 4096 / sizeof(u64); ++i)
        p[i] = 0;
}

static void copy_page(u64 dst_pa, const void *src)
{
    volatile u64 *d = (volatile u64 *)dst_pa;
    const u64 *s = (const u64 *)src;      /* src is identity-mapped VA  */
    for (u32 i = 0; i < 4096 / sizeof(u64); ++i)
        d[i] = s[i];
}

/* Clone the boot hierarchy for slot-0 (identity + user window), patch
   the user window, map the process's pages, return the root PML4 PA.
   frames[] accumulates every allocated frame; caller frees them all on
   exit. nframes caps at 10: 4 tables + 2 code + 4 stack. */

u64 proc_build_space(const void *blob, u64 blob_len, u64 *frames, int *nframes)
{
    if (blob_len > USER_MAX_BLOB)
        return 0;

    int nf = 0;
    u64 boot_cr3 = cr3_read() & ~0xFFFULL;
    u64 *boot_pml4 = (u64 *)boot_cr3;
    u64 *boot_pdpt = (u64 *)(boot_pml4[0] & ~0xFFFULL);
    u64 *boot_pd   = (u64 *)(boot_pdpt[0] & ~0xFFFULL);

/* Clones: fresh, then patched. Every intermediate entry on the
       user path carries P|U|W so CPL3 can descend; supervisors-only
       leaves under U parents stay equally uncontaminated. */
    u64 pml4c = pmm_alloc();
    if (!pml4c) return 0;
    frames[nf++] = pml4c;
    zero_frame(pml4c);
    u64 *pml4v = (u64 *)pml4c;
    for (int i = 0; i < 512; ++i)
        pml4v[i] = boot_pml4[i];          /* share the whole boot map  */
    pml4v[0] = 0;                         /* rebuilt fresh below       */

    u64 pdptc = pmm_alloc();
    if (!pdptc) { write_cr3(boot_cr3); return 0; }  /* paranoia only */
    frames[nf++] = pdptc;
    zero_frame(pdptc);
    u64 *pdptv = (u64 *)pdptc;
    for (int i = 1; i < 512; ++i)
        pdptv[i] = boot_pdpt[i];
    /* slot 0 wired to the personal PD below */

    u64 pdc = pmm_alloc();
    if (!pdc) return 0;
    frames[nf++] = pdc;
    zero_frame(pdc);
    u64 *pdv = (u64 *)pdc;
    for (int i = 0; i < 512; ++i)
        pdv[i] = boot_pd[i];
    pdv[2] = 0;                           /* PT replaces 2-MiB leaf    */

    u64 pt = pmm_alloc();
    if (!pt) return 0;
    frames[nf++] = pt;
    zero_frame(pt);
    u64 *ptv = (u64 *)pt;

    /* hook the clones together (P|U|W on intermediates) */
    pdptv[0] = pdc | 0x07;
    pdv[2]   = pt  | 0x07;
    pml4v[0] = pdptc | 0x07;

    /* code pages at 0x400000.. : copy blob into fresh zero frames */
    int code_pages = (int)((blob_len + 4095) / 4096);
    if (code_pages == 0) code_pages = 1;
    for (int cpg = 0; cpg < code_pages; ++cpg) {
        u64 pa = pmm_alloc();
        if (!pa) return 0;
        frames[nf++] = pa;
        zero_frame(pa);
        copy_page(pa, (const u8 *)blob + cpg * 4096);
        ptv[cpg] = pa | 0x07;
    }

    /* stack pages at 0x5FC000..0x600000. PT slots are (va>>12)&0x1FF,
       i.e. 0x1FC..0x1FF - the VA itself (0x5FC) is NOT the slot index
       (first draft wrote out of the 512-entry PT with 0x5FC+). */
    for (u64 spg = 0; spg < USER_STACK_SZ / 4096; ++spg) {
        u64 pa = pmm_alloc();
        if (!pa) return 0;
        frames[nf++] = pa;
        zero_frame(pa);
        ptv[0x1FC + spg] = pa | 0x07;
    }

    *nframes = nf;

    serial_puts("proc: space built cr3=");
    serial_puts_hex(pml4c);
    serial_puts(" frames=");
    serial_puts_hex(nf);
    serial_puts("\n");

    return pml4c;
}

void proc_space_destroy(const u64 *frames, int nframes)
{
    for (int i = 0; i < nframes; ++i)
        pmm_free(frames[i]);
    serial_puts("proc: space freed frames=");
    serial_puts_hex(nframes);
    serial_puts("\n");
}

void proc_user_entry(void)
{
    /*
     * Assumptions (scheduler contract): IF is OFF, CR3 is this
     * thread's space, and TSS rsp0 points at this thread's kernel
     * stack. We load the CPL3 data selectors (legal from CPL0: data
     * segments with DPL3 may be loaded), then iretq to userland.
     * rflags 0x202: IF set, IOPL 0 - userland gets preempted by the
     * PIT exactly like kernel threads do.
     */
    __asm__ __volatile__(
        "cli\n\t"                                   /* paranoia        */
        "movw $0x3B, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "xorl  %%ecx, %%ecx\n\t"
        "xorl  %%edx, %%edx\n\t"
        "xorl  %%esi, %%esi\n\t"
        "xorl  %%edi, %%edi\n\t"
        "xorl  %%r8d, %%r8d\n\t"
        "xorl  %%r9d, %%r9d\n\t"
        "xorl  %%r10d, %%r10d\n\t"
        "xorl  %%r11d, %%r11d\n\t"
        "xorl  %%r12d, %%r12d\n\t"
        "xorl  %%r13d, %%r13d\n\t"
        "xorl  %%r14d, %%r14d\n\t"
        "xorl  %%r15d, %%r15d\n\t"
        "xorl  %%eax, %%eax\n\t"
        "movl  $0x600000, %%esp\n\t"
        "pushq $0x3B\n\t"                           /* ss              */
        "pushq $0x600000\n\t"                       /* rsp  (grows dn) */
        "pushq $0x202\n\t"                          /* rflags: IF      */
        "pushq $0x33\n\t"                           /* cs: user 64-bit */
        "pushq $0x400000\n\t"                       /* rip: entry      */
        "iretq\n\t"
        :
        :
        : "rax", "rbx", "rcx", "rdx", "rsi", "rdi",
          "r8", "r9", "r10", "r11", "memory");

    for (;;)
        __asm__ volatile("hlt");          /* the compiler, not us     */
}
