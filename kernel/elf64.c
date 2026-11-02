#include <kernel/types.h>
#include <kernel/elf64.h>

/*
 * ELF64 plan, see elf64.h for the contract. Little-endian ELF64 is
 * decoded by hand (the host tests and the kernel run the same code,
 * and neither assumes aligned access or native endianness for this).
 */

/* --- amble: strict little-endian readers ------------------------------ */
static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static u64 rd64(const u8 *p)
{
    return (u64)rd32(p) | ((u64)rd32(p + 4) << 32);
}

/* ELF64 Ehdr fields we consume, per the ABI's offsets */
#define E_PHOFF    0x20
#define E_PHENT    0x36
#define E_PHNUM    0x38
#define P_TYPE     0x00
#define P_OFFSET   0x08
#define P_VADDR    0x10
#define P_FILESZ   0x20
#define P_MEMSZ    0x28

#define PT_LOAD    1u

/* failure classes */
#define ELF_E_SHORT   (-1)       /* image too small to be an ELF         */
#define ELF_E_MAGIC   (-2)       /* not 0x7F 'ELF'                       */
#define ELF_E_FORMAT  (-3)       /* wrong class/endianness/type/machine  */
#define ELF_E_PHDRS   (-4)       /* phdr table out of bounds / bogus     */
#define ELF_E_SEGDIG  (-5)       /* a PT_LOAD lies about its bytes       */
#define ELF_E_NOLOAD  (-6)       /* no loadable segment at all           */

int elf64_plan(const void *buf, u64 len, u64 *entry,
               struct elf_seg *segs, int *nsegs)
{
    const u8 *b = buf;

    if (len < 64)
        return ELF_E_SHORT;

    if (b[0] != 0x7F || b[1] != 'E' || b[2] != 'L' || b[3] != 'F')
        return ELF_E_MAGIC;
    if (b[4] != 2 || b[5] != 1)          /* 64-bit, little-endian        */
        return ELF_E_FORMAT;
    if (rd16(b + 0x10) != 2)             /* ET_EXEC only                 */
        return ELF_E_FORMAT;
    if (rd16(b + 0x12) != 62)            /* EM_X86_64                    */
        return ELF_E_FORMAT;

    u64 phoff = rd64(b + E_PHOFF);
    u16 phent = rd16(b + E_PHENT);
    u16 phnum = rd16(b + E_PHNUM);

    if (phent != 56 || phnum == 0 || phnum > ELF_MAX_SEGS)
        return ELF_E_PHDRS;
    if (phoff > len || phnum * phent > len - phoff)
        return ELF_E_PHDRS;

    u64 ent = rd64(b + 0x18);
    int n = 0;
    for (u16 i = 0; i < phnum; ++i) {
        const u8 *ph = b + phoff + (u64)i * phent;
        if (rd32(ph + P_TYPE) != PT_LOAD)
            continue;                    /* observed, not consumed       */

        u64 off   = rd64(ph + P_OFFSET);
        u64 vaddr = rd64(ph + P_VADDR);
        u64 filesz = rd64(ph + P_FILESZ);
        u64 memsz  = rd64(ph + P_MEMSZ);

        if (off > len || filesz > len - off)
            return ELF_E_SEGDIG;
        if (memsz < filesz)
            return ELF_E_SEGDIG;

        if (n == ELF_MAX_SEGS)
            return ELF_E_PHDRS;
        segs[n].vaddr  = vaddr;
        segs[n].foff   = off;
        segs[n].filesz = filesz;
        segs[n].memsz  = memsz;
        n++;
    }

    if (n == 0)
        return ELF_E_NOLOAD;

    *entry = ent;
    *nsegs = n;
    return 0;
}

const char *elf64_errname(int code)
{
    switch (code) {
    case ELF_E_SHORT:  return "not an ELF (too short)";
    case ELF_E_MAGIC:  return "bad magic";
    case ELF_E_FORMAT: return "unsupported format (class/endian/type/machine)";
    case ELF_E_PHDRS:  return "program-header table bogus";
    case ELF_E_SEGDIG: return "segment lies about its bytes";
    case ELF_E_NOLOAD: return "no PT_LOAD segments";
    default:           return "unknown";
    }
}
