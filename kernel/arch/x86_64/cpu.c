#include "cpu.h"

void kernos_cpu_vendor(char out[13])
{
    u32 b, d, c;
    /* asm: `"=b"` writes EBX directly, no separate clobber list needed. */
    __asm__ volatile("cpuid"
                 : "=b"(b), "=c"(c), "=d"(d)
                 : "a"((u32)0));
    /* Leaf 0 returns the 12-character vendor ID split across
       EBX (bytes 0-3), EDX (bytes 4-7), ECX (bytes 8-11). */
    const u32 words[3] = { b, d, c };
    for (int i = 0; i < 3; ++i) {
        out[i * 4 + 0] = (char)(words[i] & 0xFF);
        out[i * 4 + 1] = (char)((words[i] >> 8) & 0xFF);
        out[i * 4 + 2] = (char)((words[i] >> 16) & 0xFF);
        out[i * 4 + 3] = (char)((words[i] >> 24) & 0xFF);
    }
    out[12] = '\0';
}
