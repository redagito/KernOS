#include <kernel/types.h>
#include "pci.h"
#include "serial.h"
#include "console.h"

/*
 * Type-1 config access (the only kind this machine has): write the
 * b/d/f/offset into 0xCF8, read data from 0xCFC. All offsets 32-bit
 * aligned; cfg16 slices a word out of the dword.
 */

#define PCI_MAX_BUS      1
#define PCI_MAX_DEVS     32
#define MAX_DEVS         16

static struct pci_dev devs[MAX_DEVS];
static int ndevs;

/* Width-limited number printing (VID/DID/class want 4/6 hex digits). */
static void puthex(u32 v, int digits)
{
    for (int i = digits - 1; i >= 0; --i)
        console_putc("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
}

static void putdec(u32 n)
{
    char b[16]; int i = (int)sizeof b - 1; b[i] = 0;
    if (!n) b[--i] = '0';
    while (n) { b[--i] = (char)('0' + n % 10); n /= 10; }
    console_puts(&b[i]);
}

static void outl_addr(u16 port, u32 val) { __asm__ volatile("outl %0, %1" :: "a"(val), "Nd"(port)); }
static u32  inl_addr(u16 port)           { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }

static u32 cfg32(u8 bus, u8 dev, u8 fn, u8 off)
{
    u32 a = 0x80000000u
          | ((u32)bus << 16) | ((u32)(dev & 0x1F) << 11)
          | ((u32)(fn & 7) << 8) | (u32)(off & 0xFC);
    outl_addr(0xCF8, a);
    return inl_addr(0xCFC);
}

u32 pci_cfg32_read(u8 bus, u8 dev, u8 fn, u8 off) { return cfg32(bus, dev, fn, off); }

void pci_cfg32_write(u8 bus, u8 dev, u8 fn, u8 off, u32 val)
{
    u32 a = 0x80000000u
          | ((u32)bus << 16) | ((u32)(dev & 0x1F) << 11)
          | ((u32)(fn & 7) << 8) | (u32)(off & 0xFC);
    outl_addr(0xCF8, a);
    outl_addr(0xCFC, val);
}

static u16 cfg16(u8 bus, u8 dev, u8 fn, u8 off)
{
    return (u16)((cfg32(bus, dev, fn, off & 0xFC) >> ((off & 2) * 8)) & 0xFFFF);
}

void pci_probe_dev(u8 bus, u8 dev, u8 fn)
{
    if (ndevs >= MAX_DEVS) return;

    u16 vid = cfg16(bus, dev, fn, 0x00);
    if (vid == 0xFFFF)
        return;                          /* nothing at this slot */

    struct pci_dev *d = &devs[ndevs];
    d->bus = bus; d->dev = dev; d->fn = fn;
    d->vid       = vid;
    d->did       = cfg16(bus, dev, fn, 0x02);
    d->class_code = (cfg32(bus, dev, fn, 0x08) >> 8) & 0xFFFFFF;
    d->hdr_type  = (u8)(cfg16(bus, dev, fn, 0x0E) & 0xFF);
    d->irq_pin   = (u8)(cfg16(bus, dev, fn, 0x3D) & 0xFF);
    d->irq_line  = (u8)(cfg16(bus, dev, fn, 0x3C) & 0xFF);
    for (int i = 0; i < 6; ++i)
        d->bar[i] = cfg32(bus, dev, fn, (u8)(0x10 + i * 4));

    ndevs++;
}

void pci_init(void)
{
    for (u8 dev = 0; dev < PCI_MAX_DEVS; ++dev) {
        int before = ndevs;
        pci_probe_dev(0, dev, 0);
        if (ndevs == before) continue;    /* absent slot */
        /* multifunction bit (header type d7): only probe fns if set */
        if (devs[ndevs - 1].hdr_type & 0x80) {
            for (u8 fn = 1; fn < 8; ++fn)
                pci_probe_dev(0, dev, fn);
        }
    }

    console_puts("pci: ");
    putdec((u32)ndevs);
    console_puts(" devices\n");

    for (int i = 0; i < ndevs; ++i) {
        struct pci_dev *d = &devs[i];
        console_puts("  00:");
        puthex(d->dev, 2);
        console_puts(".");
        puthex(d->fn, 1);
        console_puts(" ");
        puthex(d->vid, 4);
        console_puts(":");
        puthex(d->did, 4);
        console_puts(" class ");
        puthex(d->class_code, 6);
        console_puts(" irq ");
        puthex(d->irq_pin, 1);
        console_puts("/");
        puthex(d->irq_line, 2);
        if ((d->class_code >> 16) == 0x01) {   /* storage: show the BARs */
            for (int b = 0; b < 6; ++b) {
                console_puts(" bar");
                puthex(b, 1);
                console_puts("=");
                puthex(d->bar[b], 8);
            }
        }
        console_puts("\n");
    }
}

int pci_count(void) { return ndevs; }

const struct pci_dev *pci_dev_get(int i)
{
    if (i < 0 || i >= ndevs) return 0;
    return &devs[i];
}

const struct pci_dev *pci_find_class(u32 base_class)
{
    for (int i = 0; i < ndevs; ++i)
        if ((devs[i].class_code >> 16) == base_class)
            return &devs[i];
    return 0;
}
