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
| First UI    | CLI: VGA text + serial console; framebuffer landed in M5.6 (fb block at 0x0A00) |
| Language    | (Amended 2026-11-01) C11, freestanding, for kernel + userland      |
|             | alike; boot chain in assembly. Chosen so the tcc endgame is        |
|             | honest: tcc is a C compiler, and the kernel was already C-shaped   |
|             | C++ with no real C++ substance. Converted while it is tiny.        |
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
- **M4 — Persistence** *(complete in QEMU, pending real-hardware test;
  M4.1 PCI + M4.2 AHCI + M4.3 FAT32 host-testable via the block seam +
  M4.4 KIH v1 image header; mkfs = deterministic tools/mkfat32; see
  docs/disk-layout.md for the bench fixture and the header ABI)*
  PCI enumeration, AHCI driver (spec §ours), FAT32 (existing specced
  format; read/write) on the OS disk, `mkfs` host tool, boot-chain
  image header replacing the fat-image hack (that header is the boot
  ABI — a protocol, not a format invention; defined in
  docs/disk-layout.md).
- **M5 — Userland** *(in progress; M5.1 landed — ABI v0: gate
  machinery, probe/getpid/write, substrate-resumability invariants
  in docs; M5.2 landed — CPL3 process model: per-process CR3, first
  userlands speaking the ABI, exit as the first non-returning syscall;
  M5.3 landed — ELF64 loader: userland comes from the FAT disk via
  the portable elf64_plan module + host tests, nothing user-related
  inside the kernel image; M5.4 landed — our libc: crt0/printf/
  string/exit over the gate, vsnprintf core host-tested; M5.5
  landed — concurrency + input + the first OS-run init: ABI numbers
  4 read / 5 spawn / 6 wait (append-only), IRQ1 keyboard ring feeding
  blocking READ via wait queues (SCHED_WQ_KBD/SCHED_WQ_CHILD),
  main.c boots SHELL.ELF off the FAT and JOINS its tid through the
  kernel-side sched_join (gate-typed shell round-trip `run u1` →
  spawn → wait → `exit` proven on real IRQs by a committed driver,
  tools/ksh-driver.ps1; negative control = corrupted shell magic →
  the named refusal pair with no prompt))*
  Syscall ABI (docs/syscall-abi.md — normative since M5.1), ELF64
  loader, process model, our libc, init + shell + utilities.
- **M5.5 — Compile C inside the OS** (TinyCC-fork path, locked 2026-11-01)
  KCC = our TinyCC fork (LGPL-2.1; fork policy and port surface
  described in the execution plan). Acceptance U2: type `hello.c` in
  an in-OS editor, compile with in-OS `kcc`, run from the shell.
  Residual wall: tcc's GNU-as support is partial, so the tiny boot
  `.s` files may keep binutils unless we someday add an own assembler.
- **M5.6 — the framebuffer boots (locked 2026-11-03)**
  Stage 2 establishes a 32-bpp linear framebuffer before leaving real
  mode: primary = Bochs dispi registers (0x1CE/0x1CF, register-level,
  no BIOS — QEMU std VGA's own BIOS list offers only one 32-bpp LFB
  mode) with the LFB base taken from the display device's PCI BAR0;
  fallback = VBE 4F00/4F01 enumeration keeping the largest validated
  32-bpp LFB mode; then a static candidate table. Handoff = the fb
  block at 0x0A00 (docs/disk-layout.md normative); width==0 is honest
  absence and the kernel keeps the text console (negative control
  `-vga none`). Kernel side: fb.c parses the handoff before pmm_init,
  maps via page_map_at at 0x50000000, gate = five-point pixel echo
  (`fb: pixel echo ok`). Milestone bug fixed on the way: the ATA
  reader's `rep insw`/bounded-wait counters used 16-bit movw on CX,
  which in 32-bit mode kept garbage in the upper half (the PCI walk
  leaves a slot address there) and scribbled the whole 4 GiB instead
  of one sector — now full-ECX loads.
- **M5.7 — the framebuffer console (locked 2026-11-03)**
  The console moves from BIOS text cells to real pixels: the kernel
  embeds a PSF2 font blob (tools/mkfont.c — hand-drawn 8x16 cells,
  PSF2 = the standard console-font container, host-validated by
  test_font before any boot depends on it) and kernel/arch/x86_64/
  fbcon.c validates the header, blits cells and hard-scrolls through
  the M5.6 LFB only (absence keeps the VGA text console, named).
  Gates: scroll-echo (bottom-row RGB stamps + two pure-newline
  scrolls read back 16*k pixels higher) and the banner ink count,
  plus the first SCREEN evidence — a QEMU QMP screendump
  (tools/qmp-dump.py) of the rendered banner reviewed by eye.
  Why embedded and not a FAT file: the console must print before and
  independent of the storage stack, exactly like serial/VGA did.
- **M5.8 — the PS/2 mouse (locked 2026-11-04)**
  The input pair completes: kernel/arch/x86_64/mouse.c arms the
  i8042 aux channel (0xA8, config IRQ12 on + aux clock live +
  translation off, 0xF4 with the 0xFA ack consumed synchronously
  BEFORE IRQ12 unmasked - no ack is the named "aux refused"). The
  IRQ handler consumes only aux-origin bytes (status bit 5), feeds a
  3-byte state machine with bit-3 sync, drops overflow packets into
  a counter (lost motion is named, never decoded as seen), and
  decodes 9-bit signed deltas; consumers read settled totals only,
  the per-event API waits for the M6 input layer. kbd.c gains an aux
  guard so IRQ1 never steals mouse bytes. Gate evidence: QMP
  injection (tools/qmp-mouse.py) of relative moves while the gate
  window runs -> "mouse: gate ok events=N dx=+\226 ...\ dy=...\";
  no injection -> the positive-shaped "mouse: idle - no motion
  (negative control ok)". Real bug found by the gate: irq_enable()
  never unmasked the master PIC's cascade line, so IRQ12 was the
  FIRST interrupt a slave line could not deliver - fixed with the
  unmask. Note on axes: PS/2 dy is up-positive on the wire, so an
  injected screen-down motion arrives as +dy; the wire is the truth
  the driver reports.
- **M5.9 — one console, a visible face from byte one (locked 2026-11-04)**
  kernel/arch/x86_64/console.c|h becomes the single boot log: every
  kernel print (main, sched, AHCI, PCI, traps, syscalls, the user
  shell echo) reaches serial ALWAYS (the evidence channel the gates
  read) plus whatever face the machine has: VGA text from the first
  byte, and at the M5.7 fbcon gate the LFB console takes over — the
  LFB is cleared and the log continues mid-stream (the switch line
  names the split honestly: earlier lines live on text). Console
  events now also land there, so the user shell echo is visible.
  Gates: the M5.7 banner proof evolves into the mirror gate —
  "console: mirror ok face=fbcon ink=966 mirrored=1150" (ink = the
  switch line's black-on-pixels; mirrored = bytes carried across the
  switch) — with the scroll-echo gate unchanged, and the SCREEN
  evidence matured: the screendump now shows the WHOLE log tail
  (PCI/AHCI/FAT/sched/syscall/ELF/gate5) as rendered glyphs.
  Negative control (-vga none): named fb/fbcon absences, the console
  stays on the text face, rhythm shows no V, boot completes. Full
  -B build also surfaced three latent type bugs (first triggered by
  the cross-file rename forcing full recompiles): swtch's save-slot
  is u64** not u64*, idt.c's stub table is u64[] not void*[], and
  mem.c's 0x600 E820 read needed the fixed-address read discipline
  (register barrier, same family as fb.c's rd_le32).
- **M6.1 — the per-event input channel (locked 2026-11-04)**
  kernel/input.c|h is the event core: ONE ring both input devices
  feed, shaped for userland like fat32/elf64 (the portable center of
  gravity — host tests run the same text the IRQ handlers and the
  gate run). Records are fixed 8-byte {type, code, value, seq} with
  types frozen (1=KEY post-translation ASCII, 2=RELX / 3=RELY one
  signed delta each — one PS/2 packet lands as two ordered events,
  4=BTN on transitions only); seq is a kernel-owned monotonic
  delivery number and a full ring's drops are COUNTED
  (input_dropped), never smuggled into seq. Serialisation is the
  kbd-ring discipline since M5.5: producers (IRQ1/IRQ12) and the
  consumer (syscall gate) all run IF-off on one core. Syscall nr 7
  `input(buf, max)` is NONBLOCKING by contract (0 = nothing right
  now, batch cap 64 like read's 64 bytes); a blocking variant, if
  the GUI ever needs one, arrives as number 8+ per the append-only
  ABI rule — the docs/syscall-abi.md table grows the row and the
  record shape is normative there. Producers hook into the kbd
  (every translated keystroke feeds BOTH the fd-0 text ring and the
  event core) and the mouse (per-packet REL pair, BTN transitions
  with prev-state tracking); libc grows input(); u1 grows an
  observatory phase (the typed gate): positive = 20 QMP moves
  (15,-7) staged into the ring BEFORE the spawn, drained on the
  FIRST poll -> "input: gate ok rx=60 ry=28 keys=0 btn=0 poll=1"
  (one 8-event batch = 4 packets; the 12 typed keys arrive after
  and are counted, not gated); negative = no injection -> "input:
  idle - no rel motion after 300000 polls (negative control ok)"
  (~1 s of burn, CAP stays) — exit 0 both ways, and the M5.5 shell
  round-trip rides unchanged on the same `run u1` typing. Boot-end
  counters: "input: events=52 dropped=0" (40 rel + 12 keys) and
  "input: events=12 dropped=0" (typed keys only) — drops named at
  zero, as they should be. Host suite: 108 checks / 0 failures
  (input suite 22: exact drain order, batch cap, ring wraparound,
  drop accounting at the 2^7-1 boundary, seq monotonic across a
  flood, the frozen record shape). Two real catches in this
  milestone: (1) the FIRST gate that types since M5.8 caught a
  latent M5.8 regression — mouse init cleared the i8042 config bit
  6, but translation applies to the KEYBOARD port only, so QEMU's
  native scan set 3 leaked through (`sendkey q` echoed `yy`) and
  typing was garbage; mouse init now leaves bit 6 exactly as the
  firmware set it, rationale in the comment. (2) The gate
  discipline itself caught a build-graph lie: `make image` never
  built the FAT fixture (it is the separate phony benchdisk), so
  the SAME artifacts kept booting a pre-M6.1 userland whose exit 0
  faked a green run — `image` now depends on the fixture, and
  every build replants it fresh from the current u1/shell ELFs.
  Hardening the same class into the gate itself: test_fat32 reads
  the planted programs back through the module and must match the
  build's u1/shell byte-for-byte (the 2 checks that made it 30 —
  a fixture planted from another build is a HOST failure, before
  any QEMU), and the typing driver grew -RequireCues: a run only
  exits green when the milestone's OWN evidence lines exist on
  serial; the round-trip cues alone (which a pre-phase u1
  satisfied perfectly) are decoration, proven by the meta-negative
  (a no-injection boot asked for the positive cue exits red, exit
  1). Stated plainly, for every future gate: the artifact chain is
  owned by the build graph; evidence identity is owned by REQUIRED
  named cues and byte-exact binds — never by the absence of a
  failure.
- **M5.10 — the repair milestone: the blind review's confirmed findings
  (locked 2026-11-05)**
  An independent reviewer read the whole tree cold (no context beyond
  the sources, docker for the host suite only); every High/proven
  claim was then re-verified line-by-line before acting, and one
  verdict was corrected (F1's "own-wake hang" — the old park loop was
  one-shot, the real defect was the stale in-place return). What
  shipped, one gated commit each:
  1. sched_block rewritten (512f024): T_WAITING now means "not on any
     CPU" in a provable way — the park loop is real (rescan both exits
     at every un-hlt), own-wake resumes in place and HEALS the record
     (old code returned in place still tagged T_WAITING with the waitq
     armed), both resume paths clear the spent wake. The no-peers
     iteration is yet unreachable (main stays the idle carrier) —
     stated as a named absence, not a claim.
  2. The spawn front door (5744e6e): name[13] with real 8.1 rules —
     suffix names valid iff the stem fits 8, bare stems suffixed iff
     they fit 8, everything else REFUSED (the old 7-char copy made
     `run shell.elf` look up SHELL.E.ELF and refuse-lied; 8-char stems
     silently truncated); printf clamps vsnprintf's would-be length to
     cap-1 (no stack bytes, no stray NUL, on the wire); failed spawns
     release every ledger frame via frames_release at all five failure
     points (the "NOTHING left behind" contract was aspirational).
     Discriminating probe: `run shell.elf` typed key-by-key now spawns
     a second shell (pre-fix: refusal); typed `run u1.elf` rides the
     driver's new -Run probe freedom with RequireCues.
  3. stage2 zeroes the boot-table tails (402b175): PML4[1..511] and
     PDPT[1..511] are now defined-zero, not RAM-WAS; a named kernel
     check ("page: boot tables clean") holds stage2 to it on every
     boot, so a kernel booted without the loop names the regression.
     This was the QEMU-zeroed-RAM class: user CR3s inherit all 512
     entries and user_ptr_ok walks them.
  4. Dead-code sweep (9735c8f): .userprog linker recipe, kbd_getc,
     ahci_info, pci_find_class, console_on_fbcon, fb_bpp, the
     orphaned tools/ppmtext.c, a duplicated Makefile define and a
     duplicated comment — 88 deletions, zero behavior change
     (kchk moved because the link script did, which is the proof).
  5. Input packets are atomic pairs (05f9b67): a mouse packet is
     refused AS A UNIT at a one-slot boundary (dropped += 2), never
     emitted as a half pair; two discriminator tests drive it to the
     exact boundary on host. The gate found a REAL harness lie doing
     it: Makefile's hosttest ran test_input/test_font as recipe
     commands WITHOUT prerequisites — the input suite had been
     executing a stale Nov 4 binary all milestone. The honest ledger
     is 112 checks / 0 failures (30/24/21/12/25), not the "108" the
     stale binary kept printing.
  6. Docs truth (this commit): disk-layout cross-check repaired —
     bench LBAs 4106/4114 (doc said 4076/4084, an M5.3 +/-6 carve-out
     that never propagated), the root-dir table now lists the program
     slots (5=U1.ELF, 6=SHELL.ELF, 7=terminator — "5+ 0x00" was
     stale) and the FAT-size line describes mkfat32's actual fixpoint
     shape; serial_init takes divisor 1 (115200) to MATCH the boot
     chain (the old divisor 3 = 38400 was invisible to every gate,
     QEMU ignores the divisor; stage2 already said "like the kernel
     will do" while the kernel did not).
  Recorded unchanged, by design: F3 (a user #PF dump-and-parks the
  machine) is deferred to its OWN next milestone — user fault kills
  the process, the machine lives; it needs a kill path plus negative
  controls, and M6's GUI userland wants it first (Gates will provide
  the halting evidence). F9 (exit ledger keyed by reusable tid) stays
  logged: cannot fire until slots recycle under an uncollected child;
  the collect path will re-key by (tid, generation) when a real
  orphan producer exists. The reviewer's structural verdict stands
  and becomes doctrine: the scheduler's wake/park code is the least-
  defended region of the tree — its state machine (record vs CPU)
  gets written down BEFORE M6 adds threads to GUI.
- **M5.11 — the second blind review's findings, gated one per commit
  (locked 2026-11-05)**
  Same protocol as M5.10: independent cold read of the tree, every
  claim re-verified at the exact lines before acting. Ceiling note:
  the round's headline High finding (H1, exit-ledger slot reuse) is
  a REIFICATION of round-one F9 — with six threads and slots
  recycling on the fixture boot, the round-one "can't fire yet" call
  was wrong; the review supplied the concrete mechanism (collect
  keys on `threads[i].ppid` in a claimed-over slot).
  1. vsnprintf answers its own contract (a051ec7): the `cap==0`
     early-return-0 LIE was hiding emit_char's `cap-1` size_t
     underflow; fixed at the overflow site (cap>1 guard, cap==1
     parks the NUL, cap==0 formats-into-NULL and still counts).
     The suite NEVER caught it because host builds let gcc fold the
     test's calls into its builtin - so HOST_CFLAGS gains
     -fno-builtin (the suite must test OUR code) and test rules take
     `$(MAKEFILE_LIST)` as prerequisite (a Makefile flag change must
     rebuild the test binaries: the second costume of the
     stale-binary lie). Ledger discipline: a va_list is an array;
     it cannot be "passed as the value 5" (SEGV at 0x5 proved it) -
     contract probes ride variadic helpers.
  2. The frame free is restricted to what the universe held
     (5085e41): pmm_init pushed the initial stack WITHOUT the
     in_stack bits (the O(1) double-free guard was blind for the
     whole initial stack; a duplicate push later aliases one frame
     for two callers); a hole frame (never in a type-1 run) was
     adoptable; a free past the universe indexed PAST the bitmap
     (ASan SEGV at pmm.c:82). New: in_universe[] membership +
     init bits on push + idx bounds in free AND in init (a sparse
     E820 map walks idx past the capacity while top is small - found
     WHILE writing the suite). NEW host suite test_pmm (16 checks;
     red: 5 failures + the SEGV).
  3. The spawn gate refuses what it would truncate (70bf566): 13+
     char names resolved their silent 12-char prefix - which is a
     DIFFERENT 8.3-shaped program name ("abcdefgh.elfz" ->
     "abcdefgh.elf"). The gate now looks one source byte ahead
     (user_ptr_ok re-checked) and refuses with a NEW error code:
     SYS_ENAME (-2) = "shape refusal", keeping -1 for missing
     file/loader; the shell reports "bad program name" vs "no such
     program" instead of one hollow lie. Typed red: the old
     "run refused (abcdefgh.elfz)" line (kernel resolved a name the
     user never said); typed green: kernel's named line
     "spawn: name refused (too long)" + correct split.
  4. Slot reuse forfeits the uncollected exit record (a8ede72):
     named ledger_reclaim() called at BOTH spawn publishes; reuse
     is wait(2)-style forfeit. Boot check ("sched: ledger reuse
     ok at claim") fabricates a dead child's record in the next
     claim's slot and spawns a real kernel thread to catch spawn
     paths that skip the reclaim: red on the pre-fix image (FAIL
     line), green after. Round-one F9 is hereby closed by this
     mechanism, not by "can't fire yet".
  5. Link-time and fixture honesty (this commit): prog.ld ASSERT
     counts .bss in the 32 KiB user span (red: 34000 B .bss linked
     span 0x8510 > 0x8000 was refused only at RUN time); mkfat32
     plants the FULL u32 dir size (red: 261-byte seed planted
     size 5, 301-byte body planted 45 - (u8) truncation under a
     <=512 bound that said "ok") + planted-size read-back
     verification; README Status/Layout stopped saying "M1".
  Suspended, still not fixed (recorded by both rounds, low
  severity + priced): L1/L3/L4. Host ledger at M5.11: 130 checks /
  0 failures (fat32 30, elf64 24, format 23, font 12, input 25,
  pmm 16), every suite rebuilt under -fno-builtin discipline.
- **M5.12 — the third blind review: one fix per finding, and one
  finding BEATEN by experiment (locked 2026-11-05)**
  Round three's headline was ABSENCE: no reachable High/Medium
  defect at b44008d. All four acting findings were verified at their
  exact lines before touching anything; two were Low-latent, two
  were evidence-honesty. One per commit, as always:
  1. The format loop stops AT the terminator (2a5ebb6): a bare
     trailing % or flag-without-verb made the unconditional tail
     fmt++ step over the string's NUL and rescan neighboring rodata
     (no in-tree trigger - every caller passes literals - pinned
     anyway, as the libc's most reused string). Red in-bounds: two
     suite checks plant a sentinel AFTER the NUL inside the
     caller's array (format 23 -> 25); green: the tail step is
     conditional (if (*fmt) fmt++), 132 checks / 0 failures.
  2. The AHCI read-test owns its frame (bdd2fb2): pmm exhaustion
     would have DMA'd 8 boot sectors to PA 0 (ahci_read only checks
     alignment), and the failure branch leaked the frame. Guard +
     symmetric free; no boot-red exists (exhaustion is unreachable
     with ~32k free frames at that site), stated as such; gate
     green with byte-identical read-test output.
  3. F-3 lost to an experiment, F-4 went into the code (37cf43b):
     the reviewer claimed inw's EAX[31:16] residue survives the
     andl $0xFFF8 id-compare and can silently drop a live dispi
     into the BIOS fallback. WRONG - the mask is an imm32 AND that
     zero-extends to 0x0000FFF8, clearing the upper word itself;
     a scratch build with the residue worst-case injected (id
     rescued out of AX, 0xBAC00000 loaded, restored) STILL took the
     primary path. No fix shipped; not even a defensive movzwl,
     whose comment would have stated a proven-false hazard. What
     DID ship: the dispi path reads XRES/YRES/BPP back after
     ENABLE and only hands off when the card echoes the request
     (refusal: park disabled, emit 'r', take the BIOS path) - the
     doc's "set and verified" is now literally true on both paths;
     new letters d/e (engaged / enumeration) make the dispi-vs-BIOS
     choice visible in the serial stream for the first time.
  4. Every thread record owns its name (f46e8f9): the one static
     prog_name_store every spawned record pointed into was rewritten
     by the next spawn (safe only under the serialized gate);
     namebuf[17] + set_name() gives each record an owned bound copy,
     "main" included. No red (needs overlapping spawns - the
     serialization makes it unreachable today), said honestly. Plus
     the elf64.h comment naming the real function (elf64_errname).
  Gate lesson in the ledger: a guessed cue ("u1: pid=8 tick=2";
  pid was 4) pins ksh-driver's ORDERED cue wait against its
  MaxWaitBootSec floor - require-cues are copied from observed
  serial identities, never constructed. Host ledger at M5.12:
  132 checks / 0 failures (format grew to 25).
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

### The emulator bench (in addition to QEMU — amended 2026-11-01)

One boot artifact, several independent witnesses. Every emulator runs
the identical smoke boot (timeout, `-serial file:`) and the consoles'
byte-exact sensor comparison is the same read. Divergences between
emulators are the honest early warning for real-hardware bring-up.

- **QEMU (default)** — fast, in-container; the day-to-day loop.
- **Bochs (in-container)** — independent, reference-grade CPU and
  device models, slow on purpose; refined logging + internal debugger
  catch what QEMU's forgiving hardware hides (undocumented port
  behavior, exact MSR/ACPI quirks).
- **VirtualBox / Hyper-V (host tools)** — different firmware vendors
  and device models; headless via VBoxManage; Hyper-V Gen1 from the
  same raw image via `qemu-img convert` to VHD.
- **86Box / PCem (host, optional)** — retro-machine fidelity for the
  "older PC" target (era BIOSes/chipsets). Their ROM images are
  proprietary firmware dumps: tools-outside-artifact per the
  Licensing row; for our test bench only, never redistributed.
