# KernOS build container.
#
# Honesty note: this container holds the *toolchain* (gcc, binutils,
# make, QEMU for the run loop). "No third-party software as much as
# feasible" is about the OS code and its dependencies — every byte of
# KernOS is ours; the compiler is the one accepted third-party artifact
# (Linux is built the same way) and, in the maximalist endgame, our own
# compiler replaces this file later.

FROM gcc:13

RUN apt-get update && \
    apt-get install -y --no-install-recommends qemu-system-x86 && \
    rm -rf /var/lib/apt/lists/*
