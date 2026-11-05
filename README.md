# KernOS

A hobby operating system for real hardware, written from its specs.
No GRUB, no tutorial code, no foreign bootloader: our MBR, our stage 2,
our kernel. The bottom half of the OS is dictated by the hardware; the
top half (ABI, libc, filesystem, shell, UI) is designed here, from a
blank page.

## Status

Boot-to-shell, in QEMU (real-hardware test pending): our boot chain
(MBR → stage 2 → x86-64), a kernel with preemptive scheduler +
blocking syscalls, FAT32, a shell, a printf family, malloc, and GUI
hardware input (keyboard + mouse) delivered to userland through a
typed event ABI. Milestone ledger and honesty about what is NOT
proven live in [docs/ROADMAP.md](docs/ROADMAP.md) and
[docs/disk-layout.md](docs/disk-layout.md).

Seven host suites (make hosttest) answer 157 checks for the pieces
that can answer on their own: fat32, elf64, format, font, input,
pmm, utf8; everything else is gated at boot by named serial checks.

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
boot/       stage 1 (MBR) and stage 2 (A20, PIC remap, protected mode,
            direct disk load, persistent page tables, long mode)
kernel/     the kernel: start.s → main.c; scheduler + blocking
            syscall gate + procs (arch/x86_64), FAT32, ELF64 loader,
            input core; also the ELF users that ride the image
            (kernel/user: shell, u1)
libc/       the userland libc: printf family (format.c host-tested),
            string, unistd (the syscall ABI as C)
obj/…       object/output paths under build/ (created by scripts)
include/    freestanding kernel types + host/test headers
tools/      host-side image assembler (mkimage), filesystem fixture
            builder (mkfat32), font packer (mkfont), QEMU gate
            driver, QMP input injectors, boot fraud self-checks
tests/      the host suites (make hosttest): fat32, elf64, format,
            font, input, pmm - each compiles the kernel's own module
docs/       roadmap, disk layout, syscall ABI, future ABI docs
```
