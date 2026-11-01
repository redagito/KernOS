#include <kernel/types.h>
#include "mem.h"
#include "serial.h"

#define E820_COUNT_ADDR  0x600ULL
#define E820_DATA_ADDR   0x608ULL

struct e820_entry {
    u64 base;
    u64 len;
    u32 type;
    u32 pad;
};

static const char *type_names[6] = {
    "?", "usable", "reserved", "ACPI reclaim", "ACPI NVS", "bad",
};

static struct e820_entry *entry(int i)
{
    return (struct e820_entry *)(E820_DATA_ADDR + (u64)i * 24);
}

int memmap_count(void)
{
    u64 n = *(volatile u64 *)E820_COUNT_ADDR;
    if (n > E820_MAX_ENTRIES) return 0;
    return (int)n;
}

int memmap_get(int i, u64 *base, u64 *len, u32 *type)
{
    if (i < 0 || i >= memmap_count()) return -1;
    struct e820_entry *e = entry(i);
    *base = e->base; *len = e->len; *type = e->type;
    return 0;
}

void memmap_boot_init(void)
{
    int n = memmap_count();

    serial_puts("memmap: ");
    serial_puts_hex((u64)n);
    serial_puts(" entries\n");

    if (n == 0) {
        serial_puts("memmap: no usable map (capture failed)\n");
        return;
    }

    u64 base, len;
    u32 type;
    for (int i = 0; i < n; ++i) {
        if (memmap_get(i, &base, &len, &type) < 0) break;
        serial_puts("  base=");
        serial_puts_hex(base);
        serial_puts(" len=");
        serial_puts_hex(len);
        serial_puts(" type=");
        serial_puts_hex(type);
        serial_puts(" (");
        serial_puts(type <= 5 ? type_names[type] : "?");
        serial_puts(")\n");
    }
}
