#pragma once

#include <kernel/types.h>
#include <kernel/block.h>

/*
 * FAT32 (M4.3): a compact, portable, host-testable subset.
 *
 * Scope of this milestone (kept deliberately tight):
 *   - mount validates a real FAT32 BPB (512 B sectors, cluster count
 *     in the FAT32 range, sane reserved/FAT geometry) - no FAT12/16.
 *   - files are addressed by their SHORT 8.3 name (case-insensitive).
 *     LFN dir entries are skipped, deleted entries are skipped.
 *   - reads walk the cluster chain; capacity must fit the caller's
 *     buffer.
 *   - fat_write_file preserves its legacy in-place, len <= size behavior.
 *   - fat_save_file creates or replaces root 8.3 files using bounded
 *     cluster allocation and mirrored FAT updates.
 *
 * The module prints nothing; callers turn results into evidence, which
 * is exactly why the identical code also runs under tests/test_fat32.c.
 */

struct fat_info {
    u32 spc;                 /* sectors per cluster                        */
    u32 reserved;            /* reserved sectors (FAT starts here)         */
    u32 nfats;               /* FAT copies                                 */
    u32 fat_secs;            /* sectors per FAT                            */
    u32 root_clus;           /* root directory start cluster               */
    u32 data_start;          /* LBA of cluster 2                           */
    u32 total_clusters;      /* numbered 2 .. +2-1                         */
    u64 total_sectors;       /* device size in 512-byte sectors            */
};

int  fat_mount(const struct block_dev *dev);     /* 0 = ok */
int  fat_read_file(const char *name, void *buf, u32 cap);   /* bytes, -1 = fail */
int  fat_write_file(const char *name, const void *buf, u32 len);
/* Create or atomically replace a root 8.3 file; returns len or -1. */
int  fat_save_file(const char *name, const void *buf, u32 len);
int  fat_file_size(const char *name);             /* bytes, -1 = fail */
void fat_stats(struct fat_info *out);
