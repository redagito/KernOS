/*
 * test_fat32.c — host unit tests for the portable FAT32 module.
 *
 * Same C sources the kernel boots with (kernel/fat32.c) run against the
 * SAME fixture constants (include/kernel/bench.h) that tools/mkfat32
 * plants and the kernel's own boot gates verify. The boot run proves the
 * module works on real AHCI; this suite proves its parsing, its chain
 * walking and its error paths in isolation, at 60 iterations a minute.
 *
 * Exit code 0 means the whole suite passed.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <kernel/types.h>
#include <kernel/bench.h>
#include "blk_host.h"
#include "fat32.h"      /* via -Ikernel */

static int t_no = 0, n_fail = 0;

static void check(int cond, const char *title)
{
    ++t_no;
    printf("%2d. %-52s %s\n", t_no, title, cond ? "PASS" : "FAIL");
    if (!cond)
        ++n_fail;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <fixture-image>\n", argv ? argv[0] : "test_fat32");
        return 2;
    }

    struct block_dev *b = blk_host_open(argv[1]);
    if (b == 0) {
        perror(argv[1]);
        return 2;
    }

    check(blk_sectors(b) == BENCH_DISK_SECTORS, "device size matches fixture spec");

    /* ---------- mount ---------- */
    int rc = fat_mount(b);
    check(rc == 0, "fat_mount OK");

    struct fat_info fi;
    fat_stats(&fi);
    check(fi.spc == 1, "spc=1");
    check(fi.root_clus == 2, "root directory is cluster 2");
    check(fi.nfats == 2, "two FAT copies");
    check(fi.total_clusters > 65524, "cluster count is in the FAT32 range");
    check(fi.total_sectors == BENCH_DISK_SECTORS, "boot-sector-derived geometry");

    /* ---------- reads ---------- */
    static u8 buf[4096];

    int size = fat_read_file(BENCH_FILE, buf, sizeof buf);
    printf("   KERNOS.TXT size=%d (want %d)\n", size,
           (int)(sizeof(BENCH_SEED) - 1));
    check(size == (int)(sizeof(BENCH_SEED) - 1), "KERNOS.TXT size matches planter");
    check(size <= 0 || memcmp(buf, BENCH_SEED, (size_t)size) == 0,
          "KERNOS.TXT content is byte-exact");

    /* LFN entries decorate this file; M4.3 addresses it by its SHORT
       alias only (LFN alias resolution is a later milestone). */
    int lsize = fat_read_file("VERYLO~1.TXT", buf, sizeof buf);
    printf("   VERYLO~1.TXT size=%d (want %d)\n", lsize,
           (int)(sizeof(BENCH_LONG_BODY) - 1));
    check(lsize == (int)(sizeof(BENCH_LONG_BODY) - 1),
          "LFN-backed file resolves via its short name");
    check(lsize <= 0 || memcmp(buf, BENCH_LONG_BODY, (size_t)lsize) == 0,
          "LFN file content is byte-exact");
    check(fat_read_file(BENCH_LONG_FILE, buf, sizeof buf) == -1,
          "LFN long-name alias refused (M4.3: 8.3-only lookup)");

    int csize = fat_read_file("kernos.txt", buf, sizeof buf);
    check(csize == size &&
              (size <= 0 || memcmp(buf, BENCH_SEED, (size_t)size) == 0),
          "lookup is case-insensitive (content compared)");

    check(fat_read_file(BENCH_FILE, buf, 4) == -1, "cap too small is refused");

    /* ---------- the planted programs resolve through 8.3 semantics ---- */
    /* M5.3/M5.5: programs ride the fixture (U1.ELF, then SHELL.ELF).
       The lookups here exercise the module's own shortname() form
       INDEPENDENT of the planter's self-checks (a hand-spaced literal
       typo in mkfat32 would agree with itself; the kernel-side 8.3
       semantics would not). Both must read ELF magic. */
    static u8 pbuf[16384];               /* programs exceed 4 KiB now   */
    {
        int usz = fat_read_file(BENCH_U1_FILE, pbuf, sizeof pbuf);
        printf("   %s size=%d\n", BENCH_U1_FILE, usz);
        check(usz > 64, "U1.ELF resolves and is at least one ELF header");
        check(usz <= 0 || (pbuf[0] == 0x7F && pbuf[1] == 'E' &&
                           pbuf[2] == 'L' && pbuf[3] == 'F'),
              "U1.ELF content starts with ELF magic");

        int ssz = fat_read_file(BENCH_SHELL_FILE, pbuf, sizeof pbuf);
        printf("   %s size=%d\n", BENCH_SHELL_FILE, ssz);
        check(ssz > 64, "SHELL.ELF resolves and is at least one ELF header");
        check(ssz <= 0 || (pbuf[0] == 0x7F && pbuf[1] == 'E' &&
                           pbuf[2] == 'L' && pbuf[3] == 'F'),
              "SHELL.ELF content starts with ELF magic");

        /* the lowercase lower-8.3 route the shell's spawn calls take */
        check(fat_read_file(BENCH_U1_FILE + 0, pbuf, sizeof pbuf) == usz &&
              fat_read_file("u1.elf", pbuf, sizeof pbuf) == usz,
              "program lookup is case-insensitive too");
    }

    /* ---------- the walk must have skipped what comes first ---------- */
    /* (implicit: BENCH files are entered AFTER the LFN pair and the
       deleted decoy, so every successful read above already covered it) */

    /* ---------- write ---------- */
    {
        u32 wlen = sizeof(BENCH_WRITE) - 1;
        static u8 wb[4096];

        if (size <= 0) {
            printf("   write checks skipped: KERNOS.TXT unreadable\n");
        } else {
            u32 size_u = (u32)size;

            for (u32 i = 0; i < size_u; ++i)
                wb[i] = (i < wlen) ? (u8)BENCH_WRITE[i] : (u8)' ';

            check(fat_write_file(BENCH_FILE, wb, size_u) == (int)size_u,
                  "same-size write accepted");
            int rs = fat_read_file(BENCH_FILE, buf, sizeof buf);
            check(rs == (int)size_u && memcmp(buf, wb, (size_t)size_u) == 0,
                  "write-back readback is byte-exact");
            check(fat_write_file(BENCH_FILE, wb, size_u + 1) == -1,
                  "write extension is refused (len > size)");
            check(fat_write_file("NOPE.TXT", wb, 4) == -1,
                  "write missing file refused");
        }
    }

    /* ---------- absent mount / bad image ---------- */
    {
        struct block_dev *bad = blk_host_open("/dev/null");
        check(bad != 0, "empty device opens");
        if (bad) {
            int mrc = fat_mount(bad);
            check(mrc == -1, "mount of empty device refused");
        }
    }
    {
        /* zeroed BPB: the most common on-media garbage */
        FILE *f = fopen("build/testbad.img.tmp", "wb");
        if (f) {
            static u8 zero[512];
            fwrite(zero, 1, 512, f);
            fclose(f);
        }
        struct block_dev *bad = blk_host_open("build/testbad.img.tmp");
        check(bad != 0, "bad image backend opens");
        if (bad) {
            check(fat_mount(bad) == -1, "zero BPB mount refused");
            /* the working mount must survive a failed re-mount attempt */
            check(fat_read_file(BENCH_FILE, buf, sizeof buf) > 0,
                  "failed re-mount keeps the prior device");
            remove("build/testbad.img.tmp");
        }
    }

    printf("--\n%d checks, %d failures\n", t_no, n_fail);
    return n_fail ? 1 : 0;
}
