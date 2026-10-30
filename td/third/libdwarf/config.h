/*
 * td/third/libdwarf/config.h - the config.h libdwarf's sources include
 * (<config.h>), written by hand instead of by its cmake or meson build:
 * ttr builds third_party/libdwarf with plain make (td/DESIGN.md 5.4).
 * The same file serves Linux (gcc, clang) and IRIX (MIPSpro, -c99).
 */
#ifndef TD_LIBDWARF_CONFIG_H
#define TD_LIBDWARF_CONFIG_H

#define HAVE_UNISTD_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDDEF_H 1
#define HAVE_FCNTL_H 1
#define HAVE_SYS_STAT_H 1
#define STDC_HEADERS 1

/* The host's byte order (the files read can be either). */
#if defined(__MIPSEB__) || defined(_MIPSEB) || defined(__BIG_ENDIAN__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define WORDS_BIGENDIAN 1
#endif

/* No zlib or zstd yet: compressed debug sections are not read (our own
 * builds do not compress them). */

#define PACKAGE_NAME "libdwarf"
#define PACKAGE_VERSION "71f0401"
#define PACKAGE_STRING "libdwarf 71f0401"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_URL "https://github.com/davea42/libdwarf-code"

#endif
