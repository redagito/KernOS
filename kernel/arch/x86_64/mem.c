#include <kernel/types.h>
#include "mem.h"
#include "serial.h"
#include "console.h"

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

/* Stage 2 plants the E820 count at 0x600 (boot-time RAM, not a C
   object). The register-barrier make the constant address opaque to
   the optimizer: gcc's array-bounds checker would otherwise flag the
   cast as a read of a zero-size array once it folds 0x600 in. */
__attribute__((noinline)) static u64 leak64(u64 at)
{
    __asm__ volatile("" : "+r"(at));
    return *(volatile u64 *)at;
}

int memmap_count(void)
{
    u64 n = leak64(E820_COUNT_ADDR);
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

    console_puts("memmap: ");
    console_puts_hex((u64)n);
    console_puts(" entries\n");

    if (n == 0) {
        console_puts("memmap: no usable map (capture failed)\n");
        return;
    }

    u64 base, len;
    u32 type;
    for (int i = 0; i < n; ++i) {
        if (memmap_get(i, &base, &len, &type) < 0) break;
        console_puts("  base=");
        console_puts_hex(base);
        console_puts(" len=");
        console_puts_hex(len);
        console_puts(" type=");
        console_puts_hex(type);
        console_puts(" (");
        console_puts(type <= 5 ? type_names[type] : "?");
        console_puts(")\n");
    }
}
