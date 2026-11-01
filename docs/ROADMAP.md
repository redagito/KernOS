# KernOS roadmap

An operating system for real hardware, written from its specs, with no
third-party code. Decisions made 2026-11-01, in discussion with the
author — recorded here so future sessions don't re-litigate them.

## Locked decisions

| Area        | Decision                                                                  |
|-------------|---------------------------------------------------------------------------|
| Arch        | x86-64                                                                    |
| Boot        | Own MBR → stage 2 → long mode. BIOS supplies the boot vector only (16-bit start); from stage 1's first instruction onward the chain drives the hardware directly (ATA PIO). No GRUB. No foreign code. |
| Hardware    | Older PC: SATA AHCI + PS/2 first (NVMe/xHCI/HDA later, no redesign needed)|
| First UI    | CLI: VGA text + serial console; add framebuffer (VBE) before M6           |
| Language    | C++17 freestanding on gcc now; own assembler + compiler later (self-host) |
| Honest skip | Wi-Fi, Bluetooth, GPU acceleration, suspend/lid — walls, documented below.|

## Milestones

- **M1 — Boots on bare metal (complete pending real-hardware test)**
  Own MBR + stage 2 bring the kernel up in long mode with an identity
  map of the first 1 GiB; the kernel clears the VGA text screen, prints
  a banner and the CPUID vendor to VGA text and serial (16550 at COM1);
  COM1 carries phase sensors named for the boot stage that emitted
  them, on QEMU and real hardware alike. Built with the containerized
  gcc toolchain, tested in QEMU, then on real hardware via `dd`.
- **M2 — Interruptible, schedulable**
  Proper kernel GDT/IDT/TSS, APIC (LAPIC + IOAPIC) with PIC fallback,
  PIT + HPET, PS/2 keyboard, exception dumps, a scheduler with SMP
  wake via INIT-SIPI (ACPI MADT parsed by hand).
- **M3 — Memory manager**
  Physical page allocator, kernel virtual allocator, slab-ish object
  cache, 4 KiB page fault handling; kernel virtual map with a proper
  higher-half (no more fixed boot slots).
- **M4 — Persistence**
  PCI enumeration, AHCI driver (spec §ours), KFS filesystem designed
  on a blank page, `mkfs` host tool, replace the fat-image hack with a
  kernel-image header on disk.
- **M5 — Userland**
  Syscall ABI (defined in docs/syscall-abi.md when written), ELF64
  loader, process model, our libc, init + shell + utilities.
- **M6 — GUI**
  Hand-drawn bitmap font, framebuffer (VBE in BIOS boot), software
  compositor, window manager, terminal emulator on our API.
- **M7 — Network**
  Ethernet → ARP → IP → UDP → TCP from scratch on Intel e1000 or
  Realtek 8139/8168; text tools (netcat-a-like, smtp first, then http).
- **M-Selfendgame — self-host**
  Own assembler, then own C compiler; rebuild the kernel with it.
  Treated as an endgame, not a gate.

## The walls (recorded so nobody rediscovers them the hard way)

- **Wi-Fi/Bluetooth:** firmware blobs live on the cards; we can't and
  won't write them. Ethernet is the net story.
- **GPU acceleration:** none. Software rendering forever.
- **Full ACPI:** table parsing (M2) is costed; an AML interpreter for
  runtime events (suspend, lid, thermal) is its own sub-project and is
  not scheduled.
- **USB 3 (xHCI):** doable, biggest driver in the plan, scheduled after
  M4 only if the target hardware needs it.
- **Browsers:** not happening from scratch, denied at planning time.

## Build environment

Toolchain = gcc/binutils/make/QEMU inside a Docker container
(`scripts/build.ps1`, `scripts/run.ps1`). The compiler is the one
accepted third-party artifact; every byte shipping in the OS image is
ours. The maximalist goal (M-Selfendgame) is to eventually remove even
that by rebuilding with our own compiler on the OS itself.
