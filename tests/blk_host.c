#include "blk_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

struct host_ctx {
    FILE *f;
};

static int h_read(void *ctx, u64 lba, void *buf, u32 n)
{
    struct host_ctx *c = ctx;
    if (c == 0)
        return -1;
    if (fseek(c->f, (long)(lba * 512u), SEEK_SET) != 0)
        return -1;
    if (fread(buf, 512, n, c->f) != (size_t)n) {
        fprintf(stderr, "blk_host: read lba=%llu n=%u failed (err=%d)\n",
                (unsigned long long)lba, n, errno);
        clearerr(c->f);
        return -1;
    }
    return 0;
}

static int h_write(void *ctx, u64 lba, const void *buf, u32 n)
{
    struct host_ctx *c = ctx;
    if (c == 0)
        return -1;
    if (fseek(c->f, (long)(lba * 512u), SEEK_SET) != 0)
        return -1;
    if (fwrite(buf, 512, n, c->f) != (size_t)n)
        return -1;
    return 0;
}

static u64 h_sectors(void *ctx)
{
    struct host_ctx *c = ctx;
    if (c == 0)
        return 0;
    if (fseek(c->f, 0, SEEK_END) != 0)
        return 0;
    long size = ftell(c->f);
    return size > 0 ? (u64)((unsigned long)size / 512u) : 0;
}

struct block_dev *blk_host_open(const char *path)
{
    FILE *f = fopen(path, "r+b");
    if (!f)
        return 0;

    struct host_ctx *c = malloc(sizeof *c);
    struct block_dev *b = malloc(sizeof *b);
    if (!c || !b) {
        fclose(f);
        return 0;
    }
    static const struct block_ops ops = { h_read, h_write, h_sectors };

    c->f = f;
    b->ops = &ops;
    b->ctx = c;
    return b;
}
