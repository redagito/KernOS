#include <kernel/types.h>
#include "ahci.h"
#include "pci.h"
#include "pmm.h"
#include "page.h"
#include "serial.h"

/*
 * AHCI HBA life cycle over polling DMA.
 *
 * MMIO lives wherever the PCI BAR puts it (above the identity map on
 * qemu), so the BAR pages get mapped with page_map_at() into the
 * demand region first. Single outstanding command; the command list
 * holds exactly one header, the PRDT one entry, and every op builds
 * its FIS from scratch. Serial and the block layer (M4.3) talk to
 * this driver through ahci_read/ahci_write only.
 */

static volatile u32 *hba;               /* BAR → VA via page_map_at */
static int active_port = -1;
static u64 sectors_total;
static char model[41];

static u64 hdr_frame;                   /* 1 KiB: command header list  */
static u64 tbl_frame;                   /* command table (FIS + PRDT)  */
static u64 rx_frame;                    /* received-FIS area (256 B+)  */

static volatile u32 *reg32(u64 off)
{
    return (volatile u32 *)((volatile u8 *)hba + off);
}

static volatile u32 *preg(int port, u64 off)
{
    return reg32(0x100 + (u64)port * 0x80 + off);
}

static void mmio_set(u64 off, u32 bits) { *reg32(off) |= bits; }
static void mmio_clr(u64 off, u32 bits) { *reg32(off) &= ~bits; }

static int port_present(int p)
{
    return ((*preg(p, 0x28) & 0x0F) == 3);  /* SSTS.DET=3: device present */
}

static void spin_wait(volatile u32 *r, u32 mask, const char *what)
{
    u32 n = 0;
    while ((*r & mask) != 0) {
        if (++n > 8000000) {
            serial_puts("ahci: timeout: ");
            serial_puts(what);
            serial_puts(" (val=");
            serial_puts_hex(*r);
            serial_puts(")\n");
            return;
        }
        __asm__ volatile("pause");
    }
}

/* Command header (the only live one): the layout is
   DW0 = opts (CFL|W) | PRDTL<<16, DW1 = PRDBC (HBA-owned status),
   DW2+DW3 = 64-bit CTBA. QEMU's AHCICmdHdr matches: tbl_addr lives at
   bytes 8..15 - NOT dword 1. */
static void build_cmd_header(int is_write)
{
    volatile u32 *h = (volatile u32 *)hdr_frame;
    h[0] = 5u | (1u << 16);              /* CFL=5 dwords | PRDTL=1 */
    if (is_write) h[0] |= (1u << 6);     /* W: host-to-device data */
    h[1] = 0;                            /* DW1: PRDBC - controller writes it */
    h[2] = (u32)tbl_frame;               /* DW2: CTBA low */
    h[3] = (u32)(tbl_frame >> 32);       /* DW3: CTBA U - 0 on this machine */
}

/* One synchronous command: h2d FIS + single PRDT + CI polling. */
static int issue(u8 cmd, u64 lba, u16 count, u64 buf_pa, int is_write)
{
    volatile u8 *fis = (volatile u8 *)tbl_frame;
    for (int i = 0; i < 32; ++i)
        fis[i] = 0;

    fis[0]  = 0x27;                      /* Register H2D */
    fis[1]  = 0x80;                      /* C bit: a command */
    fis[2]  = cmd;
    fis[7]  = 0x40;                      /* LBA addressing */
    /* LBA48 across bytes 4-6 (bits 0..23), 7 device, 8-10 (24..47) */
    fis[4]  = (u8)(lba & 0xFF);
    fis[5]  = (u8)((lba >> 8) & 0xFF);
    fis[6]  = (u8)((lba >> 16) & 0xFF);
    fis[8]  = (u8)((lba >> 24) & 0xFF);
    fis[9]  = (u8)((lba >> 32) & 0xFF);
    fis[10] = (u8)((lba >> 40) & 0xFF);
    fis[11] = 0;                         /* feature 15:8 - unused */
    fis[12] = (u8)(count & 0xFF);        /* sector count lo */
    fis[13] = (u8)((count >> 8) & 0xFF); /* sector count hi */

    /* PRDT at tbl+0x80: the entry is SIXTEEN bytes wide:
       DW0 = DBA, DW1 = DBAU, DW2 = reserved, DW3 = (byte count - 1)
       with the I bit at bit 31 of that dword. (A 16-byte-entry PRDT is
       the AHCI layout; QEMU's AHCI_SG struct - addr:8, reserved:4,
       flags_size:4 - reads it exactly this way.) */
    volatile u64 *prd = (volatile u64 *)((u8 *)tbl_frame + 0x80);
    prd[0] = buf_pa;                                          /* DW0+DW1: DBA | DBAU */
    prd[1] = ((u64)(count * 512 - 1) << 32) | (1ULL << 63);   /* DW2=0, DW3: DBC|I   */

    build_cmd_header(is_write);
    *preg(active_port, 0x38) = 1;        /* CI[0]: issue */

    spin_wait(preg(active_port, 0x38), 1, "CI drain");
    if (*preg(active_port, 0x38) & 1)
        return -1;                       /* timeout already reported */

    if (*preg(active_port, 0x20) & 1) {  /* TFD.ERR */
        serial_puts("ahci: op err cmd=");
        serial_puts_hex(cmd);
        serial_puts(" lba=");
        serial_puts_hex(lba);
        serial_puts(" tfd=");
        serial_puts_hex(*preg(active_port, 0x20));
        serial_puts("\n");
        return -1;
    }
    return 0;
}

void ahci_init(void)
{
    const struct pci_dev *d = 0;
    int n = pci_count();
    for (int i = 0; i < n; ++i) {
        const struct pci_dev *c = pci_dev_get(i);
        if (c->class_code == 0x010601) { d = c; break; }
    }
    if (!d) {
        serial_puts("ahci: no HBA on the bus\n");
        return;
    }

    /* PCI cmd: memory decode + bus master (DMA) + INTx mute (polling). */
    u32 pc = (pci_cfg32_read(d->bus, d->dev, d->fn, 0x04) & 0xFFFF) | 0x407;
    pci_cfg32_write(d->bus, d->dev, d->fn, 0x04, pc);

    u64 bar = d->bar[5] & ~0xFFFULL;
    if (page_map_at(AHCI_HBA_VA, bar, 1) < 0) {
        serial_puts("ahci: BAR map refused\n");
        return;
    }
    hba = (volatile u32 *)AHCI_HBA_VA;

    mmio_set(0x04, 1u << 31);            /* GHC.AE */
    mmio_clr(0x04, 1u << 1);             /* GHC.IE = 0: fully polled */

    serial_puts("ahci: HBA ");
    serial_puts_hex(bar);
    serial_puts(" -> ");
    serial_puts_hex(AHCI_HBA_VA);
    serial_puts(" ver=");
    serial_puts_hex(*reg32(0x10));
    serial_puts(" ports=");
    serial_puts_hex(*reg32(0x0C));
    serial_puts("\n");

    for (int p = 0; p <= 5; ++p) {       /* ICH9: 6 ports (0x3F in PI?) */
        if (port_present(p)) { active_port = p; break; }
    }
    if (active_port < 0) {
        serial_puts("ahci: no live port\n");
        return;
    }
    serial_puts("ahci: port ");
    serial_putc((char)('0' + active_port));
    serial_puts(" live (det=3)\n");

    /* DMA structures: fresh pmm frames (zeroed - freestanding). */
    hdr_frame = pmm_alloc();
    tbl_frame = pmm_alloc();
    rx_frame  = pmm_alloc();
    if (!hdr_frame || !tbl_frame || !rx_frame) {
        serial_puts("ahci: out of frames\n");
        return;
    }
    for (u32 i = 0; i < FRAME_SIZE / sizeof(u64); ++i)
        ((volatile u64 *)hdr_frame)[i] = 0;
    for (u32 i = 0; i < FRAME_SIZE / sizeof(u64); ++i)
        ((volatile u64 *)tbl_frame)[i] = 0;
    for (u32 i = 0; i < FRAME_SIZE / sizeof(u64); ++i)
        ((volatile u64 *)rx_frame)[i] = 0;

    /* Stop engine; install lists; restart (FRE -> ST). */
    *preg(active_port, 0x18) &= ~((1u << 0) | (1u << 4));
    spin_wait(reg32(0x100 + (u64)active_port * 0x80 + 0x18), (1u << 15) | (1u << 14), "engine stop");

    *preg(active_port, 0x00) = (u32)(hdr_frame & 0xFFFFFFFFULL);     /* PxCLB */
    *preg(active_port, 0x04) = (u32)(hdr_frame >> 32);               /* PxCLBU */
    *preg(active_port, 0x08) = (u32)(rx_frame & 0xFFFFFFFFULL);      /* PxFB  */
    *preg(active_port, 0x0C) = (u32)(rx_frame >> 32);                /* PxFBU */

    *preg(active_port, 0x18) |= (1u << 4);  /* FRE */
    *preg(active_port, 0x18) |= 1u;         /* ST  */

    /* --- IDENTIFY: model string + LBA48 capacity --- */
    u64 id_fr = pmm_alloc();
    if (issue(0xEC, 0, 1, id_fr, 0) < 0) {
        serial_puts("ahci: IDENTIFY failed\n");
        pmm_free(id_fr);
        return;
    }
    volatile u16 *id = (volatile u16 *)id_fr;
    sectors_total = (u64)id[100] | ((u64)id[101] << 16)
                  | ((u64)id[102] << 32) | ((u64)id[103] << 48);
    int last = 0;
    for (int i = 0; i < 20; ++i) {             /* words 27..46, 2 chars/word */
        char c0 = (char)(id[27 + i] >> 8);
        char c1 = (char)(id[27 + i] & 0xFF);
        if (c0 == ' ' && c1 == ' ') break;     /* chars are big-endian/word */
        if (c0 != ' ' && last < 40) model[last++] = c0;
        if (c1 != ' ' && last < 40) model[last++] = c1;
    }
    model[last] = 0;
    pmm_free(id_fr);

    serial_puts("ahci: drive '");
    serial_puts(model);
    serial_puts("' sectors=");
    serial_puts_hex(sectors_total);
    serial_puts("\n");

    /* --- read test: LBA 0..7 (boot disk untouched; the test disk holds
       our marker at LBA 100), print the first bytes in full hex --- */
    u64 rbuf = pmm_alloc();
    if (ahci_read(0, (void *)rbuf, 8) == 0) {
        volatile u8 *b = (volatile u8 *)rbuf;
        serial_puts("ahci: read lba0-7 ok, bytes[0..3]=");
        for (int i = 0; i < 4; ++i) {
            serial_putc("0123456789abcdef"[(b[i] >> 4) & 0xF]);
            serial_putc("0123456789abcdef"[b[i] & 0xF]);
        }
        serial_puts("\n");
        pmm_free(rbuf);
    } else {
        serial_puts("ahci: read test failed\n");
    }
}

int ahci_read(u64 lba, void *dma_buf, u32 sectors)
{
    if (active_port < 0 || !sectors || sectors > 256) return -1;
    if (((u64)dma_buf) & 0xFFFULL) return -1;
    return issue(0x25, lba, (u16)sectors, (u64)dma_buf, 0);
}

int ahci_write(u64 lba, const void *dma_buf, u32 sectors)
{
    if (active_port < 0 || !sectors || sectors > 256) return -1;
    if (((u64)dma_buf) & 0xFFFULL) return -1;
    return issue(0x35, lba, (u16)sectors, (u64)dma_buf, 1);
}

u64 ahci_sector_count(void)
{
    return active_port < 0 ? 0 : sectors_total;
}

void ahci_info(void)
{
    serial_puts("ahci: capacity=");
    serial_puts_hex(sectors_total);
    serial_puts(" sectors\n");
}
