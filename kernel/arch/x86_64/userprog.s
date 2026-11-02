/*
 * Embedder for userland blobs (M5.2).
 *
 * The assembled user program is objcopy'd to a flat binary and
 * included here as raw .rodata between two symbols the process model
 * melts into fresh frames at spawn time (proc_build_space). When the
 * ELF64 loader lands (M5.3) the kernel stops carrying pre-built blobs
 * and loads programs from the FAT32 fixture instead; until then this
 * is the only way userland exists.
 */

    .section .userprog, "a"
    .global _userprog_start
_userprog_start:
    .incbin "build/user1.bin"
    .global _userprog_end
_userprog_end:

    .section .note.GNU-stack,"",@progbits
