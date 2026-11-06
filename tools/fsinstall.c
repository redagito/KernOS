/* Install build outputs without regenerating the user's FAT32 disk. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blk_host.h"
#include "fat32.h"
#include <kernel/bench.h>

static int reserve_bench(struct block_dev *dev)
{
    struct fat_info info;
    fat_stats(&info);
    u64 markers[] = {BENCH_MARKER_LBA, BENCH_WRITE_LBA};
    for (unsigned i = 0; i < 2; ++i) {
        u32 cluster = 2 + (markers[i] - info.data_start) / info.spc;
        for (u32 f = 0; f < info.nfats; ++f) {
            u8 sector[512];
            u64 lba = info.reserved + f * info.fat_secs + cluster / 128;
            u32 off = (cluster % 128) * 4;
            if (blk_read(dev, lba, sector, 1)) return -1;
            u32 val = sector[off] | sector[off+1] << 8 | sector[off+2] << 16 |
                      (u32)sector[off+3] << 24;
            val &= 0x0fffffff;
            if (val != 0 && val != 0x0ffffff7) {
                fprintf(stderr, "fsinstall: benchmark sector overlaps a live file\n"); return -1;
            }
            sector[off] = 0xf7; sector[off+1] = sector[off+2] = 0xff;
            sector[off+3] = (sector[off+3] & 0xf0) | 0x0f;
            if (blk_write(dev, lba, sector, 1)) return -1;
        }
    }
    return fat_mount(dev);
}

int main(int argc, char **argv)
{
    if (argc < 4 || argc % 2) {
        fprintf(stderr, "usage: fsinstall image name host-file [name host-file ...]\n");
        return 2;
    }
    struct block_dev *dev = blk_host_open(argv[1]);
    if (!dev || fat_mount(dev)) return 1;
    if (reserve_bench(dev)) return 1;
    for (int i = 2; i < argc; i += 2) {
        FILE *f = fopen(argv[i + 1], "rb");
        if (!f) { perror(argv[i + 1]); return 1; }
        fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
        if (size < 0 || size > 512 * 1024) { fclose(f); return 1; }
        char *data = malloc(size ? size : 1), *old = malloc(size ? size : 1);
        if (!data || !old || fread(data, 1, size, f) != (size_t)size) return 1;
        fclose(f);
        int unchanged = fat_read_file(argv[i], old, size) == size && !memcmp(old, data, size);
        if (!unchanged && fat_save_file(argv[i], data, size) != size) {
            fprintf(stderr, "fsinstall: save failed: %s\n", argv[i]); return 1;
        }
        printf("fsinstall: %s %s (%ld bytes)\n", argv[i], unchanged ? "unchanged" : "installed", size);
        free(data); free(old);
    }
    return 0;
}
