#pragma once

#include <kernel/types.h>

/*
 * Block layer (M4.3).
 *
 * The storage stack is split at this boundary so every pure-logic layer
 * above it (FAT32 today) is host-testable: the kernel backend is AHCI,
 * the test backend is a stdio-backed image file (tests/blk_host.c).
 *
 * Contract:
 *   - lba is a 512-byte sector number, buffers are 512*n bytes.
 *   - read/write return 0 on success, non-zero on failure
 *     (-1: transport error; specific backends may report other codes).
 *   - sectors() reports the device size in 512-byte sectors.
 */

struct block_ops {
    int (*read)(void *ctx, u64 lba, void *buf, u32 sectors);
    int (*write)(void *ctx, u64 lba, const void *buf, u32 sectors);
    u64 (*sectors)(void *ctx);
};

struct block_dev {
    const struct block_ops *ops;   /* first: matches static initializer style */
    void *ctx;
};

static inline int blk_read(const struct block_dev *b, u64 lba, void *buf, u32 n)
{
    return b->ops->read(b->ctx, lba, buf, n);
}

static inline int blk_write(const struct block_dev *b, u64 lba, const void *buf, u32 n)
{
    return b->ops->write(b->ctx, lba, buf, n);
}

static inline u64 blk_sectors(const struct block_dev *b)
{
    return b->ops->sectors(b->ctx);
}
