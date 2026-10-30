/*
 * td/disasm.h - machine instructions as text (td/DESIGN.md section 6.1):
 * x86-64 through Zydis (disasm_x86.cpp; third_party/zydis, compiled
 * through td/third/zydis.c), MIPS by td itself (disasm_mips.cpp). Intel
 * syntax as Visual C++ wrote it ("mov dword ptr [rbp-4], eax"); MIPS as
 * IRIX's dis does ("lw a0,16(sp)"), with the n32 register names.
 */
#ifndef TD_DISASM_H
#define TD_DISASM_H

#include "td.h"

namespace td {

enum InsnKind {
    INSN_OTHER,
    INSN_JUMP,          /* always taken: jmp, j, b */
    INSN_BRANCH,        /* conditional */
    INSN_CALL,
    INSN_RETURN,
    INSN_BAD            /* not an instruction td knows: shown as bytes */
};

/* Names for addresses: "main+0x1c", "g_count". */
class Symbolizer {
public:
    virtual ~Symbolizer() {}
    /* The symbol at or around a, with "+0x..." when inside it; false if
     * none. */
    virtual bool symbol(Addr a, Text* out) const = 0;
};

struct Insn {
    Addr addr;
    int len;                /* bytes (MIPS: always 4) */
    unsigned char bytes[16];
    InsnKind kind;
    bool delay_slot;        /* MIPS: the next instruction runs first */
    bool has_target;
    Addr target;            /* a jump's, branch's or call's, if known */
    /* A memory operand through a register plus a displacement (MIPS:
     * "-32740(gp)", which the Session can name); base_reg is a DWARF
     * register number, -1 for none. */
    int base_reg;
    int64_t disp;
    char mnemonic[24];
    char operands[200];
    /* A note after the operands (what a gp-relative word points to). */
    char comment[120];
};

/* The longest instruction of any CPU td knows. */
enum { INSN_MAX = 15 };

/* The instruction at addr, from n bytes (at least INSN_MAX where the
 * memory has them). Addresses are named through sym (0: hex only). False,
 * with an INSN_BAD Insn of one byte (x86) or one word (MIPS), if they are
 * no instruction. */
bool disassemble(const Arch* a, Addr addr, const unsigned char* bytes, size_t n,
                 const Symbolizer* sym, Insn* out);

/* The CPUs' own decoders (disasm.cpp dispatches). */
bool disassemble_x86_64(Addr addr, const unsigned char* bytes, size_t n, const Symbolizer* sym,
                        Insn* out);
bool disassemble_mips(const Arch* a, Addr addr, const unsigned char* bytes, size_t n,
                      const Symbolizer* sym, Insn* out);

/* "name (0x1234)", or "0x1234" without a name: how operands show an
 * address. */
void format_address(Addr a, const Symbolizer* sym, Text* out);

} /* namespace td */

#endif
