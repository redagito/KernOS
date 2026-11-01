#include "serial.h"

namespace kernos::serial {

namespace {

/*
 * Standard IBM PC UART register offsets from the COM base port.
 * DR/THR overlays with the divisor latch when LCR.DLAB (bit 7) is set.
 */
constexpr u16 BASE = 0x3F8;
constexpr u16 DATA       = BASE + 0;  // read: RBR, write: THR
constexpr u16 INTERRUPT  = BASE + 1;  // read: IIR, write: IER
constexpr u16 FIFO       = BASE + 2;  // FCR (write)
constexpr u16 LINE_CTRL  = BASE + 3;  // LCR
constexpr u16 MODEM_CTRL = BASE + 4;  // MCR
constexpr u16 LINE_STAT  = BASE + 5;  // LSR

constexpr u8 DLAB      = 0x80;  // divisor latch access bit
constexpr u8 THR_EMPTY = 0x20;  // transmitter holding register empty

inline void out8(u16 port, u8 value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline u8 in8(u16 port) {
    u8 value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

} // namespace

void init() {
    out8(INTERRUPT, 0x00);            // disable UART IRQs for now
    out8(LINE_CTRL, DLAB);            // enable divisor latch
    out8(DATA,     0x03);             // divisor 3 of 115200 -> 38400 baud
    out8(INTERRUPT, 0x00);
    out8(LINE_CTRL, 0x03);            // 8 data bits, no parity, 1 stop
    out8(FIFO,     0xC7);             // FIFO on, clear, 14-byte threshold
    out8(MODEM_CTRL, 0x0B);           // DTR | RTS | OUT2
}

void putc(char c) {
    // Wait for the transmit holding register to empty.
    while (!(in8(LINE_STAT) & THR_EMPTY)) {}
    out8(DATA, u8(c));
}

void puts(const char* s) {
    while (*s)
        putc(*s++);
}

void puts_hex(u64 value) {
    static const char digits[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4)
        putc(digits[(value >> shift) & 0xF]);
}

} // namespace kernos::serial
