# KernOS

A hobby operating system for real hardware, written from its specs.
No GRUB, no tutorial code, no foreign bootloader: our MBR, our stage 2,
our kernel. The bottom half of the OS is dictated by the hardware; the
top half (ABI, libc, filesystem, shell, UI) is designed here, from a
blank page.

## Status

M1 complete in QEMU (real-hardware test pending): own boot chain
(MBR → stage 2 → x86-64) and a kernel
that comes up identity-mapped and talks over VGA text + serial.
Details and honesty in [docs/ROADMAP.md](docs/ROADMAP.md) and
[docs/disk-layout.md](docs/disk-layout.md).

## Building

Requires Docker Desktop (the container is the toolchain; see
[Dockerfile](Dockerfile)):

```powershell
scripts\build.ps1      # produces build/kernos.img
scripts\run.ps1        # boots it in QEMU (serial console in the terminal)
```

Real hardware: write `build/kernos.img` to a spare USB stick / spare
disk **that you are okay losing everything on** — KernOS owns the whole
disk and expects no partition table.

## Layout

```
boot/     stage 1 (MBR) and stage 2 (A20, PIC mask, protected mode,
          direct disk load, persistent page tables, long mode)
kernel/   kernel: start.s → main.cpp, console + serial drivers
include/  freestanding types
tools/    host-side image assembler (mkimage)
docs/     roadmap, disk layout, future ABI docs
```
