#include <kernel/types.h>
#include "mem.h"
#include "serial.h"

/*
 * The physical memory map, straight from the E820 handoff at 0x600.
 * For now: print it (M3 gate evidence). The page allocator (next
 * slice) will consume exactly this table - the type-1 runs are the
 * frame stack's input, with the low 1 MiB and anything overlapping
 * kernel structures carved out.
 */

#define E820_COUNT_ADDR  0x600ULL
#define E820_DATA_ADDR   0x608ULL
#define E820_MAX         32

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
    u64 a = E820_DATA_ADDR + (u64)i * 24;
    return (struct e820_entry *)a;
}

void memmap_boot_init(void)
{
    u64 count = *(volatile u64 *)E820_COUNT_ADDR;

    serial_puts("memmap: ");
    serial_puts_hex(count);
    serial_puts(" entries\n");

    if (count == 0 || count > E820_MAX) {
        serial_puts("memmap: no usable map (capture failed)\n");
        return;
    }

    for (u64 i = 0; i < count; ++i) {
        struct e820_entry *e = entry((int)i);
        const char *name = (e->type <= 5) ? type_names[e->type] : "?";

        serial_puts("  base=");
        serial_puts_hex(e->base);
        serial_puts(" len=");
        serial_puts_hex(e->len);
        serial_puts(" type=");
        serial_puts_hex(e->type);
        serial_puts(" (");
        serial_puts(name);
        serial_puts(")\n");
    }
}
