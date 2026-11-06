# KernOS

A hobby operating system for real hardware, written from its specs.
No GRUB, no tutorial code, no foreign bootloader: our MBR, our stage 2,
our kernel. The bottom half of the OS is dictated by the hardware; the
top half (ABI, libc, filesystem, shell, UI) is designed here, from a
blank page.

## Status

In-OS C development works in QEMU (real-hardware test pending): boot to
a userland text editor above an independent console, save C source on
FAT32, compile with KCC (TinyCC), and run the resulting ELF program.
The automated test types source through PS/2, checks output and exit
status, introduces and corrects an error, and restarts the VM to reopen
the saved source. It also exercises loops, structs, variadic functions,
and floating literals. See [the plan and acceptance evidence](docs/DEVELOPMENT-WORKFLOW.md).

Eleven host suites (`make hosttest`) currently pass 207 checks: fat32,
elf64, format, font, input, pmm, utf8, editor, terminal, and compiler
arena, and PS/2 mouse direction/cursor edges. The kernel includes scheduling, isolated process address spaces,
blocking syscalls, and eager x87/SSE context preservation.

## Building

Requires Docker Desktop (the container is the toolchain; see
[Dockerfile](Dockerfile)):

```powershell
scripts\build.ps1      # produces build/kernos.img
scripts\run.ps1        # opens native QEMU when installed; serial in terminal
scripts\run.ps1 -Headless  # QEMU inside Docker, without a graphical window
```

You can also double-click `build/KernOSLauncher.exe` in Explorer. The Windows
build script regenerates this launcher from `scripts/KernOSLauncher.cs`.
The graphical launchers use GTK with graphics acceleration disabled. The
installed Windows QEMU crashes in its SDL backend before the guest starts,
so SDL is not used. With the pointer over the VM, Ctrl-Alt-G toggles
mouse/keyboard capture so you can switch between KernOS and Windows.
Move up to move the guest pointer up; no inverted input setting is required.
The launcher records QEMU errors in `build/interactive-qemu.stderr` and
shows an error dialog if QEMU exits unsuccessfully.

The editor opens `HELLO.C` on startup. Type source and press **Ctrl-S**.
Click the lower console pane, then enter:

```text
cc HELLO.C
run HELLO
```

Click the editor to continue editing. Arrow keys, Home/End, Delete,
Backspace and Tab are supported. **Ctrl-Q** closes a saved editor;
unsaved edits require saving or explicit **Ctrl-X** discard. Close the
current editor before opening another with `edit OTHER.C`. Console
commands also include `cat FILE`, `echo TEXT`, `help`, and `exit`.

Source is ASCII with short 8.3 filenames and a 16,383-byte limit. KCC
accepts one source file plus an optional output filename, e.g.
`cc HELLO.C HELLO.ELF`. Only the supplied small libc is available;
general user malloc, a math library, and floating printf formats are
not yet provided. See [KCC's port notes](kcc/README.md).

`build/ahcidisk.img` is the persistent filesystem. Ordinary builds and
`make clean` preserve it; stop the VM before rebuilding. `make reset-disk`
explicitly erases and recreates it. Acceptance tests use their own copy.
The boot disk alone contains no user programs or source filesystem.
Real hardware is unverified and currently needs legacy IDE boot media,
a separate AHCI filesystem disk, and PS/2 input; a USB-only boot setup
is not supported by this boot chain.

Run all acceptance checks inside the build container:

```text
make hosttest
make image
python3 tools/workflow-test.py
```

## Layout

```
boot/       stage 1 (MBR) and stage 2 (A20, PIC remap, protected mode,
            direct disk load, persistent page tables, long mode)
kernel/     the kernel: start.s → main.c; scheduler + blocking
            syscall gate + procs (arch/x86_64), FAT32, ELF64 loader,
            input core; also the ELF users that ride the image
            (kernel/user: shell, editor, u1)
kcc/        TinyCC guest port, compatibility layer and build script
vendor/     TinyCC source and retained licensing notices
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
