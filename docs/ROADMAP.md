# KernOS roadmap

An operating system for real hardware, written from its specs. Our
code — kernel, bootloader, libc, drivers, userspace — is the product;
open-source dev tools are welcome under the licensing policy below,
and the endgoal is for the OS to develop itself further.

Decisions made 2026-11-01, in discussion with the author — recorded
here so future sessions don't re-litigate them. (Licensing and format
principles amended 2026-11-01, same day, after dogma friction.)

## Locked decisions

| Area        | Decision                                                                  |
|-------------|---------------------------------------------------------------------------|
| Arch        | x86-64                                                                    |
| Boot        | Own MBR → stage 2 → long mode. BIOS supplies the boot vector only (16-bit start); from stage 1's first instruction onward the chain drives the hardware directly (ATA PIO). No GRUB, no foreign bootloader (other third-party code is governed by the Licensing row). |
| Licensing   | (Amended 2026-11-01) Build our own OS — kernel, bootloader, libc,   |
|             | drivers, userspace. Open-source dev tools are allowed; a tool is    |
|             | judged by what it taints. Tools outside the artifact (gcc today):   |
|             | any OSI license, nothing of theirs enters our repo. Code that       |
|             | ships in the image (KCC/TinyCC fork from M5.5): permissive          |
|             | (MIT/BSD/Apache) preferred; weak copyleft (LGPL) allowed with       |
|             | obligations documented; GPL code is never copied into ours.         |
|             | Endgoal unchanged: the OS develops itself (M-Selfendgame).          |
| Hardware    | Older PC: SATA AHCI + PS/2 first (NVMe/xHCI/HDA later, no redesign needed)|
| First UI    | CLI: VGA text + serial console; add framebuffer (VBE) before M6           |
| Language    | C++17 freestanding on gcc now; userland C compiled in-OS from M5.5|
| Compiler    | (Amended 2026-11-01, supersedes own-compiler-from-scratch) KCC =   |
|             | our TinyCC fork (LGPL-2.1): use, extend, maintain in-tree. Source |
|             | obligations: publish fork sources, keep notices. Compiled output  |
|             | is license-free ours. Endgame = fork eats the kernel C.           |
| Formats     | (Adjunct 2026-11-01) Stick to free, openly documented formats       |
|             | wherever one fits: PSF2 bitmap fonts, FAT32 filesystem, UTF-8      |
|             | text, SysV AMD64 as the C ABI. We only define protocols where      |
|             | none exists — boot chain, syscall ABI — that boundary is this      |
|             | project's design work, not format invention. A KFS of our own      |
|             | is deferred, not dead: revisit when FAT32 demonstrably cannot      |
|             | carry what we need, or when self-hosting demands more.             |
| Honest skip | Wi-Fi, Bluetooth, GPU acceleration, suspend/lid — walls, documented below.|

## Execution plan (near term, 2026-11-01)

### Close M1
1. **Regression gate**: `make test` — boots the image in QEMU for ~6 s
   and asserts the exact COM1 byte sequence (`1ajULPK` + banner +
   vendor line); nonzero exit on any deviation. Cheap insurance before
   M2 churn makes boots flaky.
2. **Real-hardware boot attempt.** Media reality: our stage 2 loads the
   kernel off ports 0x1F0-0x1F7 — the target drive must be visible as
   legacy IDE. Rank: (a) spare SATA drive with the controller set to
   IDE/compatibility mode in firmware setup, (b) CF-IDE adapter,
   (c) M1.5 escape hatch: add an INT 13h fallback for the *kernel*
   load in stage 2 (documented impurity, one sensor names it),
   (d) pull an early AHCI-only driver forward (that is M4 work).
   A USB stick is the *wrong* first medium: it sits behind the USB host
   controller, invisible at 0x1F0.
   Likely first failures on metal: A20 (0x92), drive LBA28 quirks,
   timing loops tuned to QEMU speed. Sensors report which phase died.
3. **M1 exit criteria**: automatic test green in QEMU and kernel banner
   verified on real hardware on both consoles.

### M2 — interruptible, schedulable (order matters)
1. **Exception safety net first**: 64-bit IDT over 21 exception
   vectors with register/CR2/stack dumps over serial; own kernel GDT +
   TSS skeleton. First, because after this every later bug prints a
   dump instead of triple-faulting into a reboot loop.
2. **Timer**: PIT channel 0 via IRQ0; unmask selectively (PIC is being
   left as-is until the APIC is up); validate by sleeping in the
   kernel loop.
3. **PS/2 keyboard**: ports 0x60/0x64, IRQ1, scancode-set-1 decode →
   key events; first real input.
4. **Console v2**: hardware cursor via ports 0x3D4/0x3D5; serial getc
   so the CLI loop can already be exercised.
5. **MADT → APIC**: hand-parse ACPI MADT, bring up LAPIC + IOAPIC,
   re-target the timer off the PIC.
6. **Scheduler**: cooperative kernel threads first, then preemptive
   round-robin on the timer IRQ; atomics/spinlocks infra before SMP.
7. **SMP**: INIT-SIPI wake of the APs with a trampoline.
   Exit criteria: preemption between ≥2 kernel threads, clean dumps,
   keyboard interactivity — on real hardware.

### M3 — memory manager
0. **ABI v2 first**: stage 2 must collect the E820 memory map while it
   is still in 32-bit mode and hand it to the kernel at a fixed ABI
   slot; the physical allocator needs it and there is no way to get it
   post-facto (the BIOS is gone by then).
1. Physical page allocator on the E820 map, kernel virtual allocator,
   kernel slab cache for small objects.
2. 4 KiB fault-driven demand paging; move to a proper higher-half map,
   retire the fixed boot slots.

### M5.5 — the compiler fork (wait until M5's file I/O exists)
- Upstream: TinyCC (LGPL-2.1) — forked into the tree as KCC. Lives
  under the 2026-11-01 Licensing policy (weak-copyleft component in
  the image; obligations documented in the decision table). The fork
  can be studied and even started early on the host, but in-OS use
  needs M5's write/read syscalls, malloc and printf first.
- Port tactic: drive libtcc with memory buffers — source read from
  our FS into RAM, ELF64 object out — instead of porting tcc's host
  file/fork plumbing wholesale.
- Keep tcc's x86-64 SysV codegen as our C ABI; write docs/c-abi.md
  when the first syscall ABI lands (M5), so host cross-compiled
  programs and fork-compiled programs share one ABI from day one.

M4 (persistence), M5 (userland), M6 (GUI), M7 (network), M-Selfendgame
as listed below.

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
  PCI enumeration, AHCI driver (spec §ours), FAT32 (existing specced
  format; read/write) on the OS disk, `mkfs` host tool, boot-chain
  image header replacing the fat-image hack (that header is the boot
  ABI — a protocol, not a format invention; defined in
  docs/disk-layout.md).
- **M5 — Userland**
  Syscall ABI (defined in docs/syscall-abi.md when written), ELF64
  loader, process model, our libc, init + shell + utilities.
- **M5.5 — Compile C inside the OS** (TinyCC-fork path, locked 2026-11-01)
  KCC = our TinyCC fork (LGPL-2.1; fork policy and port surface
  described in the execution plan). Acceptance U2: type `hello.c` in
  an in-OS editor, compile with in-OS `kcc`, run from the shell.
  Residual wall: tcc's GNU-as support is partial, so the tiny boot
  `.s` files may keep binutils unless we someday add an own assembler.
- **M6 — GUI**
  Hand-drawn glyphs stored in PSF2 (standard console-font container,
  no custom format), framebuffer (VBE in BIOS boot), software
  compositor, window manager, terminal emulator on our API; text
  is UTF-8 from the start.
- **M7 — Network**
  Ethernet → ARP → IP → UDP → TCP from scratch on Intel e1000 or
  Realtek 8139/8168; text tools (netcat-a-like, smtp first, then http).
- **M-Selfendgame — self-host (reworded for the fork path)**
  Our TinyCC fork (KCC) extended until it compiles the kernel's C;
  rebuild the kernel with our fork and retire gcc from the build.
  The boot-chain assembly may keep binutils (see M5.5 residual wall);
  an own assembler for that last mile remains an optional dream,
  not a promise. Treated as an endgame, not a gate.

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
(`scripts/build.ps1`, `scripts/run.ps1`). Licensing policy (row above):
our source is written from specs; external tools stay in the container
and out of the repository, and anything that ships in the image (the
TinyCC fork from M5.5) is a separately licensed component with
documented obligations. The maximalist goal (M-Selfendgame) is
self-development: our fork (KCC) builds our kernel from within.
