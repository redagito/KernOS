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
    if (total <= data_lba || data_lba > 0xFFFFFFFFu)
        return -1;

    clusters = (total - data_lba) / bs[13];
    /* FAT32 is defined ABOVE 65524 clusters (ATA8-ACS geometry: 4086
       <= n <= 65524 means FAT16). Do not mislabel the format. */
    if (clusters <= 65524 || clusters > 0x0FFFFFEEu ||
        (u64)fatsecs * 128u < clusters + 2u)
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
    if ((clus >> 7) >= st.fat_secs)
        return -1;
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
    if (clus < 2 || clus >= st.total_clusters + 2)
        return -1;
    if (fat_get(clus, &v) != 0)
        return -1;
    v &= 0x0FFFFFFFu;
    if (v >= FAT_EOF_MIN)
        return 1;
    if (v == FAT_BAD || v < 2)
        return -1;
    *next = v;
    return 0;
}

static int valid_clus(u32 clus)
{
    return clus >= 2 && clus < st.total_clusters + 2;
}

/* Update one entry in every FAT while preserving each copy's reserved high
   nibble. The cache is invalidated after every attempted update. */
static int fat_set(u32 clus, u32 value)
{
    if (!valid_clus(clus) || (clus >> 7) >= st.fat_secs)
        return -1;
    u32 off = (clus & 127) * 4;
    for (u32 f = 0; f < st.nfats; ++f) {
        u64 lba = (u64)st.reserved + (u64)f * st.fat_secs + (clus >> 7);
        if (lba >= st.total_sectors || blk_read(fs, lba, secbuf, 1) != 0)
            goto fail;
        u32 old = rd32(secbuf, off);
        u32 v = (old & 0xF0000000u) | (value & 0x0FFFFFFFu);
        secbuf[off] = (u8)v;
        secbuf[off + 1] = (u8)(v >> 8);
        secbuf[off + 2] = (u8)(v >> 16);
        secbuf[off + 3] = (u8)(v >> 24);
        if (blk_write(fs, lba, secbuf, 1) != 0)
            goto fail;
    }
    fatsec_no = ~(u32)0;
    return 0;
fail:
    fatsec_no = ~(u32)0;
    return -1;
}

/* Uppercase a single 8.3 char: input already restricted by callers to
   the FAT charset (no validation demanded here - we compare chars). */
static u8 up8_3(u8 c)
{
    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    return c;
}

/* Strict 8.3 parser for creation: never silently truncate into another
   existing file's short alias. */
static int make_shortname(const char *in, u8 out[11])
{
    u32 base = 0, ext = 0;
    int dot = 0;
    if (!in || !*in)
        return -1;
    for (u32 i = 0; i < 11; ++i)
        out[i] = ' ';
    for (u32 i = 0; in[i]; ++i) {
        u8 c = (u8)in[i];
        if (c == '.') {
            if (dot || base == 0 || ext != 0)
                return -1;
            dot = 1;
            continue;
        }
        if (c < 0x21 || c > 0x7e || c == '"' || c == '*' || c == '/' ||
            c == ':' || c == '<' || c == '>' || c == '?' || c == '\\' || c == '|')
            return -1;
        if (!dot) {
            if (++base > 8)
                return -1;
            out[base - 1] = up8_3(c);
        } else {
            if (++ext > 3)
                return -1;
            out[7 + ext] = up8_3(c);
        }
    }
    if (base == 0 || (dot && ext == 0))
        return -1;
    return 0;
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

struct dir_result {
    u64 lba;
    u32 off;
    u32 first_clus;
    u32 size;
    int found;
};

/* Locate an existing root entry or the first reusable directory slot. */
static int root_locate(const u8 want[11], struct dir_result *r)
{
    u32 clus = st.root_clus;
    u32 hops = st.total_clusters;
    int have_free = 0;
    while (hops-- && valid_clus(clus)) {
        u64 lba = clus_lba(clus);
        for (u32 s = 0; s < st.spc; ++s) {
            if (lba + s >= st.total_sectors || blk_read(fs, lba + s, secbuf, 1) != 0)
                return -1;
            for (u32 e = 0; e < 16; ++e) {
                const u8 *en = secbuf + e * 32;
                u64 at = lba + s;
                u32 off = e * 32;
                if (en[0] == 0x00) {
                    if (!have_free) {
                        r->lba = at; r->off = off; have_free = 1;
                    }
                    return 1;
                }
                if (en[0] == 0xE5) {
                    if (!have_free) {
                        r->lba = at; r->off = off; have_free = 1;
                    }
                    continue;
                }
                if (en[0x0B] == 0x0F)
                    continue;
                if (name_match(en, want)) {
                    u8 attr = en[0x0B];
                    if ((attr & (0x01 | 0x08 | 0x10)) != 0)
                        return -1; /* read-only, volume label, or directory */
                    r->lba = at; r->off = off;
                    r->first_clus = rd16(en, 0x1A) | (rd16(en, 0x14) << 16);
                    r->size = rd32(en, 0x1C);
                    r->found = 1;
                    return 0;
                }
            }
        }
        u32 next;
        int rc = fat_next(clus, &next);
        if (rc == 1)
            return have_free ? 1 : 2; /* full, valid root can be extended */
        if (rc < 0)
            return -1;
        clus = next;
    }
    return -1;
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
    if (make_shortname(name, want) != 0) return -1;

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
    return remain ? -1 : (int)copied;
}

int fat_write_file(const char *name, const void *buf, u32 len)
{
    if (!fs)
        return -1;

    u8 want[11];
    if (make_shortname(name, want) != 0) return -1;

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

static int chain_count(u32 first, u32 size, u32 *count)
{
    if (first == 0) {
        if (size != 0) return -1;
        *count = 0;
        return 0;
    }
    u32 c = first, n = 0;
    u32 need = size / (512u * st.spc) + (size % (512u * st.spc) != 0);
    while (n < st.total_clusters) {
        if (!valid_clus(c)) return -1;
        ++n;
        u32 next;
        int rc = fat_next(c, &next);
        if (rc == 1) {
            if (n != need) return -1; /* refuse overlong/shared chains too */
            *count = n;
            return 0;
        }
        if (rc < 0) return -1;
        c = next;
    }
    return -1;
}

/* Best-effort bounded cleanup; safe for chains produced by this module. */
static void chain_free(u32 first)
{
    u32 c = first;
    for (u32 n = 0; n < st.total_clusters && valid_clus(c); ++n) {
        u32 next;
        int rc = fat_next(c, &next);
        if (rc < 0) return;
        if (fat_set(c, 0) != 0) return;
        if (rc == 1) return;
        c = next;
    }
}

static int chain_allocate(u32 need, u32 *first)
{
    u32 head = 0, tail = 0, scanned = 0, loose = 0;
    *first = 0;
    for (u32 c = 2; c < st.total_clusters + 2 && scanned < st.total_clusters; ++c) {
        ++scanned;
        u32 v;
        if (fat_get(c, &v) != 0) goto fail;
        if ((v & 0x0FFFFFFFu) != 0) continue;
        if (fat_set(c, FAT_EOF_MIN) != 0) { loose = c; goto fail; }
        if (tail && fat_set(tail, c) != 0) {
            loose = c;
            goto fail;
        }
        if (!head) head = c;
        tail = c;
        if (head && --need == 0) {
            *first = head;
            return 0;
        }
    }
fail:
    if (loose) (void)fat_set(loose, 0);
    if (head) chain_free(head);
    return -1;
}

static int chain_write(u32 first, const u8 *src, u32 len)
{
    u32 c = first, done = 0;
    u32 cluster_bytes = 512u * st.spc;
    u32 need = len / cluster_bytes + (len % cluster_bytes != 0);
    for (u32 n = 0; n < need; ++n) {
        if (!valid_clus(c)) return -1;
        u64 lba = clus_lba(c);
        for (u32 s = 0; s < st.spc; ++s) {
            for (u32 i = 0; i < 512; ++i)
                secbuf[i] = done < len ? src[done++] : 0;
            if (lba + s >= st.total_sectors || blk_write(fs, lba + s, secbuf, 1) != 0)
                return -1;
        }
        if (n + 1 < need) {
            u32 next;
            if (fat_next(c, &next) != 0) return -1;
            c = next;
        }
    }
    return done == len ? 0 : -1;
}

int fat_file_size(const char *name)
{
    if (!fs) return -1;
    u8 want[11];
    if (make_shortname(name, want) != 0) return -1;
    struct dir_result r;
    r.found = 0;
    if (root_locate(want, &r) != 0 || !r.found || r.size > 0x7FFFFFFFu)
        return -1;
    return (int)r.size;
}

int fat_save_file(const char *name, const void *buf, u32 len)
{
    if (!fs || len > 0x7FFFFFFFu || (len && !buf)) return -1;
    u8 want[11];
    if (make_shortname(name, want) != 0) return -1;

    struct dir_result r;
    r.found = 0;
    int where = root_locate(want, &r);
    if (where < 0) return -1;
    if (where == 2) {
        u32 tail = st.root_clus, extra = 0;
        int end = 0;
        for (u32 hop = 0; hop < st.total_clusters; ++hop) {
            u32 next;
            int rc = fat_next(tail, &next);
            if (rc < 0) return -1;
            if (rc == 1) { end = 1; break; }
            tail = next;
        }
        if (!end || chain_allocate(1, &extra) != 0) return -1;
        for (u32 i = 0; i < 512; ++i) secbuf[i] = 0;
        for (u32 s = 0; s < st.spc; ++s)
            if (blk_write(fs, clus_lba(extra) + s, secbuf, 1) != 0) {
                chain_free(extra); return -1;
            }
        if (fat_set(tail, extra) != 0) { chain_free(extra); return -1; }
        r.lba = clus_lba(extra); r.off = 0;
        where = 1;
    }

    u32 old_count = 0;
    if (where == 0 && chain_count(r.first_clus, r.size, &old_count) != 0)
        return -1;
    (void)old_count;

    u32 cluster_bytes = 512u * st.spc;
    u32 need = len / cluster_bytes + (len % cluster_bytes != 0);
    u32 fresh = 0;
    if (need && chain_allocate(need, &fresh) != 0)
        return -1;
    if (need && chain_write(fresh, (const u8 *)buf, len) != 0) {
        chain_free(fresh);
        return -1;
    }

    /* Keep the prior directory record so a failed sector write can be
       rolled back as far as the block device permits. */
    u8 old_entry[32];
    if (blk_read(fs, r.lba, secbuf, 1) != 0) {
        if (fresh) chain_free(fresh);
        return -1;
    }
    for (u32 i = 0; i < 32; ++i) old_entry[i] = secbuf[r.off + i];
    if (where == 1) {
        for (u32 i = 0; i < 32; ++i) secbuf[r.off + i] = 0;
        for (u32 i = 0; i < 11; ++i) secbuf[r.off + i] = want[i];
        secbuf[r.off + 0x0B] = 0x20; /* archive */
    }
    secbuf[r.off + 0x14] = (u8)(fresh >> 16);
    secbuf[r.off + 0x15] = (u8)(fresh >> 24);
    secbuf[r.off + 0x1A] = (u8)fresh;
    secbuf[r.off + 0x1B] = (u8)(fresh >> 8);
    secbuf[r.off + 0x1C] = (u8)len;
    secbuf[r.off + 0x1D] = (u8)(len >> 8);
    secbuf[r.off + 0x1E] = (u8)(len >> 16);
    secbuf[r.off + 0x1F] = (u8)(len >> 24);
    if (blk_write(fs, r.lba, secbuf, 1) != 0) {
        for (u32 i = 0; i < 32; ++i) secbuf[r.off + i] = old_entry[i];
        (void)blk_write(fs, r.lba, secbuf, 1);
        if (fresh) chain_free(fresh);
        return -1;
    }

    if (where == 0 && r.first_clus)
        chain_free(r.first_clus);
    return (int)len;
}
