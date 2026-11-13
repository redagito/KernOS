#pragma once

/*
 * KernOS fixed-width integer types /|\ kernel-wide vocabulary. Freestanding
 * C11: we define our own primitives instead of relying on host headers,
 * and the future in-tree libc builds on the same definitions.
 *
 * ABI: x86-64, LP64 (long = 64 bits). A future static assert against the
 * ELF header will pin this down; for now the linker rejects miscompiled
 * targets by construction.
 */

typedef signed char i8;
typedef unsigned char u8;
typedef signed short i16;
typedef unsigned short u16;
typedef signed int i32;
typedef unsigned int u32;
typedef __INT64_TYPE__ i64;
typedef __UINT64_TYPE__ u64;

typedef __SIZE_TYPE__ size_t;
typedef __UINTPTR_TYPE__ uptr;   /* also correct in Windows host tests */
