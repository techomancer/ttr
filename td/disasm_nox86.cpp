/*
 * td/disasm_nox86.cpp - no x86-64 decoder (td/disasm.h): for systems that
 * never run x86-64 programs (IRIX), instead of disasm_x86.cpp and Zydis.
 */
#include "disasm.h"

namespace td {

bool disassemble_x86_64(Addr, const unsigned char*, size_t, const Symbolizer*, Insn*)
{
    return false;
}

} /* namespace td */
