# KernOS disk layout (normative)

KernOS owns the whole boot disk. There are no partitions and no foreign
boot code. The layout below is the contract between the boot chain and
`tools/mkimage.c` (the layout constants are defined in one place each,
and the docs are the cross-check):

| LBA range      | Content                                   | Size      |
|----------------|-------------------------------------------|-----------|
| 0              | Stage 1 — our MBR (+ 0xAA55 at byte 510)  | 512 B     |
| 1 .. 62        | Stage 2 (flat binary, linked at 0x7E00)   | ≤ 31,744 B|
| 63             | Reserved (zeros)                          | 512 B     |
| 64 .. 575      | Kernel flat binary at 0x100000            | ≤ 256 KiB |
| 576 ..         | Free — future FS (KFS) lives here         | —         |

## Why stage 2 fits in track 0

Stage 1 loads stage 2 with a direct ATA PIO LBA28 read (task-file ports
0x1F0-0x1F7 after the T13 discipline: device 0/LBA, bounded BSY waits,
ERR check). INT 13h extended read (AH=42 + DAP) stays resident in stage
1 as a fallback, but the primary path never relies on BIOS service
quirks — the boot chain's dependence on the BIOS ends with the reset
stack. Stage 2's 62-sector budget is enforced by `boot/stage2.ld`'s
ASSERT at link time and again by `mkimage` as a final check.

## Why the kernel slot is fixed at 256 KiB

Milestone M1 avoids passing image metadata between stages. When the
kernel binary approaches the slot (the kernel linker ASSERT trips at
0x180000, long before that), we replace the fixed slot with a proper
KernOS image header — module count, entry point, symbol blob for
future debugging. That header is on the M2 roadmap.

## Memory map during boot (low 1 MiB)

Stage 2 is single-shot and never returns to the BIOS: A20, mask both
PICs + nIEN, protected mode, a direct ATA PIO load of the kernel flat
binary straight to 0x100000 (`rep insw` past `ES:EDI` in real flat
mode), page tables, long mode, near jump.

| Range            | Use                                   |
|------------------|---------------------------------------|
| 0x0000-0x03FF    | BIOS IVT (unused by us, still mapped) |
| 0x0500-0x07BFF   | free                                  |
| 0x07C00-0x07DFF  | stage 1 (MBR)                         |
| 0x07E00-0xFFFF   | stage 2 (up to 62 sectors)            |
| 0x20000-0x23FFF  | page tables (PML4, PDPT, PD)          |
| 0x40000-0x77FFF  | stage-2 stack (top 0x78000, grows down)|
| 0x100000-0x17FFFF| kernel flat image                     |

There is no bounce buffer: the kernel is loaded at its final address.

After the 64-bit kernel starts it zeroes its own .bss (including a
32 KiB stack at 0x101000-0x109000) and may reclaim everything below
0x100000 except the page tables.
