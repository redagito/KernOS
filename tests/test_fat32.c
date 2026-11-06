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
struct fail_ctx { struct block_dev *dev; u64 data_start; int fail_data; };
static int fail_read(void *ctx, u64 lba, void *buf, u32 n)
{ return blk_read(((struct fail_ctx *)ctx)->dev, lba, buf, n); }
static int fail_write(void *ctx, u64 lba, const void *buf, u32 n)
{
    struct fail_ctx *f = ctx;
    if (f->fail_data && lba >= f->data_start) { f->fail_data = 0; return -1; }
    return blk_write(f->dev, lba, buf, n);
}
static u64 fail_sectors(void *ctx) { return blk_sectors(((struct fail_ctx *)ctx)->dev); }

static void check(int cond, const char *title)
{
    ++t_no;
    printf("%2d. %-52s %s\n", t_no, title, cond ? "PASS" : "FAIL");
    if (!cond)
        ++n_fail;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <fixture-image> <u1.elf> <shell.elf>\n",
                argv ? argv[0] : "test_fat32");
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

        /* M6.1 bind: the fixture must carry the ELFs THIS build just
           linked - same shape the boot read them at runtime. A
           fixture planted from an earlier build's u1/shell used to
           fake a green typed gate (exit 0 without the phase ever
           existing); comparing through the module itself catches
           the split on host, before any QEMU. */
        static u8 host_elf[16384];
        for (int pair = 0; pair <= 1; ++pair) {
            const char *name = pair ? BENCH_SHELL_FILE : BENCH_U1_FILE;
            const char *path = pair ? argv[3] : argv[2];
            FILE *f = fopen(path, "rb");
            if (!f) {
                check(0, "opening the built ELF (stale build tree?)");
                continue;
            }
            size_t hlen = fread(host_elf, 1, sizeof host_elf, f);
            int bad = ferror(f);
            fclose(f);

            /* read through the module again: independent of the
               lookups above, bound to the bytes just linked */
            int got = fat_read_file(name, pbuf, sizeof pbuf);
            check(!bad && got == (int)hlen && got > 0 &&
                      memcmp(pbuf, host_elf, hlen) == 0,
                  pair ? "SHELL.ELF embed == shell.elf byte-for-byte"
                       : "U1.ELF embed == u1.elf byte-for-byte");
        }

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

    /* ---------- create/replace/truncate with durable FAT metadata ------ */
    {
        static u8 data[2600], saved[3000];
        for (u32 i = 0; i < sizeof data; ++i)
            data[i] = (u8)((i * 29u + 7u) & 0xFF);
        check(fat_save_file("SAVETEST.BIN", data, 1300) == 1300,
              "save creates a multi-cluster root file");
        check(fat_file_size("SAVETEST.BIN") == 1300,
              "new file size is recorded");
        int got = fat_read_file("SAVETEST.BIN", saved, sizeof saved);
        check(got == 1300 && memcmp(saved, data, 1300) == 0,
              "new file data reads back byte-exact");

        /* Verify the allocated cluster's low 28-bit entry is mirrored. */
        {
            u64 root_lba = (u64)fi.data_start +
                           (u64)(fi.root_clus - 2) * fi.spc;
            u32 first = 0;
            int found = 0;
            for (u32 s = 0; s < fi.spc && !found; ++s) {
                u8 sector[512];
                if (blk_read(b, root_lba + s, sector, 1) != 0) break;
                for (u32 e = 0; e < 16; ++e) {
                    u8 *en = sector + e * 32;
                    if (en[0] == 0) break;
                    if (memcmp(en, "SAVETESTBIN", 11) == 0) {
                        first = (u32)en[0x1A] | ((u32)en[0x1B] << 8) |
                                ((u32)en[0x14] << 16) | ((u32)en[0x15] << 24);
                        found = 1;
                        break;
                    }
                }
            }
            u8 fa[512], fb[512];
            int ok = found && first >= 2 &&
                blk_read(b, fi.reserved + (first >> 7), fa, 1) == 0 &&
                blk_read(b, fi.reserved + fi.fat_secs + (first >> 7), fb, 1) == 0;
            if (ok) {
                u32 off = (first & 127) * 4;
                u32 va = (u32)fa[off] | ((u32)fa[off+1] << 8) |
                         ((u32)fa[off+2] << 16) | ((u32)fa[off+3] << 24);
                u32 vb = (u32)fb[off] | ((u32)fb[off+1] << 8) |
                         ((u32)fb[off+2] << 16) | ((u32)fb[off+3] << 24);
                ok = (va & 0x0FFFFFFFu) == (vb & 0x0FFFFFFFu) &&
                     (va & 0x0FFFFFFFu) >= 2 &&
                     (va & 0x0FFFFFFFu) < fi.total_clusters + 2;
            }
            check(ok, "new allocation is written to both FAT copies");
        }

        check(fat_save_file("SAVETEST.BIN", data, 2600) == 2600,
              "replacement grows the file");
        check(fat_file_size("SAVETEST.BIN") == 2600,
              "grown size is recorded");
        check(fat_save_file("SAVETEST.BIN", data, 17) == 17,
              "replacement truncates the file");
        check(fat_file_size("SAVETEST.BIN") == 17,
              "truncated size is recorded");
        check(fat_read_file("SAVETEST.BIN", saved, sizeof saved) == 17 &&
              memcmp(saved, data, 17) == 0,
              "truncated file content is byte-exact");
        check(fat_save_file("SAVETEST.BIN", 0, 0) == 0,
              "replacement accepts an empty file");
        check(fat_file_size("SAVETEST.BIN") == 0 &&
              fat_read_file("SAVETEST.BIN", saved, sizeof saved) == 0,
              "empty file persists with size zero");

        check(fat_save_file("TOOLONGNAME.TXT", data, 1) == -1 &&
              fat_save_file("BAD.NAME.TXT", data, 1) == -1 &&
              fat_save_file("A.", data, 1) == -1 &&
              fat_save_file(".TXT", data, 1) == -1,
              "invalid 8.3 names are rejected without truncation");
        check(fat_save_file("SAVETEST.BIN", 0, 1) == -1,
              "nonempty save requires a source buffer");

        check(fat_mount(b) == 0, "remount after saves succeeds");
        check(fat_file_size("SAVETEST.BIN") == 0 &&
              fat_read_file("SAVETEST.BIN", saved, sizeof saved) == 0,
              "empty file survives remount");

        /* The compiler and header set exceed one root-directory cluster. */
        for (int i = 0; i < 24; ++i) {
            char name[13];
            snprintf(name, sizeof name, "ROOT%02d.TXT", i);
            if (fat_save_file(name, data, 17) != 17) { ++n_fail; break; }
        }
        check(fat_mount(b) == 0 && fat_read_file("ROOT23.TXT", saved, sizeof saved) == 17 &&
              !memcmp(saved, data, 17), "root directory grows and survives remount");
        check(fat_save_file("KEEP.C", "original", 8) == 8, "prepare source for failed-save test");
        struct fail_ctx fc = {b, fi.data_start, 1};
        static const struct block_ops fail_ops = {fail_read, fail_write, fail_sectors};
        struct block_dev failing = {&fail_ops, &fc};
        check(fat_mount(&failing) == 0 && fat_save_file("KEEP.C", data, 2600) == -1,
              "staged data-write failure is reported");
        check(fat_mount(b) == 0 && fat_read_file("KEEP.C", saved, sizeof saved) == 8 &&
              !memcmp(saved, "original", 8), "failed save preserves original file after remount");
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
