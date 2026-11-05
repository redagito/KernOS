#pragma once

#include <kernel/types.h>
#include "page.h"     /* PAGE_DEMAND_BASE */

/*
 * AHCI storage (M4.2): one HBA (found by PCI class 010601), polling
 * DMA, single outstanding command at a time (no locks yet - the
 * scheduler exists but shm not; serialization arrives with M4.3's
 * filesystem layer or M5 process model... noted debt).
 *
 *           write         read
 * Boot disk = if=ide (PIIX3) ; AHCI drives attach via ich9-ahci device.
 */

void ahci_init(void);

/* HBA MMIO home: demand region + 32 MiB (heap lives at +64 MiB; the
   page-table self-tests use the low blocks). Set - Page.h exposes
   PAGE_DEMAND_BASE for this. */
#define AHCI_HBA_VA  (PAGE_DEMAND_BASE + 32ULL * 1024 * 1024)

/* Synchronous block reads; requires pmm-backed DMA buffer (frame). */
int  ahci_read(u64 lba, void *dma_buf, u32 sectors);
int  ahci_write(u64 lba, const void *dma_buf, u32 sectors);

/* device capacity from the last successful IDENTIFY (0 = not up) */
u64  ahci_sector_count(void);
