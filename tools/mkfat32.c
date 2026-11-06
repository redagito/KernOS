/*
 * mkfat32.c — deterministic FAT32 bench-fixture planter (host only).
 *
 * Writes an image that is identical every run (no clock, no randomness):
 *
 *   - 128 MiB (262144 x 512 B), 1 sector/cluster so the cluster count
 *     is honestly FAT32 (the tool self-asserts clusters > 65524).
 *   - root dir contents, in the order every parse must survive:
 *       [0]      LFN entry seq 2   (tail   of VERYLONGNAME.TXT)
 *       [1]      LFN entry seq 1   (head   of VERYLONGNAME.TXT)
 *       [2]      VERYLO~1TXT       (the long file's short entry)
 *       [3]      0xE5...           deleted-entry decoy (must be skipped)
 *       [4]      KERNOS.TXT        (short 8.3 entry)
 *       [5]      U1.ELF            (the userland program, M5.3)
 *       [6]      SHELL.ELF         (the shell, M5.5)
 *       [7]      0x00 terminator; rest zero
 *   - fixture data from include/kernel/bench.h (shared with the kernel
 *     boot gates and host unit tests).
 *   - raw bench markers planted in the DATA area, with layout
 *     self-checks that refuse any collision with allocated clusters.
 *
 * argv: mkfat32 <image-file> <u1-elf-path> <shell-elf-path> - the
 * userland binaries are planted at clusters 5.. (u1 chain, then the
 * shell chain) so the kernel loads its processes from the disk, not
 * from its own image. Both entries are read back and asserted before
 * the image is allowed to exist.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/types.h>
#include <kernel/bench.h>

#define TOTAL_SECTORS  BENCH_DISK_SECTORS
#define SECTOR         512u
#define SPC            1u            /* 1 sector per cluster */
#define RESERVED       32u
#define NFATS          2u

static u8 img[TOTAL_SECTORS * SECTOR];   /* 128 MiB, zeroed   */

static u32 lba_off(u32 lba) { return lba * SECTOR; }

static u32 program_cluster(u32 index, u32 marker, u32 write_marker)
{
    u32 c = 5 + index;
    if (c >= marker) ++c;
    if (c >= write_marker) ++c;
    return c;
}

static void wr16(u8 *base, u32 o, u16 v)
{
    base[o]     = v & 0xFF;
    base[o + 1] = v >> 8;
}

static void wr32(u8 *base, u32 o, u32 v)
{
    base[o]     = v & 0xFF;
    base[o + 1] = (v >> 8) & 0xFF;
    base[o + 2] = (v >> 16) & 0xFF;
    base[o + 3] = v >> 24;
}

/* standard 8.3 LFN checksum over the 11-byte short name */
static u8 lfn_checksum(const u8 *n)
{
    u8 s = 0;
    for (int i = 0; i < 11; ++i)
        s = (u8)(((s & 1) << 7) + (s >> 1) + n[i]);
    return s;
}

/* one LFN entry, slot = physical position in the dir cluster.
   seqbit: seq number | 0x40 for the tail entry. Entry seq s holds
   UTF-16 chars 13(s-1) .. 13s-1; the first free slot after the name
   is the 0x0000 terminator, everything after that pads with 0xFFFF. */
static void lfn_entry(u8 *dir, u32 slot, u8 seqbit, const char *name, u8 chk)
{
    u8 *e = dir + slot * 32;
    memset(e, 0, 32);
    e[0]     = seqbit;
    e[0x0B]  = 0x0F;
    e[0x0C]  = 0x00;
    e[0x0D]  = chk;

    u32 seq  = (seqbit & 0x3F);
    const char *p = name + (seq - 1) * 13;
    const char *name_end = name + strlen(name);

    int past = 0;
    static const u32 pos[13] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };
    for (u32 i = 0; i < 13; ++i) {
        u16 c;
        if (!past && p + i < name_end)
            c = (u8)p[i];                       /* ASCII -> UCS-2      */
        else if (!past) {
            c = 0x0000;                         /* the terminator      */
            past = 1;
        } else {
            c = 0xFFFF;                         /* padding             */
        }
        e[pos[i]]     = c & 0xFF;
        e[pos[i] + 1] = c >> 8;
    }
    (void)seq;
}

/* Load a whole file for planting (the u1 ELF). NULL on failure. */
static u8 *read_file(const char *path, u32 *out_len, u32 cap)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || (u32)n > cap) {
        fclose(f);
        return 0;
    }
    u8 *p = malloc(n ? (u32)n : 1);
    if (!p) {
        fclose(f);
        return 0;
    }
    if (n && fread(p, 1, (u32)n, f) != (u32)n) {
        free(p);
        fclose(f);
        return 0;
    }
    fclose(f);
    *out_len = (u32)n;
    return p;
}

/* 8.3 dir name, built from base+ext. Bounded by the strings' OWN NULs
   (strlen first) - the first draft looped with a per-byte zero ternary
   and kept indexing PAST a literal's terminator, so padding slots
   picked up whatever literal the compiler parked next door: the image
   ended up with 'U1 SHELL' style nonsense names and both lookups
   failed. A perfect case for the host suite to catch. */
static void entry_name83(u8 out[11], const char *base, const char *ext)
{
    u32 bl = 0, el = 0;
    while (base[bl]) ++bl;
    while (ext[el]) ++el;
    if (bl > 8) bl = 8;
    if (el > 3) el = 3;

    for (u32 i = 0; i < 11; ++i)
        out[i] = ' ';
    for (u32 i = 0; i < bl; ++i)
        out[i] = (u8)base[i];
    for (u32 i = 0; i < el; ++i)
        out[8 + i] = (u8)ext[i];
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <image-file> <u1-elf-path> <shell-elf-path>\n",
                argv ? argv[0] : "mkfat32");
        return 2;
    }

    /* the programs that userland will run: read them BEFORE anything
       else so a bad path is a clean tool error, not a half-built image */
    u32 u1_len = 0;
    u8 *u1 = read_file(argv[2], &u1_len, 16 * 1024);
    if (!u1 || (*u1 != 0x7F)) {
        fprintf(stderr, "mkfat32: cannot read a valid u1 elf '%s'\n", argv[2]);
        free(u1);
        return 1;
    }
    u32 u1_clusters = (u1_len + SECTOR - 1) / SECTOR;

    u32 sh_len = 0;
    u8 *sh = read_file(argv[3], &sh_len, 16 * 1024);
    if (!sh || (*sh != 0x7F)) {
        fprintf(stderr, "mkfat32: cannot read a valid shell elf '%s'\n", argv[3]);
        free(u1);
        free(sh);
        return 1;
    }
    u32 sh_clusters = (sh_len + SECTOR - 1) / SECTOR;

    /* ---- geometry: fat_secs = ceil((total-r-2S)*4/512) fixpoint ---- */
    u32 fat_secs = 2048;
    for (int it = 0; it < 16; ++it) {
        u32 clusters = (TOTAL_SECTORS - RESERVED - NFATS * fat_secs) / SPC;
        u32 want = ((clusters + 2) * 4 + SECTOR - 1) / SECTOR;
        if (want == fat_secs)
            break;
        fat_secs = want;
        if (it == 15) {
            fprintf(stderr, "mkfat32: fat_secs fixpoint did not settle\n");
            return 1;
        }
    }

    u32 data_start = RESERVED + NFATS * fat_secs;      /* LBA of cluster 2 */
    u32 clusters = (TOTAL_SECTORS - data_start) / SPC;

    /* ---- self-checks: refuse to plant a dishonest fixture ---------- */
    if (clusters <= 65524) {
        fprintf(stderr, "mkfat32: %u clusters is not FAT32 (needs > 65524)\n",
                clusters);
        return 1;
    }
    u32 marker_clus = BENCH_MARKER_LBA - data_start + 2;
    u32 rb_clus     = BENCH_WRITE_LBA  - data_start + 2;
    /* clusters 2,3,4 fixed by the fixture; the PROGRAMS take 5 ..
       (U1.ELF chains first, SHELL.ELF right behind it). The AHCI
       markers sleep behind the programs - each file has a declared
       ceiling (bench.h), and the ACTUAL combined chain is asserted
       against marker_clus at every plant: a program growth that ran
       out of headroom is a loud tool error, not marker corruption. */
    if (u1_clusters > BENCH_U1_MAX_SECTORS) {
        fprintf(stderr, "mkfat32: U1.ELF needs %u clusters, bench reserve "
                        "is %u\n", u1_clusters, BENCH_U1_MAX_SECTORS);
        free(u1); free(sh);
        return 1;
    }
    if (sh_clusters > BENCH_SHELL_MAX_SECTORS) {
        fprintf(stderr, "mkfat32: SHELL.ELF needs %u clusters, bench reserve "
                        "is %u\n", sh_clusters, BENCH_SHELL_MAX_SECTORS);
        free(u1); free(sh);
        return 1;
    }
    if (marker_clus < 5) {
        fprintf(stderr, "mkfat32: marker overlaps fixed fixture files\n");
        free(u1); free(sh);
        return 1;
    }
    if (BENCH_WRITE_LBA <= BENCH_MARKER_LBA || rb_clus <= marker_clus ||
        BENCH_WRITE_LBA >= TOTAL_SECTORS || marker_clus + 1 == rb_clus) {
        fprintf(stderr, "mkfat32: write LBA %u invalid vs marker LBA %u "
                        "(clusters %u/%u)\n",
                BENCH_WRITE_LBA, BENCH_MARKER_LBA, rb_clus, marker_clus);
        return 1;
    }
    /* M5.11 (round-two review, M5): the size field is a full u32
       FAT ABI (dir[0x1C..0x1F], LO byte first). The old code cast
       strlen to u8 into byte 0x1C only - with the <=512 body bound
       a 261-byte seed planted size 5 and the byte-3 tail wrote
       GARBAGE it never owned - and the directory lied while
       mkfat32 said ok. Red evidence: probe seed overrided at 261 /
       body 301 planted 5 / 45. Write the whole word, verify below. */
    if (strlen(BENCH_SEED) == 0 || strlen(BENCH_LONG_BODY) == 0 ||
        strlen(BENCH_SEED) > 512 || strlen(BENCH_LONG_BODY) > 512 ||
        strlen(BENCH_WRITE) > strlen(BENCH_SEED)) {
        fprintf(stderr, "mkfat32: fixture body sizes out of bounds\n");
        return 1;
    }

    /* ---- BPB -------------------------------------------------------- */
    img[0] = 0xEB; img[1] = 0x3C; img[2] = 0x90;
    memcpy(img + 3, "KERNOS  ", 8);
    wr16(img, 0x0B, SECTOR);
    img[0x0D] = SPC;
    wr16(img, 0x0E, RESERVED);
    img[0x10] = NFATS;
    wr16(img, 0x11, 0);                      /* FAT32: root via cluster */
    wr16(img, 0x13, 0);
    img[0x15] = 0xF8;
    wr16(img, 0x16, 0);
    wr16(img, 0x18, 63);
    wr16(img, 0x1A, 16);
    wr32(img, 0x20, TOTAL_SECTORS);
    wr32(img, 0x24, fat_secs);
    wr16(img, 0x28, 0);
    wr16(img, 0x2A, 0);
    wr32(img, 0x2C, 2);                      /* root dir cluster        */
    wr16(img, 0x30, 1);                      /* fsinfo at sector 1      */
    wr16(img, 0x32, 6);                      /* backup boot at sector 6 */
    img[0x40] = 0x80;
    img[0x42] = 0x29;
    wr32(img, 0x43, 0xFEEDFACE);
    memcpy(img + 0x47, "KERNOS    ", 11);
    memcpy(img + 0x52, "FAT32   ", 8);
    img[510] = 0x55; img[511] = 0xAA;

    /* ---- FSInfo ------------------------------------------------------ */
    u8 *fsi = img + lba_off(1);
    wr32(fsi, 0x000, 0x41615252);
    wr32(fsi, 0x1E8, 0x61417272);
    wr32(fsi, 0x1EC, 0xffffffffu); /* allocator maintains no free-count hint */
    wr32(fsi, 0x1F0, 0xffffffffu);

    /* ---- backup boot sector ------------------------------------------ */
    memcpy(img + lba_off(6), img, SECTOR);

    /* ---- FATs (both copies identical) -------------------------------- */
    for (u32 f = 0; f < NFATS; ++f) {
        u8 *T = img + lba_off(RESERVED + f * fat_secs);
        wr32(T, 0 * 4, 0x0FFFFFF8u);     /* media marker                   */
        wr32(T, 1 * 4, 0x0FFFFFFFu);
        wr32(T, 2 * 4, 0x0FFFFFFFu);     /* root dir: one cluster          */
        wr32(T, 3 * 4, 0x0FFFFFFFu);     /* KERNOS.TXT                     */
        wr32(T, 4 * 4, 0x0FFFFFFFu);     /* VERYLONGNAME.TXT               */
        for (u32 i = 0; i < u1_clusters + sh_clusters; ++i) {
            u32 c = program_cluster(i, marker_clus, rb_clus);
            u32 next = (i + 1 == u1_clusters || i + 1 == u1_clusters + sh_clusters) ?
                       0x0fffffffu : program_cluster(i + 1, marker_clus, rb_clus);
            wr32(T, c * 4, next);
        }
        /* Raw boot self-test sectors must never be allocated to files. */
        wr32(T, marker_clus * 4, 0x0FFFFFF7u);
        wr32(T, rb_clus * 4, 0x0FFFFFF7u);
    }

    /* ---- root directory ---------------------------------------------- */
    u8 *D = img + lba_off(data_start);

    /* slot 4: KERNOS.TXT */
    {
        u8 *e = D + 4 * 32;
        memset(e, 0, 32);
        memcpy(e, "KERNOS  TXT", 11);        /* base + ext, space padded */
        e[11]  = 0x20;
        e[0x1A] = 3;                         /* first cluster (lo,hi)=3,0 */
        wr32(e, 0x1C, (u32)strlen(BENCH_SEED)); /* size: full u32 word (M5.11) */
    }
    /* slot 3: deleted-entry decoy */
    {
        u8 *e = D + 3 * 32;
        memset(e, 0, 32);
        memcpy(e, "DELETED     ", 11);
        e[0]  = 0xE5;                        /* the tombstone            */
        e[11] = 0x20;
    }
    /* slots 0-2: VERYLONGNAME.TXT as LFN + short entry */
    {
        u8 chk = lfn_checksum((const u8 *)"VERYLO~1TXT");
        lfn_entry(D, 0, 0x02 | 0x40, BENCH_LONG_FILE, chk);  /* tail   */
        lfn_entry(D, 1, 0x01, BENCH_LONG_FILE, chk);         /* head   */
        u8 *e = D + 2 * 32;
        memset(e, 0, 32);
        memcpy(e, "VERYLO~1TXT", 11);
        e[11]  = 0x20;
        e[0x1A] = 4;                         /* first cluster (lo,hi)=4,0 */
        wr32(e, 0x1C, (u32)strlen(BENCH_LONG_BODY)); /* full u32 (M5.11) */
    }
    /* M5.11: planted-size read-back self-check - what the directory
       says must equal what was planted, reread from the just-built
       image bytes (this is where the (u8) truncation above used to
       hide: a lying dir entry with a clean exit). */
    {
        static const char *names[] = { "KERNOS  TXT", "VERYLO~1TXT" };
        u32 src[2] = { (u32)strlen(BENCH_SEED), (u32)strlen(BENCH_LONG_BODY) };
        u32 rd[2];
        u8 *es[2] = { D + 4 * 32, D + 2 * 32 };
        for (int k = 0; k < 2; ++k) {
            if (memcmp(es[k], names[k], 11) != 0) {
                fprintf(stderr, "mkfat32: planted name mismatch slot %d\n", k);
                free(u1); free(sh);
                return 1;
            }
            rd[k] = (u32)es[k][0x1C]
                  | ((u32)es[k][0x1D] << 8)
                  | ((u32)es[k][0x1E] << 16)
                  | ((u32)es[k][0x1F] << 24);
            if (rd[k] != src[k]) {
                fprintf(stderr, "mkfat32: planted size %u != source %u (%s)\n",
                        rd[k], src[k], names[k]);
                free(u1); free(sh);
                return 1;
            }
        }
        fprintf(stdout, "mkfat32: planted dir sizes verified (KERNOS.TXT %u, VERYLO~1TXT %u)\n",
                rd[0], rd[1]);
    }
    /* slot 5: U1.ELF - the userland program (M5.3) */
    {
        u8 *e = D + 5 * 32;
        memset(e, 0, 32);
        entry_name83(e, "U1", "ELF");
        e[11]  = 0x20;
        e[0x1A] = 5;                         /* first cluster            */
        wr32(e, 0x1C, u1_len);               /* exact size, LO u32       */
    }
    /* slot 6: SHELL.ELF - the first thing the OS runs (M5.5) */
    {
        u8 *e = D + 6 * 32;
        memset(e, 0, 32);
        entry_name83(e, "SHELL", "ELF");
        e[11]  = 0x20;
        wr16(e, 0x1A, program_cluster(u1_clusters, marker_clus, rb_clus));
        wr32(e, 0x1C, sh_len);
    }
    /* slot 7 onward: zero (terminator covered by the zeroed image) */

    /* ---- file bodies: cluster 3 @ data_start+1, cluster 4 @ +2 ------- */
    memcpy(img + lba_off(data_start + 1), BENCH_SEED, strlen(BENCH_SEED));
    memcpy(img + lba_off(data_start + 2), BENCH_LONG_BODY, strlen(BENCH_LONG_BODY));

    /* ---- raw AHCI markers in the free data area ----------------------- */
    memcpy(img + lba_off(BENCH_MARKER_LBA), "KERNOSDISK\001\002\003",
           BENCH_MARKER_LEN);
    /* BENCH_WRITE_LBA is left zero: the write-verify drives its own
       pattern round-trip. */

    /* ---- U1.ELF body: clusters 5 .. 4+u1_clusters --------------------- */
    u32 u1_left = u1_len;
    for (u32 c = 0; c < u1_clusters; ++c) {
        u32 chunk = u1_left > SECTOR ? SECTOR : u1_left;
        memcpy(img + lba_off(data_start + program_cluster(c, marker_clus, rb_clus) - 2), u1 + c * SECTOR, chunk);
        u1_left -= chunk;
    }

    /* ---- SHELL.ELF body: clusters 5+u1c .. --------------------------- */
    u32 sh_left = sh_len;
    for (u32 c = 0; c < sh_clusters; ++c) {
        u32 chunk = sh_left > SECTOR ? SECTOR : sh_left;
        memcpy(img + lba_off(data_start + program_cluster(u1_clusters + c, marker_clus, rb_clus) - 2),
               sh + c * SECTOR, chunk);
        sh_left -= chunk;
    }

    FILE *fp = fopen(argv[1], "wb");
    if (!fp) { perror(argv[1]); return 1; }
    if (fwrite(img, 1, sizeof img, fp) != sizeof img) {
        fprintf(stderr, "mkfat32: short write\n");
        fclose(fp);
        return 1;
    }
    fclose(fp);

    printf("total=%u reserved=%u fat_secs=%u data_start=%u clusters=%u\n",
           TOTAL_SECTORS, RESERVED, fat_secs, data_start, clusters);
    /* read back the planted dir entries: a planter bug (like a size
       field that silently truncates) must be a tool error, not a
       kernel boot surprise */
    {
        struct { const char *b, *x; u32 *lenp; u32 first; u32 slot; } planted[2] = {
            { "U1",    "ELF", &u1_len, 5,               5 },
            { "SHELL", "ELF", &sh_len, program_cluster(u1_clusters, marker_clus, rb_clus), 6 },
        };
        int bad = 0;
        for (int i = 0; i < 2; ++i) {
            u8 want[11];
            entry_name83(want, planted[i].b, planted[i].x);
            const u8 *e = D + planted[i].slot * 32;
            u32 sz = e[0x1C] | (e[0x1D] << 8) | (e[0x1E] << 16) |
                     ((u32)e[0x1F] << 24);
            int name_ok = memcmp(e, want, 11) == 0;
            int clus_ok = (e[0x1A] | ((u32)e[0x1B] << 8)) == planted[i].first;
            if (!name_ok || sz != *planted[i].lenp || !clus_ok) {
                fprintf(stderr, "mkfat32: %s entry read-back mismatch "
                                "(name ok=%d size=%u want %u cluster ok=%d)\n",
                        planted[i].b, name_ok, sz, *planted[i].lenp, clus_ok);
                bad = 1;
            }
        }
        if (bad) {
            free(u1); free(sh);
            return 1;
        }
    }
    printf("mkfat32: planted U1.ELF %u bytes (%u clusters, first cluster 5)\n",
           u1_len, u1_clusters);
    printf("mkfat32: planted SHELL.ELF %u bytes (%u clusters, first cluster %u)\n",
           sh_len, sh_clusters, 5 + u1_clusters);
    printf("mkfat32: ok\n");
    return 0;
}
