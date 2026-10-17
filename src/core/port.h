/*
 * Portability helpers. Everything here must build with MIPSpro 7.4 and
 * with g++/clang in -std=c++98 -pedantic mode.
 */
#ifndef TTR_CORE_PORT_H
#define TTR_CORE_PORT_H

#if defined(__sgi)
#include <inttypes.h>
#else
#include <stdint.h>
#endif

#define TTR_UNUSED(x) ((void)(x))

/* C++98 compile-time assert; use at namespace scope only. */
#define TTR_CONCAT_(a, b) a##b
#define TTR_CONCAT(a, b) TTR_CONCAT_(a, b)
#define TTR_STATIC_ASSERT(cond) \
    typedef char TTR_CONCAT(ttr_static_assert_, __LINE__)[(cond) ? 1 : -1]

#endif
