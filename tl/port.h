/*
 * tl: portability helpers. Everything in tl/ must build with MIPSpro 7.4
 * and with g++/clang in -std=c++98 -pedantic mode, with only the C
 * library. This mirrors src/core/port.h; tl does not depend on core/.
 */
#ifndef TL_PORT_H
#define TL_PORT_H

#if defined(__sgi)
#include <inttypes.h>
#else
#include <stdint.h>
#endif
#include <stddef.h>

#define TL_UNUSED(x) ((void)(x))

/* C++98 compile-time assert; use at namespace scope only. */
#define TL_CONCAT_(a, b) a##b
#define TL_CONCAT(a, b) TL_CONCAT_(a, b)
#define TL_STATIC_ASSERT(cond) \
    typedef char TL_CONCAT(tl_static_assert_, __LINE__)[(cond) ? 1 : -1]

#endif
