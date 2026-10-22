/*
 * tu/tparm.h - expand a parameterized terminfo string (cup, setaf, ...)
 * with up to nine integer parameters, as terminfo(5) describes: %p, %d,
 * %c, %s-free subset, arithmetic, comparisons, %i, %?...%t...%e...%;
 * and the static/dynamic variables. Padding ($<...>) is removed: tu
 * talks to terminals that do flow control.
 */
#ifndef TU_TPARM_H
#define TU_TPARM_H

#include <stddef.h>

namespace tu {

/* Writes at most n-1 bytes and a NUL to out; returns the length, or -1
 * if cap is null. p holds np parameters (the rest are 0). */
int tparm(char* out, size_t n, const char* cap, const long* p, int np);

/* Convenience forms. */
int tparm1(char* out, size_t n, const char* cap, long a);
int tparm2(char* out, size_t n, const char* cap, long a, long b);

} /* namespace tu */

#endif
