# KernOS build container.
#
# Honesty note: this container holds the *toolchain* (gcc, binutils,
# make, QEMU for the run loop). "No third-party software as much as
# feasible" is about the OS code and its dependencies. The kernel,
# bootloader and drivers are ours. The in-OS KCC compiler vendors TinyCC
# with its LGPL notices and source; see kcc/README.md for the port,
# exclusions and rebuild requirements.

FROM gcc:13

RUN apt-get update && \
    apt-get install -y --no-install-recommends qemu-system-x86 python3 && \
    rm -rf /var/lib/apt/lists/*
