#pragma once

/*
 * Boot self-test fixture contract.
 *
 * These constants are the SINGLE SOURCE OF TRUTH shared by:
 *   - kernel/main.c        (in-boot self-test gates; serial evidence)
 *   - tools/mkfat32.c      (deterministic fixture image planter;
 *                           also self-checks that its layout never
 *                           collides with the raw AHCI markers)
 *   - tests/test_fat32.c   (host unit tests over the same constants)
 *
 * The bench disk is a 128 MiB FAT32 (262144 sectors, spc=1, so the
 * cluster count is FAT32-valid). Raw-lba AHCI markers live in the DATA
 * area, beyond every cluster the fixture files allocate - mkfat32
 * asserts that and refuses to build a colliding image.
 *
 * NOTE: any change here must be re-planted: rebuild the fixture images
 * (make benchdisk / make hosttest regenerate them).
 */

/* raw AHCI tests (block markers, below the file system) */
#define BENCH_DISK_SECTORS 262144u       /* 128 MiB / 512              */
#define BENCH_MARKER_LBA   4076u         /* 'KERNOSDISK\1\2\3', read   */
#define BENCH_WRITE_LBA    4084u         /* write-verify target        */
#define BENCH_MARKER_LEN   13u

/* FAT32 fixture files at the root directory */
#define BENCH_FILE        "KERNOS.TXT"
#define BENCH_SEED        "KernOS M4.3 seed line planted by tools/mkfat32\n"
#define BENCH_WRITE       "KernOS M4.3 seed line rewritten by the kernel\n"

#define BENCH_LONG_FILE   "VERYLONGNAME.TXT"   /* forces LFN entries   */
#define BENCH_LONG_BODY   "long filename file body\n"
