/*
 * td/disasm_mips.cpp - MIPS instructions (td/disasm.h): MIPS I to IV,
 * what MIPSpro and gcc write for IRIX, with the floating point unit
 * (COP1, COP1X) and the system's own instructions (COP0) a program
 * might be stopped in. Written as IRIX's dis does: "addiu sp,sp,-32",
 * "lw a0,16(sp)", "add.d $f0,$f2,$f4", with the usual short forms (nop,
 * move, li, b, beqz, bnez, neg, negu, not, bal).
 *
 * The fields: op 31-26, rs 25-21, rt 20-16, rd 15-11, sa 10-6, funct
 * 5-0; for the floating point unit fmt 25-21, ft 20-16, fs 15-11, fd
 * 10-6.
 */
#include "disasm.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace td {

namespace {

const char* const k_regimm[32] = {
    "bltz", "bgez", "bltzl", "bgezl", 0, 0, 0, 0,
    "tgei", "tgeiu", "tlti", "tltiu", "teqi", 0, "tnei", 0,
    "bltzal", "bgezal", "bltzall", "bgezall", 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

/* SPECIAL's three-register arithmetic (rd,rs,rt), by funct. */
const char* const k_special3[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, "movz", "movn", 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    "add", "addu", "sub", "subu", "and", "or", "xor", "nor",
    0, 0, "slt", "sltu", "dadd", "daddu", "dsub", "dsubu",
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

/* Loads and stores (rt,offset(base)), by op; floating point ones
 * marked with an f first. */
const char* const k_mem[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, "ldl", "ldr", 0, 0, 0, 0,
    "lb", "lh", "lwl", "lw", "lbu", "lhu", "lwr", "lwu",
    "sb", "sh", "swl", "sw", "sdl", "sdr", "swr", 0,
    "ll", "flwc1", "lwc2", 0, "lld", "fldc1", "ldc2", "ld",
    "sc", "fswc1", "swc2", 0, "scd", "fsdc1", "sdc2", "sd"
};

const char* const k_fp_cond[16] = {
    "f", "un", "eq", "ueq", "olt", "ult", "ole", "ule",
    "sf", "ngle", "seq", "ngl", "lt", "nge", "le", "ngt"
};

/* fmt's letter: 16 s, 17 d, 20 w, 21 l. */
const char* fmt_name(int fmt)
{
    switch (fmt) {
    case 16: return "s";
    case 17: return "d";
    case 20: return "w";
    case 21: return "l";
    case 22: return "ps";
    default: return 0;
    }
}

class Decoder {
public:
    Decoder(const Arch* a, Addr pc, const Symbolizer* sym, Insn* out)
        : a_(a), pc_(pc), sym_(sym), out_(out)
    {
    }
    bool decode(uint32_t w);

private:
    const char* r(int n) const { return a_->reg_name(n); }
    void name(const char* m) { snprintf(out_->mnemonic, sizeof(out_->mnemonic), "%s", m); }
    void ops(const char* fmt, ...);
    /* A branch: its target, "name+0x10 (0x...)". */
    void branch(const char* m, const char* regs, InsnKind kind, int16_t off);
    void mem(const char* m, const char* reg, int base, int16_t off);
    Addr wrap(Addr a) const { return a_->ptr_size == 4 ? (Addr)(uint32_t)a : a; }
    bool special(uint32_t w);
    bool regimm(uint32_t w);
    bool cop0(uint32_t w);
    bool cop1(uint32_t w);
    bool cop1x(uint32_t w);

    const Arch* a_;
    Addr pc_;
    const Symbolizer* sym_;
    Insn* out_;
};

void Decoder::ops(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out_->operands, sizeof(out_->operands), fmt, ap);
    va_end(ap);
}

void Decoder::branch(const char* m, const char* regs, InsnKind kind, int16_t off)
{
    Addr t = wrap(pc_ + 4 + (Addr)((int64_t)off * 4));
    Text text;
    if (regs && *regs) {
        text.add(regs);
        text.add(",");
    }
    format_address(t, sym_, &text);
    name(m);
    ops("%s", text.c_str());
    out_->kind = kind;
    out_->delay_slot = true;
    out_->has_target = true;
    out_->target = t;
}

void Decoder::mem(const char* m, const char* reg, int base, int16_t off)
{
    name(m);
    ops("%s,%d(%s)", reg, (int)off, r(base));
    out_->base_reg = base;
    out_->disp = off;
}

bool Decoder::decode(uint32_t w)
{
    int op = (int)(w >> 26);
    int rs = (int)(w >> 21) & 31, rt = (int)(w >> 16) & 31;
    int16_t imm = (int16_t)(w & 0xffff);
    unsigned uimm = w & 0xffff;
    char regs[32];
    switch (op) {
    case 0: return special(w);
    case 1: return regimm(w);
    case 2:
    case 3: {
        Addr t = wrap(((pc_ + 4) & ~(Addr)0x0fffffff) | (Addr)(w & 0x03ffffff) << 2);
        Text text;
        format_address(t, sym_, &text);
        name(op == 2 ? "j" : "jal");
        ops("%s", text.c_str());
        out_->kind = op == 2 ? INSN_JUMP : INSN_CALL;
        out_->delay_slot = true;
        out_->has_target = true;
        out_->target = t;
        return true;
    }
    case 4:
    case 20:
        if (rs == 0 && rt == 0 && op == 4) {
            branch("b", "", INSN_JUMP, imm);
        } else if (rt == 0) {
            branch(op == 4 ? "beqz" : "beqzl", r(rs), INSN_BRANCH, imm);
        } else {
            snprintf(regs, sizeof(regs), "%s,%s", r(rs), r(rt));
            branch(op == 4 ? "beq" : "beql", regs, INSN_BRANCH, imm);
        }
        return true;
    case 5:
    case 21:
        if (rt == 0) {
            branch(op == 5 ? "bnez" : "bnezl", r(rs), INSN_BRANCH, imm);
        } else {
            snprintf(regs, sizeof(regs), "%s,%s", r(rs), r(rt));
            branch(op == 5 ? "bne" : "bnel", regs, INSN_BRANCH, imm);
        }
        return true;
    case 6:
    case 7:
    case 22:
    case 23: {
        if (rt != 0)
            return false;
        static const char* const n[4] = { "blez", "bgtz", "blezl", "bgtzl" };
        branch(n[op < 8 ? op - 6 : op - 20], r(rs), INSN_BRANCH, imm);
        return true;
    }
    case 8:
    case 9:
    case 24:
    case 25:
        if (rs == 0 && (op == 9 || op == 25)) {
            name("li");
            ops("%s,%d", r(rt), (int)imm);
            return true;
        }
        name(op == 8 ? "addi" : op == 9 ? "addiu" : op == 24 ? "daddi" : "daddiu");
        ops("%s,%s,%d", r(rt), r(rs), (int)imm);
        return true;
    case 10:
    case 11:
        name(op == 10 ? "slti" : "sltiu");
        ops("%s,%s,%d", r(rt), r(rs), (int)imm);
        return true;
    case 12:
    case 13:
    case 14:
        if (op == 13 && rs == 0) {
            name("li");
            ops("%s,0x%x", r(rt), uimm);
            return true;
        }
        name(op == 12 ? "andi" : op == 13 ? "ori" : "xori");
        ops("%s,%s,0x%x", r(rt), r(rs), uimm);
        return true;
    case 15:
        if (rs != 0)
            return false;
        name("lui");
        ops("%s,0x%x", r(rt), uimm);
        return true;
    case 16: return cop0(w);
    case 17: return cop1(w);
    case 19: return cop1x(w);
    case 47:
        mem("cache", "", rs, imm);
        ops("0x%x,%d(%s)", rt, (int)imm, r(rs));
        return true;
    case 51:
        mem("pref", "", rs, imm);
        ops("%d,%d(%s)", rt, (int)imm, r(rs));
        return true;
    default:
        break;
    }
    const char* m = k_mem[op];
    if (!m)
        return false;
    if (m[0] == 'f') {
        char f[8];
        snprintf(f, sizeof(f), "$f%d", rt);
        mem(m + 1, f, rs, imm);
    } else if (op == 50 || op == 54 || op == 58 || op == 62) {
        char c[8];
        snprintf(c, sizeof(c), "$%d", rt);
        mem(m, c, rs, imm);
    } else {
        mem(m, r(rt), rs, imm);
    }
    return true;
}

bool Decoder::special(uint32_t w)
{
    int rs = (int)(w >> 21) & 31, rt = (int)(w >> 16) & 31, rd = (int)(w >> 11) & 31;
    int sa = (int)(w >> 6) & 31, fn = (int)(w & 63);
    switch (fn) {
    case 0:
        if (w == 0) {
            name("nop");
            return true;
        }
        if (rs)
            return false;
        name("sll");
        ops("%s,%s,%d", r(rd), r(rt), sa);
        return true;
    case 2:
    case 3:
    case 56:
    case 58:
    case 59:
    case 60:
    case 62:
    case 63: {
        if (rs)
            return false;
        static const char* names[8] = { "dsll", 0, "dsrl", "dsra", "dsll32", 0, "dsrl32",
                                        "dsra32" };
        name(fn == 2 ? "srl" : fn == 3 ? "sra" : names[fn - 56]);
        ops("%s,%s,%d", r(rd), r(rt), sa);
        return true;
    }
    case 4:
    case 6:
    case 7:
    case 20:
    case 22:
    case 23: {
        if (sa)
            return false;
        static const char* names[4] = { "sllv", 0, "srlv", "srav" };
        name(fn < 8 ? names[fn - 4] : fn == 20 ? "dsllv" : fn == 22 ? "dsrlv" : "dsrav");
        ops("%s,%s,%s", r(rd), r(rt), r(rs));
        return true;
    }
    case 1: {
        /* movf, movt rd,rs,cc */
        if (sa || (rt & 2))
            return false;
        name(rt & 1 ? "movt" : "movf");
        ops("%s,%s,$fcc%d", r(rd), r(rs), rt >> 2);
        return true;
    }
    case 8:
        if (rt || rd || sa)
            return false;
        name("jr");
        ops("%s", r(rs));
        out_->kind = rs == 31 ? INSN_RETURN : INSN_JUMP;
        out_->delay_slot = true;
        return true;
    case 9:
        if (rt || sa)
            return false;
        name("jalr");
        if (rd == 31)
            ops("%s", r(rs));
        else
            ops("%s,%s", r(rd), r(rs));
        out_->kind = INSN_CALL;
        out_->delay_slot = true;
        return true;
    case 12:
        name("syscall");
        if (w >> 6)
            ops("0x%x", (unsigned)(w >> 6) & 0xfffff);
        return true;
    case 13:
        name("break");
        if ((w >> 6) & 0xfffff)
            ops("%u", (unsigned)(w >> 16) & 0x3ff);
        if ((w >> 6) & 0x3ff)
            ops("%u,%u", (unsigned)(w >> 16) & 0x3ff, (unsigned)(w >> 6) & 0x3ff);
        return true;
    case 15:
        name("sync");
        return true;
    case 16:
    case 18:
        name(fn == 16 ? "mfhi" : "mflo");
        ops("%s", r(rd));
        return true;
    case 17:
    case 19:
        name(fn == 17 ? "mthi" : "mtlo");
        ops("%s", r(rs));
        return true;
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31: {
        static const char* names[8] = { "mult", "multu", "div", "divu",
                                        "dmult", "dmultu", "ddiv", "ddivu" };
        name(names[fn - 24]);
        /* MIPSpro and gcc write div with zero as its destination. */
        if ((fn == 26 || fn == 27 || fn == 30 || fn == 31) && rd == 0)
            ops("zero,%s,%s", r(rs), r(rt));
        else
            ops("%s,%s", r(rs), r(rt));
        return true;
    }
    case 48:
    case 49:
    case 50:
    case 51:
    case 52:
    case 54: {
        static const char* names[7] = { "tge", "tgeu", "tlt", "tltu", "teq", 0, "tne" };
        name(names[fn - 48]);
        if ((w >> 6) & 0x3ff)
            ops("%s,%s,%u", r(rs), r(rt), (unsigned)(w >> 6) & 0x3ff);
        else
            ops("%s,%s", r(rs), r(rt));
        return true;
    }
    default:
        break;
    }
    const char* m = k_special3[fn];
    if (!m || sa)
        return false;
    /* The short forms. */
    if ((fn == 37 || fn == 33 || fn == 45) && rt == 0) {
        name("move");
        ops("%s,%s", r(rd), r(rs));
        return true;
    }
    if ((fn == 34 || fn == 35 || fn == 46 || fn == 47) && rs == 0) {
        name(fn == 34 ? "neg" : fn == 35 ? "negu" : fn == 46 ? "dneg" : "dnegu");
        ops("%s,%s", r(rd), r(rt));
        return true;
    }
    if (fn == 39 && rt == 0) {
        name("not");
        ops("%s,%s", r(rd), r(rs));
        return true;
    }
    name(m);
    ops("%s,%s,%s", r(rd), r(rs), r(rt));
    return true;
}

bool Decoder::regimm(uint32_t w)
{
    int rs = (int)(w >> 21) & 31, rt = (int)(w >> 16) & 31;
    int16_t imm = (int16_t)(w & 0xffff);
    const char* m = k_regimm[rt];
    if (!m)
        return false;
    if (rt >= 8 && rt <= 14) {
        name(m);
        ops("%s,%d", r(rs), (int)imm);
        return true;
    }
    if (rt == 17 && rs == 0) {
        branch("bal", "", INSN_CALL, imm);
        return true;
    }
    branch(m, r(rs), rt >= 16 ? INSN_CALL : INSN_BRANCH, imm);
    return true;
}

bool Decoder::cop0(uint32_t w)
{
    int rs = (int)(w >> 21) & 31, rt = (int)(w >> 16) & 31, rd = (int)(w >> 11) & 31;
    if (rs == 0 || rs == 1 || rs == 4 || rs == 5) {
        static const char* names[6] = { "mfc0", "dmfc0", 0, 0, "mtc0", "dmtc0" };
        name(names[rs]);
        ops("%s,$%d", r(rt), rd);
        return true;
    }
    if (rs < 16)
        return false;
    switch (w & 63) {
    case 1: name("tlbr"); return true;
    case 2: name("tlbwi"); return true;
    case 6: name("tlbwr"); return true;
    case 8: name("tlbp"); return true;
    case 24: name("eret"); return true;
    default: return false;
    }
}

bool Decoder::cop1(uint32_t w)
{
    int fmt = (int)(w >> 21) & 31, ft = (int)(w >> 16) & 31, fs = (int)(w >> 11) & 31;
    int fd = (int)(w >> 6) & 31, fn = (int)(w & 63);
    switch (fmt) {
    case 0:
    case 1:
    case 4:
    case 5: {
        static const char* names[6] = { "mfc1", "dmfc1", 0, 0, "mtc1", "dmtc1" };
        name(names[fmt]);
        ops("%s,$f%d", r(ft), fs);
        return true;
    }
    case 2:
    case 6:
        name(fmt == 2 ? "cfc1" : "ctc1");
        ops("%s,$%d", r(ft), fs);
        return true;
    case 8: {
        /* bc1f, bc1t, bc1fl, bc1tl [cc,]offset */
        static const char* names[4] = { "bc1f", "bc1t", "bc1fl", "bc1tl" };
        char cc[16] = "";
        if (ft >> 2)
            snprintf(cc, sizeof(cc), "$fcc%d", ft >> 2);
        branch(names[ft & 3], cc, INSN_BRANCH, (int16_t)(w & 0xffff));
        return true;
    }
    default:
        break;
    }
    const char* f = fmt_name(fmt);
    if (!f)
        return false;
    char m[24];
    if (fn >= 48) {
        /* c.cond.fmt [cc,]fs,ft */
        snprintf(m, sizeof(m), "c.%s.%s", k_fp_cond[fn - 48], f);
        name(m);
        if (fd >> 2)
            ops("$fcc%d,$f%d,$f%d", fd >> 2, fs, ft);
        else
            ops("$f%d,$f%d", fs, ft);
        return true;
    }
    static const char* const two[16] = {
        0, 0, 0, 0, "sqrt", "abs", "mov", "neg",
        "round.l", "trunc.l", "ceil.l", "floor.l", "round.w", "trunc.w", "ceil.w", "floor.w"
    };
    if (fn < 4) {
        static const char* names[4] = { "add", "sub", "mul", "div" };
        snprintf(m, sizeof(m), "%s.%s", names[fn], f);
        name(m);
        ops("$f%d,$f%d,$f%d", fd, fs, ft);
        return true;
    }
    if (fn < 16) {
        if (ft)
            return false;
        snprintf(m, sizeof(m), "%s.%s", two[fn], f);
        name(m);
        ops("$f%d,$f%d", fd, fs);
        return true;
    }
    switch (fn) {
    case 17:
        snprintf(m, sizeof(m), "%s.%s", ft & 1 ? "movt" : "movf", f);
        name(m);
        ops("$f%d,$f%d,$fcc%d", fd, fs, ft >> 2);
        return true;
    case 18:
    case 19:
        snprintf(m, sizeof(m), "%s.%s", fn == 18 ? "movz" : "movn", f);
        name(m);
        ops("$f%d,$f%d,%s", fd, fs, r(ft));
        return true;
    case 21:
    case 22:
        snprintf(m, sizeof(m), "%s.%s", fn == 21 ? "recip" : "rsqrt", f);
        name(m);
        ops("$f%d,$f%d", fd, fs);
        return true;
    case 32:
    case 33:
    case 36:
    case 37: {
        static const char* to[6] = { "s", "d", 0, 0, "w", "l" };
        snprintf(m, sizeof(m), "cvt.%s.%s", to[fn - 32], f);
        name(m);
        ops("$f%d,$f%d", fd, fs);
        return true;
    }
    default:
        return false;
    }
}

bool Decoder::cop1x(uint32_t w)
{
    int base = (int)(w >> 21) & 31, index = (int)(w >> 16) & 31, fs = (int)(w >> 11) & 31;
    int fd = (int)(w >> 6) & 31, fn = (int)(w & 63);
    switch (fn) {
    case 0:
    case 1:
        name(fn == 0 ? "lwxc1" : "ldxc1");
        ops("$f%d,%s(%s)", fd, r(index), r(base));
        return true;
    case 8:
    case 9:
        name(fn == 8 ? "swxc1" : "sdxc1");
        ops("$f%d,%s(%s)", fs, r(index), r(base));
        return true;
    case 15:
        name("prefx");
        ops("%d,%s(%s)", fs, r(index), r(base));
        return true;
    default:
        break;
    }
    /* madd.fmt fd,fr,fs,ft */
    int kind = fn >> 3, fmt = fn & 7;
    if ((kind != 4 && kind != 5 && kind != 6 && kind != 7) || fmt > 1)
        return false;
    static const char* names[4] = { "madd", "msub", "nmadd", "nmsub" };
    char m[24];
    snprintf(m, sizeof(m), "%s.%s", names[kind - 4], fmt ? "d" : "s");
    name(m);
    ops("$f%d,$f%d,$f%d,$f%d", fd, base, fs, index);
    return true;
}

} /* namespace */

bool disassemble_mips(const Arch* a, Addr addr, const unsigned char* b, size_t n,
                      const Symbolizer* sym, Insn* out)
{
    if (n < 4)
        return false;
    uint32_t w = a->big_endian
                     ? (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]
                     : (uint32_t)b[3] << 24 | (uint32_t)b[2] << 16 | (uint32_t)b[1] << 8 | b[0];
    out->len = 4;
    memcpy(out->bytes, b, 4);
    out->kind = INSN_OTHER;
    Decoder d(a, addr, sym, out);
    return d.decode(w);
}

} /* namespace td */
