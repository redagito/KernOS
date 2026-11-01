#include "serial.h"

/*
 * Standard IBM PC UART register offsets from the COM base port.
 * DR/THR overlays with the divisor latch when LCR.DLAB (bit 7) is set.
 */
#define UART_BASE       0x3F8
#define UART_DATA       (UART_BASE + 0)  /* read: RBR, write: THR      */
#define UART_INTERRUPT  (UART_BASE + 1)  /* read: IIR, write: IER      */
#define UART_FIFO       (UART_BASE + 2)  /* FCR (write)                */
#define UART_LINE_CTRL  (UART_BASE + 3)  /* LCR                        */
#define UART_MODEM_CTRL (UART_BASE + 4)  /* MCR                        */
#define UART_LINE_STAT  (UART_BASE + 5)  /* LSR                        */

#define UART_DLAB       0x80  /* divisor latch access bit            */
#define UART_THR_EMPTY  0x20  /* transmitter holding register empty  */

static inline void out8(u16 port, u8 value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 in8(u16 port)
{
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_init(void)
{
    out8(UART_INTERRUPT, 0x00);            /* disable UART IRQs for now */
    out8(UART_LINE_CTRL, UART_DLAB);       /* enable divisor latch      */
    out8(UART_DATA,      0x03);            /* divisor 3 of 115200 -> 38400 baud */
    out8(UART_INTERRUPT, 0x00);
    out8(UART_LINE_CTRL, 0x03);            /* 8 data bits, no parity, 1 stop */
    out8(UART_FIFO,      0xC7);            /* FIFO on, clear, 14-byte threshold */
    out8(UART_MODEM_CTRL, 0x0B);           /* DTR | RTS | OUT2          */
}

void serial_putc(char c)
{
    /* Wait for the transmit holding register to empty. */
    while (!(in8(UART_LINE_STAT) & UART_THR_EMPTY)) {}
    out8(UART_DATA, (u8)c);
}

void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

void serial_puts_hex(u64 value)
{
    static const char digits[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4)
        serial_putc(digits[(value >> shift) & 0xF]);
}
