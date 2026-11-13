# KernOS

A hobby x86-64 operating system written in C and assembly, with its own
BIOS bootloader, kernel, drivers and userland.

KernOS has a graphical desktop, a Notepad-style editor and a console.
Write C, save it to disk, compile it with the TinyCC-based KCC compiler,
and run the result—all inside the OS.

![KernOS desktop with Notepad and Console running in QEMU](docs/images/kernos-clean-desktop.png)

Tested in QEMU; real-hardware support remains unverified.

## Build and run

Windows requires LLVM, Visual Studio C build tools and QEMU.

```powershell
scripts\build-native.ps1
scripts\run.ps1
```

A [Docker build](Dockerfile) is also available through `scripts\build.ps1`.

## Documentation

- [Desktop and current limits](docs/DESKTOP.md)
- [C development workflow](docs/DEVELOPMENT-WORKFLOW.md)
- [Roadmap](docs/ROADMAP.md)
- [Compiler and licensing notes](kcc/README.md)
