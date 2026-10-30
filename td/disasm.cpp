/*
 * td/disasm.cpp - which decoder for which CPU, and what both share
 * (td/disasm.h).
 */
#include "disasm.h"

#include <stdio.h>
#include <string.h>

namespace td {

namespace {

void copy(char* to, size_t size, const char* from)
{
    size_t n = strlen(from);
    if (n >= size)
        n = size - 1;
    memcpy(to, from, n);
    to[n] = 0;
}

} /* namespace */

void format_address(Addr a, const Symbolizer* sym, Text* out)
{
    char h[24];
    size_t at = out->length();
    if (sym && sym->symbol(a, out)) {
        out->add(" (");
        out->add(hex(a, h));
        out->add(")");
        return;
    }
    out->truncate(at);
    out->add(hex(a, h));
}

bool disassemble(const Arch* a, Addr addr, const unsigned char* bytes, size_t n,
                 const Symbolizer* sym, Insn* out)
{
    memset(out, 0, sizeof(*out));
    out->addr = addr;
    out->base_reg = -1;
    bool ok = false;
    if (a->id == ARCH_X86_64)
        ok = disassemble_x86_64(addr, bytes, n, sym, out);
    else
        ok = disassemble_mips(a, addr, bytes, n, sym, out);
    if (ok)
        return true;
    /* As data: one byte (x86), one word (MIPS). */
    int len = a->id == ARCH_X86_64 ? 1 : 4;
    if ((size_t)len > n)
        len = (int)n;
    memset(out, 0, sizeof(*out));
    out->addr = addr;
    out->base_reg = -1;
    out->kind = INSN_BAD;
    out->len = len > 0 ? len : 1;
    memcpy(out->bytes, bytes, (size_t)len);
    Text t;
    int i;
    for (i = 0; i < len; i++) {
        if (i)
            t.add(",");
        char b[8];
        snprintf(b, sizeof(b), "0x%02x", bytes[i]);
        t.add(b);
    }
    copy(out->mnemonic, sizeof(out->mnemonic), len == 4 ? ".word" : ".byte");
    if (len == 4) {
        uint32_t w = a->big_endian ? (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
                                         (uint32_t)bytes[2] << 8 | bytes[3]
                                   : (uint32_t)bytes[3] << 24 | (uint32_t)bytes[2] << 16 |
                                         (uint32_t)bytes[1] << 8 | bytes[0];
        char b[16];
        snprintf(b, sizeof(b), "0x%08x", (unsigned)w);
        copy(out->operands, sizeof(out->operands), b);
    } else {
        copy(out->operands, sizeof(out->operands), t.c_str());
    }
    return false;
}

} /* namespace td */
