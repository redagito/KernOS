# In-OS C development workflow

## Goal

Boot KernOS into a simple text editor and an independent console. Write C
source, save it to the FAT32 disk, compile it inside KernOS, run the resulting
ELF program, see output and compiler diagnostics, and edit and repeat. Saved
source must survive a VM restart. QEMU is the first acceptance platform;
real-hardware support remains a separate validation step.

## Implementation sequence

1. **Reproducible baseline and persistent disk.** Repair the launch setup to
   attach the AHCI filesystem disk. Separate destructive fixture generation
   from ordinary builds and runs so rebuilding cannot erase user source.
2. **Persistent user files.** Add bounded root-directory 8.3 file creation,
   replacement, growth, truncation, and loading. Expose explicit file calls to
   user programs without changing existing syscall numbers. Verify remount
   persistence, allocation, mirrored FATs, invalid names and failure paths.
3. **Independent terminal surfaces and reliable input.** Give editor and
   console separate row bands, cursors, and scrolling. Keep kernel diagnostics
   on serial after user surfaces start. Process pointer events in order and
   wake blocked readers on mouse input. Preserve the existing syscall ABI.
4. **Editor and command arguments.** Add bounded process arguments and an
   editor with insertion, deletion, navigation, scrolling, load/save, dirty
   state, and an explicit way to leave without silently losing edits. The
   console remains usable while the editor is open.
5. **KCC compiler.** Port TinyCC through its library interface, retain its
   license and source, document local changes, and supply the runtime,
   headers, memory budget, and ELF output needed by KernOS. Compilation and
   linking execute inside the guest; no host-side compiler proxy.
6. **Acceptance and documentation.** Automate the complete workflow through
   emulated keyboard/mouse input and serial assertions. Test a real C
   program, a compiler error followed by correction, output execution, and
   reopening the source after restarting against the same filesystem disk.

## Acceptance example

Write `HELLO.C` with `main`, a helper function or loop, and console output;
save it in the editor; compile to `HELLO.ELF`; launch it from the console;
observe its expected output and exit status. Repeat after editing. Introduce
a syntax error and confirm a diagnostic without a kernel crash. Restart the
VM without regenerating the filesystem and reopen the byte-exact source.

## Scope and constraints

- A basic two-pane interface is sufficient; overlapping movable windows,
  networking, USB, SMP, and self-compiling the kernel are not prerequisites.
- Start with 8.3 names and bounded resources, reporting limits to the user.
- Normal builds and launches must preserve user files. Reset is explicit.
- No screen or desktop capture. Acceptance uses serial and targeted VM state
  inspection; any future visual capture must directly isolate the VM app.
- Keep generated files and test artifacts in this repository; never use D:.

## Baseline (2026-11-06)

The existing seven host suites pass in `kernos-build:latest`. Docker provides
QEMU 7.2.22. The initial tree implements ELF loading and a shell but only
in-place FAT writes and no user file API or compiler. M6.5 focus routing is
present; scoped rendering is unfinished. Implementation status must be
reported from checks, not inferred from this plan.

## Delivered and verified (2026-11-06)

All six implementation steps above are implemented. Eleven host suites pass
207 checks. `tools/workflow-test.py` passes the full two-boot acceptance
sequence in Docker QEMU 7.2.22, with no screen capture:

- Type `TEST.C` through PS/2 into the userland editor and save it.
- Keep the editor open, click the console, compile `TEST.ELF` in the guest,
  and run it: `hello 42`, exit code 7.
- Introduce a syntax error, obtain a filename/line diagnostic and compiler
  exit 1, and confirm the preceding good executable still runs.
- Correct the source in the editor, save, compile again, and run again.
- Compile and run `MATH.C`, checking a loop, a struct, a variadic function,
  decimal and hex floating literals: `matrix 3 12 8`, exit code 0.
- Independently read the saved source from the FAT image and compare every
  byte; restart the VM with the same disk, reopen the source, and compare
  every saved byte again.

Logs are generated in `build/workflow-hosttest.log`,
`build/workflow-full-test.log`, `build/workflow-first.log`, and
`build/workflow-reboot.log`. Tests use `build/workflow-test.img`, a copy;
the user's development filesystem is `build/ahcidisk.img`. The original
pre-development fixture was backed up as `build/ahcidisk.before-files.img`
when its legacy shared program chains were replaced by valid independent
chains. New fixtures reserve raw benchmark sectors as bad clusters and
skip them during program allocation.

The interface is a fixed two-pane text interface, not movable windows.
One editor at a time is supported. Source is ASCII, at most 16,383 bytes,
with flat 8.3 names. Programs have a bounded 1920 KiB image span and a
64 KiB stack; KCC has a bounded 1 MiB heap. No dynamic linking, complete
standard library, user exception recovery, power-loss transaction journal,
or real-hardware validation is claimed.

### Physical mouse follow-up

Physical testing exposed an inverted Y axis: PS/2 is positive upwards,
whereas framebuffer coordinates are positive downwards. The old QMP helper
negated its injected Y to compensate, masking the driver error. The driver
now converts at the hardware boundary and the helper passes ordinary QMP
screen coordinates through unchanged. Eight host regression checks cover
raw PS/2 direction, all nine sign bits, and cursor clamping at all edges;
the complete two-pane workflow is rerun using unmodified QMP directions.
Windows launchers use GTK with GL disabled and expose its capture toggle,
Ctrl-Alt-G. A direct native test reproduced an SDL host access violation
(0xC0000005) before any guest serial output, both with and without GL.
The GTK configuration reaches the editor. The graphical launcher source
is retained under `scripts`, captures QEMU's error output, and reports
unsuccessful exits instead of silently disappearing.
