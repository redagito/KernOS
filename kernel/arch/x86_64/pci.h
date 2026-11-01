#pragma once

#include <kernel/types.h>

/*
 * PCI enumeration (M4.1): port IO config space (0xCF8/0xCFC), bus 0
 * for now - the piix3/ich9 estate all lives there. Devices are kept
 * in a fixed table for the AHCI driver (M4.2) to find by class.
 */

struct pci_dev {
    u8  bus, dev, fn;
    u16 vid, did;
    u32 class_code;      /* base<<16 | sub<<8 | prog IF */
    u8  hdr_type;
    u8  irq_pin, irq_line;
    u32 bar[6];
};

void pci_init(void);
int  pci_count(void);
const struct pci_dev *pci_dev_get(int i);

/* First device of a storage class (base 0x01), or NULL. */
const struct pci_dev *pci_find_class(u32 base_class);
