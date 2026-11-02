/*
 * test_elf64.c — host unit tests for the portable ELF64 plan module.
 *
 * Same C source the kernel boots with (kernel/elf64.c). Two test
 * groups:
 *   - synthetic images built byte-by-byte in this file: every refusal
 *     class of elf64_plan must fire on the corruption it was built
 *     for, and a minimal honest image must parse.
 *   - the real U1.ELF that mkfat32 plants in the fixture (argv[1]):
 *     the loader plans it and every field is checked to be sane —
 *     entry inside the first segment, segments sized legally.
 *
 * Exit code 0 means the whole suite passed.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <kernel/types.h>
#include <kernel/elf64.h>

static int t_no = 0, n_fail = 0;

static void check(int cond, const char *title)
{
    ++t_no;
    printf("%2d. %-52s %s\n", t_no, title, cond ? "PASS" : "FAIL");
    if (!cond)
        ++n_fail;
}

/* --- synthetic image builder: one flat table with helpers ------------- */
static u8 img[512];
static u64 img_len;

static void w16(u32 o, u16 v) { img[o] = v & 0xFF; img[o+1] = v >> 8; }
static void w32(u32 o, u32 v)
{
    img[o] = v & 0xFF; img[o+1] = (v >> 8) & 0xFF;
    img[o+2] = (v >> 16) & 0xFF; img[o+3] = v >> 24;
}
static void w64(u32 o, u64 v)
{
    w32(o, (u32)v); w32(o + 4, (u32)(v >> 32));
}

static void base_image(void)
{
    memset(img, 0, sizeof img);
    img_len = 512;
    memcpy(img, "\x7F""ELF\x02\x01\x01\x00", 8);
    w16(0x10, 2);                     /* ET_EXEC        */
    w16(0x12, 62);                    /* EM_X86_64      */
    w64(0x18, 0x400000);              /* e_entry        */
    w64(0x20, 64);                    /* e_phoff        */
    w16(0x36, 56);                    /* e_phentsize    */
    w16(0x38, 1);                     /* e_phnum = 1    */
    /* PT_LOAD #0: x86-64 bss-aligned params */
    w32(64 + 0x00, 1);                /* p_type PT_LOAD */
    w32(64 + 0x04, 7);                /* p_flags        */
    w64(64 + 0x08, 64);               /* p_offset       */
    w64(64 + 0x10, 0x400000);         /* p_vaddr        */
    w64(64 + 0x18, 0);                /* p_paddr        */
    w64(64 + 0x20, 512 - 64);         /* p_filesz (rest of image)   */
    w64(64 + 0x28, 512 - 64);         /* p_memsz                    */
    w64(64 + 0x30, 4096);             /* p_align                    */
    /* fill the loadable bytes: recognizable tail. The phdr lives at
       64..119 - the payload starts behind it, or base_image would
       erase its own program header (found by this very suite). */
    memset(img + 64 + 56, 0xAA, 512 - (64 + 56));
}

static int plans_ok(u64 *entry, struct elf_seg *segs, int *nsegs)
{
    *entry = 0; *nsegs = 0;
    int rc = elf64_plan(img, img_len, entry, segs, nsegs);
    if (rc)
        printf("    [dbg] plan rc=%d err=%s\n", rc, elf64_errname(rc));
    return rc == 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <u1.elf>\n", argv ? argv[0] : "test_elf64");
        return 2;
    }

    /* ---------- synthetic: happy path ---------- */
    base_image();
    {
        u64 entry; struct elf_seg segs[ELF_MAX_SEGS]; int nsegs;
        check(plans_ok(&entry, segs, &nsegs), "minimal honest image plans");
        check(entry == 0x400000, "entry point carried through");
        check(nsegs == 1, "one PT_LOAD observed");
        check(segs[0].vaddr == 0x400000 && segs[0].filesz == 512 - 64,
              "segment fields decode faithfully");
        check(segs[0].foff == 64, "segment file offset decode");
    }

    /* ---------- synthetic: each refusal class ---------- */
    {
        u64 entry; struct elf_seg segs[ELF_MAX_SEGS]; int nsegs;
        check(elf64_plan(img, 32, &entry, segs, &nsegs) < 0,
              "refuses truncated header");

        base_image(); img[0] = 0;
        check(elf64_plan(img, img_len, &entry, segs, &nsegs) < 0,
              "refuses bad magic");

        base_image(); img[4] = 1;         /* 32-bit class */
        check(elf64_plan(img, img_len, &entry, segs, &nsegs) < 0,
              "refuses ELF32 class");

        base_image(); img[5] = 2;         /* big-endian */
        check(elf64_plan(img, img_len, &entry, segs, &nsegs) < 0,
              "refuses big-endian");

        base_image(); w16(0x10, 3);       /* ET_DYN */
        check(elf64_plan(img, img_len, &entry, segs, &nsegs) < 0,
              "refuses shared objects");

        base_image(); w16(0x12, 40);      /* EM_ARM   */
        check(elf64_plan(img, img_len, &entry, segs, &nsegs) < 0,
              "refuses foreign machine");

        base_image(); w16(0x36, 52);      /* wrong phentsize */
        check(elf64_plan(img, img_len, &entry, segs, &nsegs) < 0,
              "refuses bad phentsize");

        base_image(); w16(0x38, 99);      /* phnum beyond cap */
        check(elf64_plan(img, img_len, &entry, segs, &nsegs) < 0,
              "refuses phnum over ELF_MAX_SEGS");

        base_image(); w64(0x20, 400);     /* phoff + 1*56 > 512 */
        check(elf64_plan(img, img_len, &entry, segs, &nsegs) < 0,
              "refuses phdr past end of image");

        base_image(); w64(64 + 0x08, 400); w64(64 + 0x20, 200);
        check(elf64_plan(img, img_len, &entry, segs, &nsegs) < 0,
              "refuses segment filesz past image end");
    }
    {
        /* memsz < filesz */
        u64 entry; struct elf_seg segs[ELF_MAX_SEGS]; int nsegs;
        base_image();
        w64(64 + 0x28, (512 - 64) - 1);
        check(elf64_plan(img, img_len, &entry, segs, &nsegs) < 0,
              "refuses memsz < filesz");
    }
    {
        /* only non-LOAD phdrs */
        u64 entry; struct elf_seg segs[ELF_MAX_SEGS]; int nsegs;
        base_image();
        w32(64 + 0x00, 6);                /* PT_PHDR instead */
        check(elf64_plan(img, img_len, &entry, segs, &nsegs) < 0,
              "refuses no PT_LOAD at all");
    }

    /* ---------- the real thing: U1.ELF from the fixture build ---------- */
    {
        FILE *f = fopen(argv[1], "rb");
        check(f != 0, "u1.elf opens");
        if (f) {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            check(n > 64 && n < 16384, "u1.elf plausible size");
            static u8 buf[16384];
            if (n > 64 && n < 16384 && fread(buf, 1, (u32)n, f) == (u32)n) {
                u64 entry; struct elf_seg segs[ELF_MAX_SEGS]; int nsegs;
                memset(&segs, 0, sizeof segs);
                int rc = elf64_plan(buf, (u64)n, &entry, segs, &nsegs);
                check(rc == 0, "u1.elf plans");
                if (rc == 0) {
                    check(entry >= 0x400000 && entry < 0x600000,
                          "u1.elf entry inside the user window");
                    u64 lo = (u64)-1, hi = 0;
                    for (int i = 0; i < nsegs; ++i) {
                        check(segs[i].memsz >= segs[i].filesz,
                              "u1.elf segment memsz >= filesz");
                        if (segs[i].vaddr < lo) lo = segs[i].vaddr;
                        if (segs[i].vaddr + segs[i].memsz > hi)
                            hi = segs[i].vaddr + segs[i].memsz;
                    }
                    check(entry >= lo && entry < hi,
                          "u1.elf entry inside its own span");
                    check(hi <= 0x5FC000,
                          "u1.elf span below the user stack");
                    printf("    u1.elf: entry=0x%llx segs=%d span=0x%llx..0x%llx\n",
                           (unsigned long long)entry, nsegs,
                           (unsigned long long)lo, (unsigned long long)hi);
                }
            }
            fclose(f);
        }
    }

    printf("%d checks, %d failures\n", t_no, n_fail);
    return n_fail ? 1 : 0;
}
