#include <kernel/types.h>
#include <kernel/elf64.h>
#include "proc.h"
#include "pmm.h"
#include "serial.h"
#include "console.h"

/*
 * M5.3 process model, see proc.h for the contract.
 *
 * Everything walks the boot hierarchy through the identity map
 * (PA == VA below 1 GiB): a P word IS its target's address. User PTEs
 * are P|U|W = 0x07. The span comes from the ELF plan, not a fixed
 * blob: the page range covering every PT_LOAD segment, each page
 * allocated + zeroed, then segmented file bytes copied in (the bss
 * tail of memsz > filesz is left at zero by construction).
 */

static u64 cr3_read(void)
{
    u64 v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static void zero_frame(u64 pa)
{
    volatile u64 *p = (volatile u64 *)pa;
    for (u32 i = 0; i < 4096 / sizeof(u64); ++i)
        p[i] = 0;
}

void proc_space_destroy(const u64 *frames, int nframes)
{
    for (int i = 0; i < nframes; ++i)
        pmm_free(frames[i]);
    console_puts("proc: space freed frames=");
    console_puts_hex(nframes);
    console_puts("\n");
}

/* Copy one segment's file bytes into the allocated span pages. */
static void copy_segment(const u8 *file_buf, const struct elf_seg *s,
                         u64 span_lo, const u64 *pages)
{
    u64 copied = 0;
    while (copied < s->filesz) {
        u64 va    = s->vaddr + copied;
        u64 page  = va & ~0xFFFULL;
        u64 off   = va & 0xFFF;
        u64 chunk = 0x1000 - off;
        if (chunk > s->filesz - copied)
            chunk = s->filesz - copied;

        u64 dst = pages[(page - span_lo) / 0x1000] + off;
        const u8 *src = file_buf + s->foff + copied;
        for (u64 i = 0; i < chunk; ++i)
            ((volatile u8 *)dst)[i] = src[i];
        copied += chunk;
    }
}

/* Give the ledger pages back on a failed build (M5.10: refusals
   leave NOTHING left behind - docs/syscall-abi.md makes that the
   contract; the old code committed *nframes only on success, so an
   exhausted pmm mid-build silently leaked every allocated frame). */
static void frames_release(u64 *frames, int nf)
{
    while (nf > 0)
        pmm_free(frames[--nf]);
}

u64 proc_build_space_elf(const void *elf, u64 elf_len,
                         u64 *frames, int *nframes, u64 *entry_out)
{
    struct elf_seg segs[ELF_MAX_SEGS];
    int nsegs = 0;
    u64 entry;

    int rc = elf64_plan(elf, elf_len, &entry, segs, &nsegs);
    if (rc < 0) {
        console_puts("proc: elf refused - ");
        console_puts(elf64_errname(rc));
        console_puts("\n");
        return 0;
    }

    /* window check: every segment must fit below the user stack, and
       the entry must point into the observed span (executable bytes). */
    u64 span_lo = (u64)-1, span_hi = 0;
    for (int i = 0; i < nsegs; ++i) {
        if (segs[i].vaddr < USER_WINDOW_LO ||
            segs[i].vaddr + segs[i].memsz > USER_STACK_VA - USER_STACK_SZ)
            goto refuse_window;
        if (segs[i].vaddr < span_lo)
            span_lo = segs[i].vaddr;
        if (segs[i].vaddr + segs[i].memsz > span_hi)
            span_hi = segs[i].vaddr + segs[i].memsz;
    }
    if (entry < span_lo || entry >= span_hi)
        goto refuse_window;

    span_lo &= ~0xFFFULL;
    u64 span_pages = (span_hi - span_lo + 0xFFF) / 0x1000;
    if (span_pages > USER_MAX_PAGES) {
        console_puts("proc: elf refused - span ");
        console_puts_hex(span_pages);
        console_puts(" pages exceeds budget\n");
        return 0;
    }

    /* ---- boot-table clones, patched for the user window ---- */
    int nf = 0;
    u64 boot_cr3 = cr3_read() & ~0xFFFULL;
    u64 *boot_pml4 = (u64 *)boot_cr3;
    u64 *boot_pdpt = (u64 *)(boot_pml4[0] & ~0xFFFULL);
    u64 *boot_pd   = (u64 *)(boot_pdpt[0] & ~0xFFFULL);

    /* Clones: fresh, then patched. Every intermediate entry on the
       user path carries P|U|W so CPL3 can descend; supervisor-only
       leaves under U parents stay equally uncontaminated. */
    u64 pml4c = pmm_alloc();
    if (!pml4c) return 0;
    frames[nf++] = pml4c;
    zero_frame(pml4c);
    u64 *pml4v = (u64 *)pml4c;
    for (int i = 0; i < 512; ++i)
        pml4v[i] = boot_pml4[i];          /* share the whole boot map  */

    u64 pdptc = pmm_alloc();
    if (!pdptc) { frames_release(frames, nf); return 0; }
    frames[nf++] = pdptc;
    zero_frame(pdptc);
    u64 *pdptv = (u64 *)pdptc;
    for (int i = 1; i < 512; ++i)
        pdptv[i] = boot_pdpt[i];

    u64 pdc = pmm_alloc();
    if (!pdc) { frames_release(frames, nf); return 0; }
    frames[nf++] = pdc;
    zero_frame(pdc);
    u64 *pdv = (u64 *)pdc;
    for (int i = 0; i < 512; ++i)
        pdv[i] = boot_pd[i];

    u64 pt = pmm_alloc();
    if (!pt) { frames_release(frames, nf); return 0; }
    frames[nf++] = pt;
    zero_frame(pt);
    u64 *ptv = (u64 *)pt;

    pdptv[0] = pdc | 0x07;
    pdv[2]   = pt  | 0x07;
    pml4v[0] = pdptc | 0x07;

    /* ---- span pages: zero-allocated, mapped, then bytes copied ---- */
    for (u64 p = 0; p < span_pages; ++p) {
        u64 pa = pmm_alloc();
        if (!pa) { frames_release(frames, nf); return 0; }
        frames[nf++] = pa;
        zero_frame(pa);
        ptv[((span_lo >> 12) + p) & 0x1FF] = pa | 0x07;
    }
    for (int i = 0; i < nsegs; ++i)
        copy_segment(elf, &segs[i], span_lo, frames + 4);

    /* Stack PT slots derive from its size: a 64 KiB stack occupies
       slots 0x1F0..0x1FF, independently of the program span. */
    for (u64 spg = 0; spg < USER_STACK_SZ / 4096; ++spg) {
        u64 pa = pmm_alloc();
        if (!pa) { frames_release(frames, nf); return 0; }
        frames[nf++] = pa;
        zero_frame(pa);
        ptv[((USER_STACK_VA - USER_STACK_SZ) >> 12 & 0x1FF) + spg] = pa | 0x07;
    }

    console_puts("proc: space built elf cr3=");
    console_puts_hex(pml4c);
    console_puts(" frames=");
    console_puts_hex(nf);
    console_puts(" entry=");
    console_puts_hex(entry);
    console_puts("\n");

    *nframes  = nf;
    *entry_out = entry;
    return pml4c;

refuse_window:
    console_puts("proc: elf refused - outside the user window\n");
    return 0;
}

void proc_user_entry(u64 entry)
{
    /*
     * Assumptions (scheduler contract): IF is OFF, CR3 is this
     * thread's space, and TSS rsp0 points at this thread's kernel
     * stack. We load the CPL3 data selectors (legal from CPL0: data
     * segments with DPL3 may be loaded), then iretq to userland.
     * rflags 0x202: IF set, IOPL 0 - userland gets preempted by the
     * PIT exactly like kernel threads do. The entry point arrives in
     * RDI and rides through untouched until the final push (the rip
     * slot); everything else is wiped from the same cold start.
     */
    register u64 ent __asm__("rdi") = entry;   /* explicit ABI binding  */
    (void)ent;                                 /* consumed via rdi below */

    __asm__ __volatile__(
        "cli\n\t"                                   /* paranoia        */
        "movw $0x3B, %%dx\n\t"
        "movw %%dx, %%ds\n\t"
        "movw %%dx, %%es\n\t"
        "movw %%dx, %%fs\n\t"
        "movw %%dx, %%gs\n\t"
        "xorl  %%eax, %%eax\n\t"
        "xorl  %%ecx, %%ecx\n\t"
        "xorl  %%edx, %%edx\n\t"
        "xorl  %%esi, %%esi\n\t"
        "xorl  %%r8d, %%r8d\n\t"
        "xorl  %%r9d, %%r9d\n\t"
        "xorl  %%r10d, %%r10d\n\t"
        "xorl  %%r11d, %%r11d\n\t"
        "xorl  %%r12d, %%r12d\n\t"
        "xorl  %%r13d, %%r13d\n\t"
        "xorl  %%r14d, %%r14d\n\t"
        "xorl  %%r15d, %%r15d\n\t"
        "movl  $0x600000, %%esp\n\t"
        "pushq $0x3B\n\t"                           /* ss              */
        "pushq $0x5ffff8\n\t"                       /* SysV C entry: rsp mod 16 == 8 */
        "pushq $0x202\n\t"                          /* rflags: IF      */
        "pushq $0x33\n\t"                           /* cs: user 64-bit */
        "pushq %%rdi\n\t"                           /* rip: entry      */
        "iretq\n\t"
        :
        : "r"(ent)
        /* no clobber list: this never returns, and RDI (declared in
           the input row) is consumed here by the final push. GCC's
           constraint engine refuses input+clobber for the same reg. */
        : "rax", "rbx", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11",
          "memory");

    for (;;)
        __asm__ volatile("hlt");          /* the compiler, not us     */
}
