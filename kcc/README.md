# KCC (TinyCC 0.9.27 for KernOS)

KCC is a freestanding CPL3 guest program built with the host GCC toolchain. It uses TinyCC's libtcc API to compile and statically link one C translation unit into a KernOS ELF executable starting in the user window at `0x400000`. Input command line: `SOURCE.C [OUTPUT.ELF]`; default output changes the source extension to `.ELF`. KCC validates saved output with the kernel's own portable ELF parser and user memory limits before reporting success.

The app uses an isolated compatibility header set and a 1 MiB bounded arena. `vendor/tcc-0.9.27/config.h` selects x86_64 ELF, boot mode, static output, and one-source compilation. The local change to `tccpp.c` reduces its initial token pools to 64/64/16 KiB under `CONFIG_KERNOS`, retaining their normal growth path. Upstream's default 768/768/256 KiB reservations exceed the guest arena. The vendored LGPL `COPYING` notice is retained. The unused `lib/libtcc1.c` (GPL), `il-opcodes.h`, `texi2pod.pl`, and `tcctools.c` (which includes GPL command-line tool code) are excluded. The LGPL `lib/va_list.c` is included in KernOS libc for compiled variadic functions; the GPL helper archive is not used.

The second upstream change is in `tccelf.c`: static executables must
relocate their PLT displacements and fill GOT entries before relocation
tables are hidden. GCC 13's PLT32 calls otherwise produced jumps through
zero pointers. KCC also selects 4 KiB section alignment; the default 2 MiB
alignment placed writable data outside KernOS's user window. Both changes
were driven by guest execution failures and are covered by the workflow
test's calls between compiled code and the prebuilt runtime.

Source files, `LIBC.A`, `CRT0.OBJ`, and include files must be in the FAT root as 8.3 names; `/INCLUDE/<name>` is normalized to that root. KCC currently accepts one source plus an optional output name and no compiler flags or multiple translation units. Output linking is static, so only symbols available from `LIBC.A` can be used. The current guest libc has no general `malloc` or math library and its formatted I/O supports a limited set of integer and string formats; nested include directories and floating-point formatting are unsupported. Decimal and hex floating literals are parsed by TinyCC, but math-library functions are not provided.

Build with `make image` in the KernOS build container. This runs `kcc/build.sh`, links `build/kcc.elf` with the shared user linker script and libc, and installs it as `KCC.ELF`, together with `LIBC.A`, `CRT0.OBJ`, and the public guest headers from `libc/include`, `include/user`, and TinyCC's `include/stdarg.h`. The private compatibility headers under `kcc/include` are only for building KCC and must not be installed as the guest standard library.

Upstream archive: `https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27.tar.bz2`.
SHA-256: `de23af78fca90ce32dff2dd45b3432b2334740bb9bb7b05bf60fdbfc396ceb9c`.
Distribute this vendored source, local configuration and changes, notices, and
the project's build inputs alongside any KCC binary distribution so users can
rebuild and relink it with a modified compiler library.
