#pragma once

// KernOS fixed-width integer types. We do not include host headers; the
// kernel defines its own primitive vocabulary and the (future) in-tree libc
// will build on the same types.
//
// Assumed ABI: x86-64, LP64 (long = 64 bits). Checked by the linker warnings
// on miscompiled targets until we add a static assert against an ELF header.

namespace kernos {

using i8  = signed char;
using u8  = unsigned char;
using i16 = signed short;
using u16 = unsigned short;
using i32 = signed int;
using u32 = unsigned int;
using i64 = signed long;
using u64 = unsigned long;

using size_t = unsigned long;   // LP64: unsigned long is 64-bit
using uptr   = unsigned long;   // pointer-sized unsigned

// Physical frame size of the x86-64 paging hierarchy (SDM Vol.3 §4.2).
// 4 KiB pages are the granularity we use for fine-grained mappings;
// 2 MiB pages (PS bit in PDE) are used for the initial identity map.
constexpr u64 page_size_4k = 0x1000;
constexpr u64 page_size_2m = 0x200000;

} // namespace kernos
