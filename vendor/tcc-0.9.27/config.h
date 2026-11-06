/* KernOS guest compiler build configuration. Keep TinyCC notices intact. */
#ifndef KERNOS_TCC_CONFIG_H
#define KERNOS_TCC_CONFIG_H
#define CONFIG_TCCBOOT 1
#define CONFIG_KERNOS 1
#define CONFIG_TCC_STATIC 1
#define TCC_TARGET_X86_64 1
#define CONFIG_TCCDIR "/"
#define CONFIG_TCC_SYSINCLUDEPATHS "/INCLUDE"
#define CONFIG_TCC_LIBPATHS "/"
#define CONFIG_TCC_CRTPREFIX "/"
#define CONFIG_TCC_ELFINTERP "-"
#define TCC_VERSION "0.9.27"
#define ONE_SOURCE 1
#endif
