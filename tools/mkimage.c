/*
 * mkimage — KernOS disk image assembler.
 *
 * A host tool (built with the host gcc, it never runs in the kernel).
 * Assembles the fixed disk layout described in docs/disk-layout.md:
 *
 *   LBA 0        stage 1 (our MBR, exactly 512 bytes incl. 0xAA55)
 *   LBA 1..62    stage 2 (flat binary, <= 62 * 512 bytes, zero-padded)
 *   LBA 63       KIH v1 image header (the boot ABI: sizes + checksums)
 *   LBA 64..575  kernel flat image (<= 256 KiB, zero-padded)
 *   ...          image is padded to --size MiB (default 64)
 *
 * The header is the contract between mkimage and stage 2: stage 2
 * validates magic, version, both additive checksums and the field
 * bounds before it trusts anything. Exit status 0 = image written;
 * anything else = refuse to lie.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR      512u
#define KIH_LBA          63u
#define STAGE2_LBA       1u
#define STAGE2_SECTORS  62u
#define KERNEL_LBA      64u
#define KERNEL_SECTORS 512u
#define KIH_MAGIC   0x3148494Bu         /* 'K','I','H','1' little-endian */
#define KIH_VERSION      1u
#define KIH_ENTRY   0x100000u           /* flat kernel load address      */

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

static void wr_le32(unsigned char *p, unsigned v) {
    p[0] = v & 0xFFu;
    p[1] = (v >> 8) & 0xFFu;
    p[2] = (v >> 16) & 0xFFu;
    p[3] = (v >> 24) & 0xFFu;
}

static unsigned rd_le32(const unsigned char *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

/* stage 2 sums raw little-endian dwords; mkimage must see the same
   arithmetic, byte-by-byte, never host-endianness shortcuts. */
static unsigned le_add(const unsigned char *p, unsigned bytes) {
    unsigned sum = 0;
    for (unsigned i = 0; i < bytes; i += 4)
        sum += rd_le32(p + i);
    return sum;
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

    /* ----- KIH v1 image header (the boot ABI) ---------------------- */
    unsigned kern_psectors = ((unsigned long)kern_size + SECTOR - 1) / SECTOR;
    if (kern_psectors == 0) {
        fprintf(stderr, "mkimage: empty kernel refuses to boot\n");
        return 1;
    }
    if (kern_psectors > KERNEL_SECTORS) {   /* ceil'd, complements the raw check */
        fprintf(stderr, "mkimage: kernel.bin = %ld bytes padded to %u sectors, slot holds %u\n",
                kern_size, kern_psectors, KERNEL_SECTORS);
        return 1;
    }

    /* payload checksum: over the EXACT byte span stage 2 will sum,
       sector padding included (the disk bytes are identical). */
    unsigned ksum = le_add(img + (size_t)KERNEL_LBA * SECTOR,
                           kern_psectors * SECTOR);

    unsigned char *h = img + (size_t)KIH_LBA * SECTOR;
    wr_le32(h + 0x00, KIH_MAGIC);
    h[0x04] = KIH_VERSION;              /* version          */
    h[0x05] = 1;                        /* hdr_sectors: one */
    wr_le32(h + 0x08, KERNEL_LBA);
    wr_le32(h + 0x0C, kern_psectors);
    wr_le32(h + 0x10, KIH_ENTRY);
    wr_le32(h + 0x14, 0);               /* flags    */
    wr_le32(h + 0x18, 0);               /* reserved */
    wr_le32(h + 0x20, ksum);            /* kernel payload checksum */

    /* self-checksum: negative additive sum so the whole-sector total
       (stage 2's validation) wraps to 0. Computed, then re-verified. */
    unsigned hs = 0;
    for (unsigned o = 0; o < SECTOR; o += 4)
        if (o != 0x1C)
            hs += rd_le32(h + o);
    wr_le32(h + 0x1C, 0u - hs);
    if (le_add(h, SECTOR) != 0) {
        fprintf(stderr, "mkimage: KIH checksum did not close (internal bug)\n");
        return 1;
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) { fprintf(stderr, "mkimage: cannot create %s\n", out_path); return 1; }
    size_t total = (size_t)image_sectors * SECTOR;
    if (fwrite(img, 1, total, out) != total) {
        fprintf(stderr, "mkimage: short write to %s\n", out_path);
        return 1;
    }
    fclose(out);

    printf("mkimage: %s, %lu MiB, stage2 %ld B kernel %ld B (%u sect)\n",
           out_path, size_mib, s2_size, kern_size, kern_psectors);
    printf("mkimage: kih v1 lba=%u ksectors=%u entry=0x%x kchk=0x%08x\n",
           KIH_LBA, kern_psectors, KIH_ENTRY, ksum);
    return 0;
}
