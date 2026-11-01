#include <kernel/types.h>
#include <kernel/block.h>
#include "fat32.h"

#ifdef FAT32_DBG
#include <stdio.h>       /* host-side lab debugging only (-DFAT32_DBG) */
#endif

/*
 * FAT32 subset over the block layer - nothing below here touches
 * hardware, ports or memory maps. Static budget: two 512-byte sector
 * buffers (the scratch IO buffer and the one-sector FAT cache).
 */

static const struct block_dev *fs;
static struct fat_info st;

static u8  secbuf[512];          /* generic scratch sector               */
static u32 fatsec_no;            /* which sector the cached FAT came from */
static u8  fatsec[512];          /* 1-sector FAT cache: 128 entries/sector */

/* little-endian field readers (bytes, not loads of wide types) */
static u32 rd16(const u8 *b, u32 o) { return b[o] | (b[o + 1] << 8); }
static u32 rd32(const u8 *b, u32 o)
{
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | ((u32)b[o + 3] << 24);
}

int fat_mount(const struct block_dev *dev)
{
    u8 bs[512];
    u64 total, data_lba, clusters;

    if (!dev)
        return -1;

    if (blk_read(dev, 0, bs, 1) != 0)
        return -1;

    if (rd16(bs, 11) != 512)                     /* bytes/sector    */
        return -1;

    if (bs[13] == 0 || (bs[13] & (bs[13] - 1)) || bs[13] > 128)
        return -1;                               /* spc: po2 1..128 */

    u32 reserved = rd16(bs, 14);
    u32 nfats    = bs[16];
    u32 fatsecs  = rd32(bs, 0x24);
    u32 root     = rd32(bs, 0x2C);
    total        = blk_sectors(dev);

    if (reserved == 0 || nfats == 0 || fatsecs == 0 || root < 2)
        return -1;

    data_lba = (u64)reserved + (u64)nfats * fatsecs;
    if (total <= data_lba)
        return -1;

    clusters = (total - data_lba) / bs[13];
    /* FAT32 is defined ABOVE 65524 clusters (ATA8-ACS geometry: 4086
       <= n <= 65524 means FAT16). Do not mislabel the format. */
    if (clusters <= 65524)
        return -1;

    fs = dev;

    st.spc            = bs[13];
    st.reserved       = reserved;
    st.nfats          = nfats;
    st.fat_secs       = fatsecs;
    st.root_clus      = root;
    st.data_start     = (u32)data_lba;
    st.total_clusters = (u32)clusters;
    st.total_sectors  = total;

    fatsec_no = ~(u32)0;             /* invalidate the FAT cache          */
    return 0;
}

void fat_stats(struct fat_info *out)
{
    *out = st;
}

/* One FAT32 table entry, with a single-sector cache: chains read
   sequential clusters, so consecutive lookups usually hit the cache. */
static int fat_get(u32 clus, u32 *out)
{
    u32 sec = st.reserved + (clus >> 7);     /* 512 B / 4 B = 128 entries */

    if (fatsec_no != sec) {
        if (blk_read(fs, sec, fatsec, 1) != 0)
            return -1;
        fatsec_no = sec;
    }
    *out = rd32(fatsec, (clus & 127) * 4);
    return 0;
}

#define FAT_EOF_MIN    0x0FFFFFF8u         /* EOF chain values          */
#define FAT_BAD        0x0FFFFFF7u         /* bad cluster               */

static u64 clus_lba(u32 clus)
{
    return (u64)st.data_start + (u64)(clus - 2) * st.spc;
}

/* chain iterator: -1 error, 0 more clusters, 1 = clean end */
static int fat_next(u32 clus, u32 *next)
{
    u32 v;
    if (fat_get(clus, &v) != 0)
        return -1;
    if (v >= FAT_EOF_MIN)
        return 1;
    if (v == FAT_BAD || v < 2)
        return -1;
    *next = v;
    return 0;
}

/* Uppercase a single 8.3 char: input already restricted by callers to
   the FAT charset (no validation demanded here - we compare chars). */
static u8 up8_3(u8 c)
{
    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    return c;
}

/* "KERNOS.TXT" -> 11-byte dir-entry name "KERNOS  TXT" (space padded).
   Dot-less names are treated as base-only ("VERYLO~1TXT" style also
   fits this shape). */
static void shortname(const char *in, u8 out[11])
{
    u32 i = 0, o = 0;

    for (o = 0; o < 11; ++o)
        out[o] = ' ';
    o = 0;                              /* the blank-fill left o at 11: the
                                           copy loops below all repeat the
                                           o < limit guard, so o MUST
                                           restart at 0 here              */

    while (in[i] && in[i] != '.' && o < 8)
        out[o++] = up8_3((u8)in[i++]);

    if (in[i] == '.') {
        ++i;
        o = 8;
        while (in[i] && o < 11)
            out[o++] = up8_3((u8)in[i++]);
    }
}

static int name_match(const u8 *e, const u8 want[11])
{
    for (u32 i = 0; i < 11; ++i) {
        u8 ec = up8_3(e[i]);
        if (ec != want[i])
            return 0;
    }
    return 1;
}

/* Walk a directory's cluster chain looking for a short-name entry.
   Skips LFN entries (attr 0x0F) and deleted entries (first byte 0xE5).
   A zero first byte ends the directory. */
static int dir_find(u32 dir_clus, const u8 want[11], u32 *first_clus, u32 *size)
{
#ifdef FAT32_DBG
    fprintf(stderr, "dir_find: dir=%u want=%.11s\n", dir_clus, (const char *)want);
#endif
    u32 clus = dir_clus;
    u32 hops = st.total_clusters + 1;        /* bound the loop hard      */

    while (hops--) {
        if (clus < 2)
            return -1;
        u64 lba = clus_lba(clus);
        for (u32 s = 0; s < st.spc; ++s) {
            if (blk_read(fs, lba + s, secbuf, 1) != 0)
                return -1;
            for (u32 e = 0; e < 512 / 32; ++e) {
                const u8 *en = secbuf + e * 32;
#ifdef FAT32_DBG
                fprintf(stderr, "  e=%2u %02x%02x%02x%02x%02x "
                        "%02x%02x%02x%02x%02x%02x attr=%02x\n",
                        e, en[0], en[1], en[2], en[3], en[4],
                        en[5], en[6], en[7], en[8], en[9], en[10], en[11]);
#endif
                if (en[0] == 0x00)
                    return -1;               /* end of directory         */
                if (en[0] == 0xE5)
                    continue;                /* deleted                  */
                if (en[0x0B] == 0x0F)
                    continue;                /* LFN: short entry follows */
                if (name_match(en, want)) {
                    *first_clus = rd16(en, 0x1A) | (rd16(en, 0x14) << 16);
                    *size       = rd32(en, 0x1C);
                    return 0;
                }
            }
        }
        int rc = fat_next(clus, &clus);
        if (rc == 1)
            return -1;                       /* chain ended: not found   */
        if (rc < 0)
            return -1;
    }
    return -1;
}

int fat_read_file(const char *name, void *buf, u32 cap)
{
    if (!fs)
        return -1;

    u8 want[11];
    shortname(name, want);

    u32 clus, size;
    if (dir_find(st.root_clus, want, &clus, &size) < 0)
        return -1;

    if (size > cap)
        return -1;                       /* caller's buffer too small    */

    u8 *dst = (u8 *)buf;
    u32 copied = 0, remain = size;
    u32 hops = st.total_clusters + 1;

    while (remain && hops--) {
        if (clus < 2)
            return -1;
        u64 lba = clus_lba(clus);
        for (u32 s = 0; s < st.spc && remain; ++s) {
            if (blk_read(fs, lba + s, secbuf, 1) != 0)
                return -1;
            u32 n = remain < 512 ? remain : 512;
            for (u32 i = 0; i < n; ++i)
                dst[copied++] = secbuf[i];
            remain -= n;
        }
        int rc = fat_next(clus, &clus);
        if (rc == 1)
            break;                       /* buffer satisfied or EOF      */
        if (rc < 0)
            return -1;
    }
    return (int)copied;
}

int fat_write_file(const char *name, const void *buf, u32 len)
{
    if (!fs)
        return -1;

    u8 want[11];
    shortname(name, want);

    u32 clus, size;
    if (dir_find(st.root_clus, want, &clus, &size) < 0)
        return -1;
    if (len > size)
        return -1;                       /* no extension in M4.3         */

    const u8 *src = (const u8 *)buf;
    u32 put = 0;
    u32 hops = st.total_clusters + 1;

    while (put < len && hops--) {
        if (clus < 2)
            return -1;
        u64 lba = clus_lba(clus);
        for (u32 s = 0; s < st.spc && put < len; ++s) {
            u32 n = (len - put) < 512 ? (len - put) : 512;
            if (n == 512) {
                if (blk_write(fs, lba + s, src + put, 1) != 0)
                    return -1;
            } else {
                /* tail: read-modify-write, the rest of the file's last
                   sector is preserved */
                if (blk_read(fs, lba + s, secbuf, 1) != 0)
                    return -1;
                for (u32 i = 0; i < n; ++i)
                    secbuf[i] = src[put + i];
                if (blk_write(fs, lba + s, secbuf, 1) != 0)
                    return -1;
            }
            put += n;
        }
        int rc = fat_next(clus, &clus);
        if (rc == 1)
            break;
        if (rc < 0)
            return -1;
    }
    return (int)put;
}
