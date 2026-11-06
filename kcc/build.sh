#!/bin/sh
set -eu
mkdir -p build
mkdir -p build/tmp
TMPDIR="$PWD/build/tmp"; export TMPDIR
CFLAGS='-std=c11 -O2 -fno-builtin -ffreestanding -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone -Ikcc/include -Ilibc/include -Iinclude -Ikernel -Ivendor/tcc-0.9.27'
gcc $CFLAGS -c kcc/main.c -o build/kcc_main.o
gcc $CFLAGS -c kcc/arena.c -o build/kcc_arena.o
gcc $CFLAGS -c kcc/os_compat.c -o build/kcc_os_compat.o
gcc $CFLAGS -c kcc/libc_extra.c -o build/kcc_libc_extra.o
gcc $CFLAGS -c kcc/setjmp.S -o build/kcc_setjmp.o
gcc $CFLAGS -c kernel/elf64.c -o build/kcc_elf.o
gcc $CFLAGS -w -c vendor/tcc-0.9.27/libtcc.c -o build/kcc_libtcc.o
ld -s -nostdlib -T kernel/user/prog.ld -o build/kcc.elf \
  obj/lc_crt0.o build/kcc_main.o build/kcc_arena.o build/kcc_os_compat.o \
  build/kcc_libc_extra.o build/kcc_setjmp.o build/kcc_libtcc.o build/kcc_elf.o build/libc.a
