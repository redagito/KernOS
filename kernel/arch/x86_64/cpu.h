#pragma once

// CPU identification, straight from CPUID (SDM Vol.2, CPUID instruction).

#include <kernel/types.h>

namespace kernos::cpu {

// Vendor string from CPUID leaf 0: EBX, EDX, ECX concatenated.
// e.g. "GenuineIntel", "AuthenticAMD". Fills `out` (13 bytes incl. NUL).
void vendor(char out[13]);

} // namespace kernos::cpu
