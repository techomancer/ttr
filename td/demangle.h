/*
 * td/demangle.h - C++ names as the source wrote them, for display and for
 * breakpoints by name (td/DESIGN.md section 5.5). Two manglings, either
 * read on any system:
 *
 * - The Itanium C++ ABI's (GCC and Clang, Linux): "_ZN3Foo3barEi" is
 *   "Foo::bar(int)".
 * - MIPSpro's, cfront's scheme as EDG writes it (IRIX): "bar__3FooFi" is
 *   "Foo::bar(int)".
 *
 * td's own, as libstdc++'s __cxa_demangle would bring the standard
 * library in (td/DESIGN.md section 2).
 */
#ifndef TD_DEMANGLE_H
#define TD_DEMANGLE_H

#include "td.h"

namespace td {

/* The name as written into out; false (out as it was) if name is not a
 * mangled name td reads. */
bool demangle(const char* name, Text* out);
bool demangle_itanium(const char* name, Text* out);
bool demangle_cfront(const char* name, Text* out);

} /* namespace td */

#endif
