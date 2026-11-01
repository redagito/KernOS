#pragma once

// 16550-class UART (PC legacy COM1 at port 0x3F8), programmed directly
// from the 16550/NS16450 register description. This is the debug channel
// for the whole project: QEMU exposes it, and real boards with a serial
// header (or a PCI serial card) do too.

#include <kernel/types.h>

namespace kernos::serial {

void init();                 // 115200 baud, 8N1, FIFO on, no loopback
void putc(char c);
void puts(const char* s);
void puts_hex(u64 value);

} // namespace kernos::serial
