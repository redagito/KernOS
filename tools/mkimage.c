/*
 * mkimage — KernOS disk image assembler.
 *
 * A host tool (built with the host gcc, it never runs in the kernel).
 * Assembles the fixed disk layout described in docs/disk-layout.md:
 *
 *   LBA 0        stage 1 (our MBR, exactly 512 bytes incl. 0xAA55)
 *   LBA 1..62    stage 2 (flat binary, <= 62 * 512 bytes, zero-padded)
 *   LBA 63       reserved (zeros)
 *   LBA 64..575  kernel flat image (<= 256 KiB, zero-padded)
 *   ...          image is padded to --size MiB (default 64)
 *
 * Exit status 0 = image written; anything else = refuse to lie.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR      512u
#define STAGE2_LBA       1u
#define STAGE2_SECTORS  62u
#define KERNEL_LBA      64u
#define KERNEL_SECTORS 512u
#define IMAGE_SECTORS   (64u * 1024u * 1024u / SECTOR)  /* 64 MiB default */

static unsigned char *slurp(const char *path, long *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "mkimage: cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(size ? (size_t)size : 1);
    if (!buf) { fprintf(stderr, "mkimage: out of host memory\n"); exit(1); }
    if (size && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "mkimage: short read on %s\n", path); exit(1);
    }
    fclose(f);
    *size_out = size;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr,
                "usage: %s <out.img> <mbr.bin> <stage2.bin> <kernel.bin> [size_mib]\n",
                argv[0]);
        return 2;
    }
    const char *out_path = argv[1];
    const char *mbr_path = argv[2];
    const char *s2_path = argv[3];
    const char *kern_path = argv[4];
    unsigned long size_mib = argc > 5 ? strtoul(argv[5], NULL, 0) : 64;
    if (size_mib == 0) size_mib = 64;

    long mbr_size, s2_size, kern_size;
    unsigned char *mbr = slurp(mbr_path, &mbr_size);
    unsigned char *s2 = slurp(s2_path, &s2_size);
    unsigned char *kern = slurp(kern_path, &kern_size);

    if (mbr_size != 512) {
        fprintf(stderr, "mkimage: mbr.bin must be exactly 512 bytes, got %ld\n", mbr_size);
        return 1;
    }
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        fprintf(stderr, "mkimage: mbr.bin not bootable (missing 0x55AA signature)\n");
        return 1;
    }
    if (s2_size > (long)(STAGE2_SECTORS * SECTOR)) {
        fprintf(stderr, "mkimage: stage2.bin = %ld bytes, boot slot holds %u\n",
                s2_size, STAGE2_SECTORS * SECTOR);
        return 1;
    }
    if (kern_size > (long)(KERNEL_SECTORS * SECTOR)) {
        fprintf(stderr, "mkimage: kernel.bin = %ld bytes, boot slot holds %u\n",
                kern_size, KERNEL_SECTORS * SECTOR);
        return 1;
    }

    unsigned long image_sectors =
        size_mib * 1024u * 1024u / SECTOR;
    if (image_sectors < KERNEL_LBA + KERNEL_SECTORS) {
        fprintf(stderr, "mkimage: image size %lu MiB too small for layout\n", size_mib);
        return 1;
    }
    unsigned char *img = calloc(image_sectors, SECTOR);
    if (!img) { fprintf(stderr, "mkimage: out of host memory\n"); return 1; }

    memcpy(img, mbr, 512);
    memcpy(img + STAGE2_LBA * SECTOR, s2, (size_t)s2_size);
    memcpy(img + KERNEL_LBA * SECTOR, kern, (size_t)kern_size);

    FILE *out = fopen(out_path, "wb");
    if (!out) { fprintf(stderr, "mkimage: cannot create %s\n", out_path); return 1; }
    size_t total = (size_t)image_sectors * SECTOR;
    if (fwrite(img, 1, total, out) != total) {
        fprintf(stderr, "mkimage: short write to %s\n", out_path);
        return 1;
    }
    fclose(out);

    printf("mkimage: %s, %lu MiB, stage2 %ld B kernel %ld B\n",
           out_path, size_mib, s2_size, kern_size);
    return 0;
}
