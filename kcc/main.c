#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <libtcc.h>
#include <kernel/elf64.h>
#include "arch/x86_64/proc.h"

static void error_cb(void *opaque, const char *msg)
{ (void)opaque; fprintf(stderr, "kcc: %s\n", msg); }

static int parse(char *s, char **args)
{
    int n = 0;
    while (*s) {
        while (*s == ' ' || *s == '\t') ++s;
        if (!*s) break;
        if (n == 2) return -1;
        args[n++] = s;
        while (*s && *s != ' ' && *s != '\t') ++s;
        if (*s) *s++ = 0;
    }
    return n;
}

static int name_ok(const char *name)
{
    int base = 0, ext = 0, dot = 0;
    for (int i = 0; name[i]; ++i) {
        char c = name[i];
        if (c == '.') { if (dot || !base) return 0; dot = 1; continue; }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) return 0;
        if (dot) ++ext; else ++base;
    }
    return base > 0 && base <= 8 && ext > 0 && ext <= 3;
}

static int valid_output(const char *name)
{
    long n = file_load(name, 0, 0);
    if (n < 64 || n > 512 * 1024) return 0;
    unsigned char *file = malloc(n);
    if (!file) return 0;
    struct elf_seg segs[ELF_MAX_SEGS];
    int count = 0;
    u64 entry, low = (u64)-1, high = 0;
    int ok = file_load(name, file, n) == n && !elf64_plan(file, n, &entry, segs, &count);
    for (int i = 0; ok && i < count; ++i) {
        if (segs[i].vaddr < USER_WINDOW_LO ||
            segs[i].vaddr + segs[i].memsz > USER_STACK_VA - USER_STACK_SZ) ok = 0;
        if (segs[i].vaddr < low) low = segs[i].vaddr;
        if (segs[i].vaddr + segs[i].memsz > high) high = segs[i].vaddr + segs[i].memsz;
    }
    if (ok && (entry < low || entry >= high ||
               (high - (low & ~4095ULL) + 4095) / 4096 > USER_MAX_PAGES)) ok = 0;
    free(file);
    return ok;
}

int main(void)
{
    char args[128], *av[2], out[13];
    long n = process_args(args, sizeof args);
    if (n < 0) { puts("kcc: command line unavailable"); return 1; }
    int count = parse(args, av);
    if (count < 1 || !name_ok(av[0]) || (count == 2 && !name_ok(av[1]))) {
        puts("usage: cc SOURCE.C [OUTPUT.ELF] (8.3 names)"); return 1;
    }
    char *source_ext = strchr(av[0], '.');
    if (!source_ext || strlen(source_ext) != 2 || (source_ext[1] | 32) != 'c') {
        puts("kcc: source must have the .C extension"); return 1;
    }
    if (count == 2) strcpy(out, av[1]);
    else {
        int i = 0;
        while (av[0][i] && av[0][i] != '.') { out[i] = av[0][i]; ++i; }
        strcpy(out + i, ".ELF");
    }
    char *ext = strchr(out, '.');
    if (!ext || strlen(ext) != 4 || (ext[1] | 32) != 'e' ||
        (ext[2] | 32) != 'l' || (ext[3] | 32) != 'f') {
        puts("kcc: output must have the .ELF extension"); return 1;
    }
    long size = file_load(av[0], 0, 0);
    if (size < 0 || size > 16383) { puts("kcc: source unreadable or exceeds 16383 bytes"); return 1; }
    char *source = malloc(size + 64);
    if (!source) { puts("kcc: source buffer unavailable"); return 1; }
    int prefix = snprintf(source, 64, "#line 1 \"%s\"\n", av[0]);
    if (file_load(av[0], source + prefix, size) != size) {
        puts("kcc: source read failed"); free(source); return 1;
    }
    source[prefix + size] = 0;
    TCCState *state = tcc_new();
    if (!state) { free(source); puts("kcc: compiler memory unavailable"); return 1; }
    tcc_set_error_func(state, 0, error_cb);
    tcc_set_lib_path(state, "/");
    tcc_set_options(state, "-nostdlib -nostdinc -static -Wl,-Ttext=0x400000,-section-alignment=0x1000");
    int failed = tcc_set_output_type(state, TCC_OUTPUT_EXE) < 0;
    tcc_add_include_path(state, "/INCLUDE");
    /* FAT names are case-insensitive; upstream extension dispatch is not. */
    if (!failed) failed = tcc_add_file(state, "CRT0.OBJ") < 0;
    if (!failed) failed = tcc_compile_string(state, source) < 0;
    if (!failed) failed = tcc_add_file(state, "LIBC.A") < 0;
    if (!failed) failed = tcc_output_file(state, out) < 0;
    tcc_delete(state);
    free(source);
    if (failed) { puts("kcc: compilation failed"); return 1; }
    if (!valid_output(out)) { puts("kcc: output exceeds the executable contract"); return 1; }
    printf("created %s\n", out);
    return 0;
}
