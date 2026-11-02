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
 *       [6]      0x00 terminator; rest zero
 *   - fixture data from include/kernel/bench.h (shared with the kernel
 *     boot gates and host unit tests).
 *   - raw bench markers planted in the DATA area, with layout
 *     self-checks that refuse any collision with allocated clusters.
 *
 * argv: mkfat32 <image-file> <u1-elf-path> - the userland binary is
 * planted at clusters 5.. so the kernel loads its first processes from
 * the disk, not from its own image.
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

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <image-file> <u1-elf-path>\n",
                argv ? argv[0] : "mkfat32");
        return 2;
    }

    /* the program that userland will run: read it BEFORE anything else
       so a bad path is a clean tool error, not a half-built image */
    u32 u1_len = 0;
    u8 *u1 = read_file(argv[2], &u1_len, 16 * 1024);
    if (!u1 || (*u1 != 0x7F)) {
        fprintf(stderr, "mkfat32: cannot read a valid u1 elf '%s'\n", argv[2]);
        free(u1);
        return 1;
    }
    u32 u1_clusters = (u1_len + SECTOR - 1) / SECTOR;

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
    /* clusters 2,3,4 fixed by the fixture; U1.ELF takes 5 .. 4+u1_clusters;
       the AHCI raw markers must start after all of it */
    if (marker_clus < 5 + u1_clusters) {
        fprintf(stderr, "mkfat32: marker LBA %u collides with the U1.ELF "
                        "chain (%u clusters)\n", BENCH_MARKER_LBA, u1_clusters);
        return 1;
    }
    if (BENCH_WRITE_LBA <= BENCH_MARKER_LBA || rb_clus <= marker_clus ||
        BENCH_WRITE_LBA >= TOTAL_SECTORS || marker_clus + 1 == rb_clus) {
        fprintf(stderr, "mkfat32: write LBA %u invalid vs marker LBA %u "
                        "(clusters %u/%u)\n",
                BENCH_WRITE_LBA, BENCH_MARKER_LBA, rb_clus, marker_clus);
        return 1;
    }
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
    wr32(fsi, 0x1EC, clusters - 3 - u1_clusters);   /* 2..4 + u1 chain */
    wr32(fsi, 0x1F0, 5 + u1_clusters);              /* next free       */

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
        for (u32 c = 5; c < 5 + u1_clusters - 1; ++c)
            wr32(T, c * 4, c + 1);       /* U1.ELF chain                   */
        wr32(T, (4 + u1_clusters) * 4, 0x0FFFFFFFu);   /* chain end       */
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
        e[0x1C] = (u8)strlen(BENCH_SEED);    /* size: < 256 bytes        */
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
        e[0x1C] = (u8)strlen(BENCH_LONG_BODY);
    }
    /* slot 5: U1.ELF - the userland program (M5.3) */
    {
        u8 *e = D + 5 * 32;
        memset(e, 0, 32);
        memcpy(e, "U1      ELF", 11);        /* base + ext, space padded */
        e[11]  = 0x20;
        e[0x1A] = 5;                         /* first cluster            */
        wr32(e, 0x1C, u1_len);               /* exact size, LO u32       */
    }
    /* slot 6 onward: zero (terminator covered by the zeroed image) */

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
        memcpy(img + lba_off(data_start + 3 + c), u1 + c * SECTOR, chunk);
        u1_left -= chunk;
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
    /* read back the planted dir entry: a planter bug (like a size
       field that silently truncates) must be a tool error, not a
       kernel boot surprise */
    {
        const u8 *e = D + 5 * 32;
        u32 sz = e[0x1C] | (e[0x1D] << 8) | (e[0x1E] << 16) | ((u32)e[0x1F] << 24);
        if (memcmp(D + 5 * 32, "U1      ELF", 11) != 0 || sz != u1_len) {
            fprintf(stderr, "mkfat32: U1.ELF entry read-back mismatch "
                            "(name ok=%d size=%u want %u)\n",
                    memcmp(D + 5 * 32, "U1      ELF", 11) == 0, sz, u1_len);
            free(u1);
            return 1;
        }
    }
    printf("mkfat32: planted U1.ELF %u bytes (%u clusters, first cluster 5)\n",
           u1_len, u1_clusters);
    printf("mkfat32: ok\n");
    return 0;
}
