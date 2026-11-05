# KernOS disk layout (normative)

KernOS owns the whole boot disk. There are no partitions and no foreign
boot code. The layout below is the contract between the boot chain and
`tools/mkimage.c` (the layout constants are defined in one place each,
and the docs are the cross-check):

| LBA range      | Content                                   | Size      |
|----------------|-------------------------------------------|-----------|
| 0              | Stage 1 — our MBR (+ 0xAA55 at byte 510)  | 512 B     |
| 1 .. 62        | Stage 2 (flat binary, linked at 0x7E00)   | ≤ 31,744 B|
| 63             | **KIH v1 image header — the boot ABI**    | 512 B     |
| 64 .. 575      | Kernel flat binary at 0x100000            | ≤ 256 KiB |
| 576 ..         | Free — future filesystem (FAT32, presumptive M4 choice)    | —         |

## KIH v1: the image header (boot ABI, normative)

LBA 63 carries the versioned image header `mkimage` plants. It is the
protocol between the image builder and stage 2 — stage 2 trusts the
checksummed bytes, never its own layout guesses. Fixed constants in
stage 2 pin only the header's location and hard safety ceilings.

| Offset | Size | Field          | Value / rule                                    |
|--------|------|----------------|-------------------------------------------------|
| 0x00   | 4    | magic          | `KIH1` (LE u32 `0x3148494B`)                    |
| 0x04   | 1    | version        | 1                                               |
| 0x05   | 1    | hdr_sectors    | 1                                               |
| 0x06   | 2    | reserved       | 0                                               |
| 0x08   | 4    | kernel_lba     | first sector of the kernel slot (currently 64)  |
| 0x0C   | 4    | kernel_sectors | ≥ 1, ≤ 512 (slot ceiling; exact, not a guess)   |
| 0x10   | 4    | kernel_entry   | must equal the fixed flat load addr 0x100000    |
| 0x14   | 4    | flags          | 0                                               |
| 0x18   | 4    | reserved       | 0                                               |
| 0x1C   | 4    | hdr checksum   | additive: sum of ALL 128 header dwords == 0     |
| 0x20   | 4    | kernel checksum| additive u32 over kernel_sectors × 512 bytes    |
| 0x24.. | -    | (zeros)        | future fields live here, format grows downward  |

Normative rules:

1. **Checksums**: additive, 32-bit wrap-around, computed over raw
   little-endian dwords. `mkimage` derives the header checksum slot so
   the whole 512 B sector sums to 0; the kernel checksum spans the
   sector-padded image exactly as it sits on the disk.
2. **Validation is unconditional**: stage 2 checks magic, version,
   `kernel_entry`, `kernel_lba > 63`, `1 ≤ kernel_sectors ≤ 512` **and
   both checksums** before anything else loads or runs. Any failure is
   the `'X'` phase halt — a corrupt image fails with a named phase, not
   a mystery jump.
3. **Size passing replaces slot trusting**: stage 2 loads exactly
   `kernel_sectors` sectors from `kernel_lba` (the loader is chunked,
   partial last command included), and verifies the payload checksum
   after the load. The 256 KiB slot ceiling stays until the slot row
   itself is re-planned; the header makes the *used* size honest well
   before that.
4. The header occupies transient RAM at 0x1000–0x11FF in stage 2 (free
   per the map above, outside E820's 0x600–0x910); the region is dead
   after the load — it is not a handoff ABI.

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

## Boot ABI: E820 memory-map handoff (normative since stage 2)

Stage 2 captures the E820 SMAP via `int 15h` — the last BIOS data call
of the entire boot, made deliberately while interrupts are still on —
into low RAM before leaving real mode:

| Address        | Content                                                |
|----------------|--------------------------------------------------------|
| 0x0000-0x03FF  | BIOS IVT (dead, still mapped)                          |
| 0x0500-0x05FF  | free                                                   |
| **0x0600**     | **u64 entry count** (0 = capture failed)               |
| **0x0608**     | E820 entries, 24 B each: u64 base, u64 len, u32 type, u32 pad, max 32 |
| 0x0908-0x09FF  | free                                                   |
| **0x0A00**     | **fb handoff** — linear-framebuffer block, M5.6 (below) |
| 0x0A18-0x0BFF  | free                                                   |
| **0x0C00**     | stage-2 VBE ModeInfoBlock scratch (transient, dead past PM entry) |
| 0x0DFF         | (scratch runs 128 B: 0x0C00-0x0DFF)                     |
| **0x0E00**     | stage-2 VBE VbeInfoBlock scratch (transient, dead past PM entry) |
| 0x0FFF         | (scratch runs 512 B: 0x0E00-0x0FFF)                     |

The kernel reads this at fixed addresses (`kernel/arch/x86_64/mem.c`).
When the KernOS image header replaces the fixed kernel slot, the
pointer/struct form of this ABI moves into it; until then this table
plus docs is the single normative definition.

After the 64-bit kernel starts it zeroes its own .bss (including a
32 KiB stack at 0x101000-0x109000) and may reclaim everything below
0x100000 except the page tables.

## Boot ABI: linear-framebuffer handoff (normative since M5.6)

Stage 2, while the display BIOS/emulated card is still programmable,
attempts to establish a 32-bpp linear framebuffer graphic mode and
hands the accepted mode down at **0x0A00**:

| Offset | Field  | Meaning                                       |
|--------|--------|-----------------------------------------------|
| +0x00  | u32    | width in pixels                               |
| +0x04  | u32    | height in pixels                              |
| +0x08  | u32    | pitch in bytes per scanline                   |
| +0x0C  | u32    | bits per pixel (always 32)                    |
| +0x10  | u64    | physical base (LE; PCI BAR, sub-4-GiB)        |

**`width == 0` is the ABI's absence flag**: stage 2 pre-zeroes the
block and writes it only after a mode is set AND its geometry verified,
so a `width` of 0 downstream means "no LFB was offered/accepted" —
information, not failure. The kernel keeps the VGA text console in that
case; every consumer must branch on presence before touching pixels.
Absent is honest; a half-written or incoherent block (zero height,
zero pitch, bpp != 32, zero phys) is refused by the kernel and treated
as absence.

Stage 2's own strategy (implementation, not ABI): it first programs the
Bochs dispi registers (0x1CE/0x1CF) directly — register-level, no BIOS
— because QEMU's std VGA emulates exactly that and its BIOS mode list
offers only one 32-bpp LFB mode. Failing that it enumerates the card's
own VBE mode list (4F00/4F01) and keeps the largest 32-bpp LFB mode;
failing that it tries a small static candidate table. Every step
validates what the mode actually reports before it is handed down.

### COM1 phase sensors (boot chain, single ASCII chars)

The boot chain emits stage-1 gutters and phase letters on COM1:
`1 a j` (stage 1), then `U` (A20), `M` (E820), and since M5.6 `V`
(a 32-bpp LFB mode was set and verified — only when true), then `H`
(KIH valid), `L` (kernel loaded), `P` (page tables), `K` (kernel
jumped). `X` = stage2_fail (any named failure path); the machine halts.
Absence of `V` is a signal, not an error: the boot continues without a
framebuffer by design.

## The bench disk (second drive, M4.x gates) — normative fixture

The boot disk above never carries file-system state; storage gates run
against a **second drive** attached to the emulated AHCI HBA
(QEMU: `-device ich9-ahci,id=ahci -device ide-hd,drive=hd0,bus=ahci.0`).
It is planted by `tools/mkfat32` — deterministic, no clock, no
randomness — and every constant lives in **`include/kernel/bench.h`**,
the single source of truth shared by three consumers that cannot drift
apart: `tools/mkfat32.c` (planter), `kernel/main.c` (boot gates), and
`tests/test_fat32.c` (host unit suite).

`mkfat32` refuses to plant anything that violates its own invariants
(FAT32-valid cluster count, no marker/file collisions), so the fixture
self-audits at build time.

| Property         | Value                                            |
|------------------|--------------------------------------------------|
| Capacity         | 128 MiB = 262144 × 512 B                          |
| Geometry         | bytes/sector 512, spc 1, reserved 32, 2 FATs      |
| FAT size         | 2017 sectors (mkfat32 fixpoint: `want = ceil((clusters+2)*4/512)` re-derived from `fat_secs` until it stops moving, seeded 2048) |
| Data start       | LBA 4066 = cluster 2                              |
| Clusters         | 258078 ( > 65524: genuinely FAT32, not FAT16)     |
| Root directory   | cluster 2                                         |
| FSInfo/backup    | sector 1 / sector 6 (secondary structures planted)|

Root directory order is a deliberate parse obstacle course, in order:

| Slot | Entry                                            |
|------|--------------------------------------------------|
| 0..1 | LFN pair for VERYLONGNAME.TXT (tail first, then head, checksum, `0xFFFF` padding + `0x0000` terminator) |
| 2    | `VERYLO~1TXT` short entry of the LFN pair        |
| 3    | `0xE5` deleted-entry decoy (must be skipped)     |
| 4    | `KERNOS.TXT` (short 8.3)                         |
| 5    | `U1.ELF` (short 8.3, program fixtures start here: u1 chain first, then shell) |
| 6    | `SHELL.ELF` (short 8.3)                          |
| 7+   | `0x00` terminator                                |

File bodies: cluster 3 = `BENCH_SEED` (KERNOS.TXT, 47 B), cluster 4 =
`BENCH_LONG_BODY` (VERYLONGNAME.TXT, 24 B); the PROGRAMS start at
cluster 5 (u1 chain, then the shell chain — slots 5/6 above). Raw AHCI
markers sit in the
**unallocated data area**, asserted collision-free by the planter:
LBA 4106 (`BENCH_MARKER_LBA`) carries `KERNOSDISK\1\2\3` (read-only
match), LBA 4114 (`BENCH_WRITE_LBA`) is the write-verify target.

M4.3 FAT32 behavior contract (what the gates assert): mount validates a
real FAT32 BPB (spc power of two, cluster count in the FAT32 range);
lookup is 8.3 short-name, case-insensitive; LFN and deleted entries are
skipped (so the long name itself resolves to `-1` — intentional scope);
reads walk the cluster chain; writes overwrite in place with
`len <= size` only (no extension until the M5 userland needs it).

Because the boot gates and the host suite both **mutate** the fixture
(they write to it), both fixture images (`build/ahcidisk.img`,
`build/testdisk.img`) are regenerated on every `make benchdisk` /
`make hosttest` run — they are `.PHONY` so stale residue can never leak
into a pass. The full M4.3 gate evidence reads:

```
ahci: disk marker matches
ahci: disk write-verify ok
fat: mount ok data=0x0fe2 root=0x2 clusters=0x3f01e
fat: seed matches: KernOS M4.3 seed line planted by tools/mkfat32
fat: write-back verified
```
