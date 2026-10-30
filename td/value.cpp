/*
 * Values (td/value.h): DWARF location expressions run against a frame,
 * a value's bytes and scalars, formatting in Visual C++'s manner, and
 * opening up structs, arrays and pointers.
 */
#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dwarf.h"

namespace td {

Value::Value()
    : type(0), where(VW_NONE), addr(0), reg(-1), bit_size(0), bit_offset(0), hex(false)
{
    memset(bytes, 0, sizeof(bytes));
}

namespace {

/* How much of a value formatting reads and shows. */
const int k_max_string = 64;    /* characters of a char * or char [] */
const int k_max_summary = 160;  /* characters of a struct's or array's one line */
const int k_max_elements = 10;  /* in an array's one line */

uint64_t get_uint(const Arch* a, const unsigned char* b, int n)
{
    uint64_t v = 0;
    int i;
    if (a->big_endian)
        for (i = 0; i < n; i++)
            v = (v << 8) | b[i];
    else
        for (i = n - 1; i >= 0; i--)
            v = (v << 8) | b[i];
    return v;
}

void put_uint(const Arch* a, unsigned char* b, int n, uint64_t v)
{
    int i;
    if (a->big_endian)
        for (i = n - 1; i >= 0; i--, v >>= 8)
            b[i] = (unsigned char)v;
    else
        for (i = 0; i < n; i++, v >>= 8)
            b[i] = (unsigned char)v;
}

bool host_big_endian()
{
    unsigned short x = 1;
    return *(unsigned char*)&x == 0;
}

/* A float of the target's into the host's (both IEEE). */
bool bytes_to_double(const Arch* a, const unsigned char* b, int n, double* out)
{
    unsigned char h[16];
    if (n > 16)
        return false;
    int i;
    for (i = 0; i < n; i++)
        h[i] = a->big_endian == host_big_endian() ? b[i] : b[n - 1 - i];
    if (n == (int)sizeof(float)) {
        float f;
        memcpy(&f, h, sizeof(f));
        *out = f;
        return true;
    }
    if (n == (int)sizeof(double)) {
        memcpy(out, h, sizeof(double));
        return true;
    }
    if (n == (int)sizeof(long double)) {
        long double l;
        memcpy(&l, h, sizeof(l));
        *out = (double)l;
        return true;
    }
    return false;
}

bool is_aggregate(const Type* s)
{
    return s && (s->kind == TK_STRUCT || s->kind == TK_UNION || s->kind == TK_CLASS);
}

bool is_char_type(const Type* t)
{
    const Type* s = strip(t);
    return s && s->kind == TK_INT && s->is_char && s->size == 1;
}

/* An expression for a part of e: parenthesized unless it is a plain
 * name, member or element chain. */
ttr::Str sub_expr(const ttr::Str& e, const char* op, const char* tail)
{
    Text t;
    const char* s = e.c_str();
    bool plain = true;
    const char* p;
    for (p = s; *p; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '.' || c == '[' || c == ']' || c == '$' || c == '@')
            continue;
        if (c == '-' && p[1] == '>') {
            p++;
            continue;
        }
        plain = false;
        break;
    }
    if (!strcmp(op, "*")) {
        t.add("*");
        if (!plain)
            t.add("(");
        t.add(s);
        if (!plain)
            t.add(")");
        return ttr::Str(t.c_str());
    }
    if (!plain)
        t.add("(");
    t.add(s);
    if (!plain)
        t.add(")");
    t.add(op);
    t.add(tail);
    return ttr::Str(t.c_str());
}

void add_char_literal(Text* out, int c, char quote)
{
    switch (c) {
    case 0: out->add("\\0"); return;
    case '\n': out->add("\\n"); return;
    case '\t': out->add("\\t"); return;
    case '\r': out->add("\\r"); return;
    case '\\': out->add("\\\\"); return;
    default: break;
    }
    if (c == quote) {
        out->add_char('\\');
        out->add_char((char)c);
    } else if (c >= 32 && c < 127) {
        out->add_char((char)c);
    } else {
        out->addf("\\x%02x", c & 0xff);
    }
}

} /* namespace */

/* ---- locations ---- */

/* Runs a location's operations for the frame's pc. depth: frame bases
 * are locations too, and do not nest further. */
bool Session::locate(const Frame& f, const Module* m, const Function* fn, const Location& loc,
                     Value* v, int depth)
{
    Info* info = m->info();
    const Arch* arch = target_->arch();
    Addr link = f.pc - m->bias() - (f.caller ? 1 : 0);
    const LocRange* r = 0;
    int i;
    for (i = 0; i < loc.count; i++) {
        const LocRange& x = info->range(loc.first + i);
        if ((!x.lo && !x.hi) || (link >= x.lo && link < x.hi)) {
            r = &x;
            break;
        }
    }
    if (!r) {
        v->error = loc.count ? "not available here" : "optimized away";
        return false;
    }
    uint64_t stack[64];
    int sp = 0;
    bool is_reg = false, is_value = false;
    int reg = -1;
    Addr mask = arch->ptr_size == 4 ? (Addr)0xffffffffu : ~(Addr)0;
#define TD_NEED(n)                                  \
    if (sp < (n)) {                                 \
        v->error = "bad location expression";     \
        return false;                               \
    }
#define TD_PUSH(x)                                      \
    do {                                                \
        uint64_t td_x = (x);                            \
        if (sp >= 64) {                                 \
            v->error = "location expression too deep"; \
            return false;                               \
        }                                               \
        stack[sp++] = td_x;                             \
    } while (0)
    int k;
    for (k = 0; k < r->count; k++) {
        const Op& o = info->op(r->first + k);
        unsigned a = o.atom;
        if (a >= DW_OP_lit0 && a <= DW_OP_lit31) {
            TD_PUSH((uint64_t)(a - DW_OP_lit0));
            continue;
        }
        if (a >= DW_OP_reg0 && a <= DW_OP_reg31) {
            is_reg = true;
            reg = (int)(a - DW_OP_reg0);
            continue;
        }
        if (a >= DW_OP_breg0 && a <= DW_OP_breg31) {
            int n = (int)(a - DW_OP_breg0);
            if (n >= MAX_REGS || !f.regs.valid[n]) {
                v->error = "its register was not saved";
                return false;
            }
            TD_PUSH(f.regs.r[n] + o.a1);
            continue;
        }
        switch (a) {
        case DW_OP_addr:
            TD_PUSH(o.a1 + m->bias());
            break;
        case DW_OP_regx:
            is_reg = true;
            reg = (int)o.a1;
            break;
        case DW_OP_bregx: {
            int n = (int)o.a1;
            if (n < 0 || n >= MAX_REGS || !f.regs.valid[n]) {
                v->error = "its register was not saved";
                return false;
            }
            TD_PUSH(f.regs.r[n] + o.a2);
            break;
        }
        case DW_OP_fbreg: {
            if (depth > 0 || !fn) {
                v->error = "bad frame base";
                return false;
            }
            Value fb;
            if (!locate(f, m, fn, fn->frame_base, &fb, depth + 1)) {
                v->error = fb.error;
                return false;
            }
            /* A register frame base means what the register holds. */
            Addr base = fb.where == VW_MEMORY ? fb.addr : get_uint(arch, fb.bytes, 8);
            TD_PUSH(base + o.a1);
            break;
        }
        case DW_OP_call_frame_cfa: {
            Addr c = 0;
            if (!m->cfa(f.regs, f.caller, &c)) {
                v->error = "no frame information here";
                return false;
            }
            TD_PUSH(c);
            break;
        }
        case DW_OP_const1u:
        case DW_OP_const2u:
        case DW_OP_const4u:
        case DW_OP_const8u:
        case DW_OP_constu:
        case DW_OP_const1s:
        case DW_OP_const2s:
        case DW_OP_const4s:
        case DW_OP_const8s:
        case DW_OP_consts:
            TD_PUSH(o.a1);
            break;
        case DW_OP_dup:
            TD_NEED(1);
            TD_PUSH(stack[sp - 1]);
            break;
        case DW_OP_drop:
            TD_NEED(1);
            sp--;
            break;
        case DW_OP_over:
            TD_NEED(2);
            TD_PUSH(stack[sp - 2]);
            break;
        case DW_OP_pick:
            TD_NEED((int)o.a1 + 1);
            TD_PUSH(stack[sp - 1 - (int)o.a1]);
            break;
        case DW_OP_swap: {
            TD_NEED(2);
            uint64_t t = stack[sp - 1];
            stack[sp - 1] = stack[sp - 2];
            stack[sp - 2] = t;
            break;
        }
        case DW_OP_rot: {
            TD_NEED(3);
            uint64_t t = stack[sp - 1];
            stack[sp - 1] = stack[sp - 2];
            stack[sp - 2] = stack[sp - 3];
            stack[sp - 3] = t;
            break;
        }
        case DW_OP_deref:
        case DW_OP_deref_size: {
            TD_NEED(1);
            int n = a == DW_OP_deref ? arch->ptr_size : (int)o.a1;
            uint64_t x = 0;
            if (n < 1 || n > 8 || !target_->read_uint(stack[sp - 1] & mask, n, &x)) {
                v->error = "cannot read its location";
                return false;
            }
            stack[sp - 1] = x;
            break;
        }
        case DW_OP_abs:
            TD_NEED(1);
            if ((int64_t)stack[sp - 1] < 0)
                stack[sp - 1] = (uint64_t)0 - stack[sp - 1];
            break;
        case DW_OP_neg:
            TD_NEED(1);
            stack[sp - 1] = (uint64_t)0 - stack[sp - 1];
            break;
        case DW_OP_not:
            TD_NEED(1);
            stack[sp - 1] = ~stack[sp - 1];
            break;
        case DW_OP_plus_uconst:
            TD_NEED(1);
            stack[sp - 1] += o.a1;
            break;
        case DW_OP_and:
        case DW_OP_or:
        case DW_OP_xor:
        case DW_OP_plus:
        case DW_OP_minus:
        case DW_OP_mul:
        case DW_OP_div:
        case DW_OP_mod:
        case DW_OP_shl:
        case DW_OP_shr:
        case DW_OP_shra:
        case DW_OP_eq:
        case DW_OP_ne:
        case DW_OP_lt:
        case DW_OP_le:
        case DW_OP_gt:
        case DW_OP_ge: {
            TD_NEED(2);
            uint64_t y = stack[--sp], x = stack[sp - 1], z = 0;
            switch (a) {
            case DW_OP_and: z = x & y; break;
            case DW_OP_or: z = x | y; break;
            case DW_OP_xor: z = x ^ y; break;
            case DW_OP_plus: z = x + y; break;
            case DW_OP_minus: z = x - y; break;
            case DW_OP_mul: z = x * y; break;
            case DW_OP_div:
            case DW_OP_mod:
                if (!y) {
                    v->error = "bad location expression";
                    return false;
                }
                z = a == DW_OP_div ? (uint64_t)((int64_t)x / (int64_t)y) : x % y;
                break;
            case DW_OP_shl: z = x << (y & 63); break;
            case DW_OP_shr: z = x >> (y & 63); break;
            case DW_OP_shra: z = (uint64_t)((int64_t)x >> (y & 63)); break;
            case DW_OP_eq: z = x == y; break;
            case DW_OP_ne: z = x != y; break;
            case DW_OP_lt: z = (int64_t)x < (int64_t)y; break;
            case DW_OP_le: z = (int64_t)x <= (int64_t)y; break;
            case DW_OP_gt: z = (int64_t)x > (int64_t)y; break;
            case DW_OP_ge: z = (int64_t)x >= (int64_t)y; break;
            default: break;
            }
            stack[sp - 1] = z;
            break;
        }
        case DW_OP_skip:
        case DW_OP_bra: {
            bool jump = true;
            if (a == DW_OP_bra) {
                TD_NEED(1);
                jump = stack[--sp] != 0;
            }
            if (!jump)
                break;
            /* The target, in bytes after this 3-byte operation. */
            uint64_t to = o.offset + 3 + (uint64_t)(int64_t)(int16_t)o.a1;
            int j;
            for (j = 0; j < r->count; j++)
                if (info->op(r->first + j).offset == to)
                    break;
            if (j >= r->count) {
                if (j == r->count && to > o.offset) {
                    k = r->count;   /* past the end */
                    break;
                }
                v->error = "bad location expression";
                return false;
            }
            k = j - 1;
            break;
        }
        case DW_OP_nop:
            break;
        case DW_OP_stack_value:
            is_value = true;
            break;
        case DW_OP_implicit_value: {
            size_t n = (size_t)o.a1;
            const unsigned char* p = (const unsigned char*)(size_t)o.a2;
            if (!p || n > sizeof(v->bytes)) {
                v->error = "a value td cannot show";
                return false;
            }
            memcpy(v->bytes, p, n);
            v->where = VW_CONSTANT;
            return true;
        }
        case DW_OP_piece:
            /* One piece covering it all is the plain location. */
            if (k == r->count - 1 && (!v->type || o.a1 >= type_size(v->type)))
                break;
            v->error = "split into pieces (optimized code)";
            return false;
        case DW_OP_GNU_push_tls_address:
        case DW_OP_form_tls_address:
            v->error = "thread-local (not yet)";
            return false;
        case DW_OP_entry_value:
        case DW_OP_GNU_entry_value:
            v->error = "optimized away";
            return false;
        default:
            v->error = "a location td cannot read yet";
            return false;
        }
    }
#undef TD_NEED
#undef TD_PUSH
    if (is_reg) {
        if (reg < 0 || reg >= MAX_REGS || !f.regs.valid[reg]) {
            v->error = "in a register that was not saved";
            return false;
        }
        v->where = VW_REGISTER;
        v->reg = reg;
        /* The low bytes of the register, in the target's order. */
        uint64_t x = f.regs.r[reg];
        int n = (int)type_size(v->type);
        if (n <= 0 || n > 8)
            n = arch->reg_size;
        put_uint(arch, v->bytes, n, x);
        return true;
    }
    if (!sp) {
        v->error = "optimized away";
        return false;
    }
    if (is_value) {
        int n = (int)type_size(v->type);
        if (n <= 0 || n > 8)
            n = 8;
        put_uint(arch, v->bytes, n, stack[sp - 1]);
        v->where = VW_CONSTANT;
        return true;
    }
    v->where = VW_MEMORY;
    v->addr = stack[sp - 1] & mask;
    return true;
}

Value Session::variable_value(const Frame& f, const Module* m, const Function* fn,
                              const Variable& var)
{
    Value v;
    v.name = var.name;
    v.expr = var.name;
    v.type = var.type;
    if (var.has_const) {
        int n = (int)type_size(var.type);
        if (n <= 0 || n > 8)
            n = 8;
        put_uint(target_->arch(), v.bytes, n, (uint64_t)var.const_value);
        v.where = VW_CONSTANT;
        return v;
    }
    locate(f, m, fn, var.loc, &v, 0);
    return v;
}

namespace {

bool block_in_scope(const Function* fn, int b, Addr link)
{
    while (b >= 0) {
        const Block& bl = fn->blocks[b];
        if ((bl.lo || bl.hi) && (link < bl.lo || link >= bl.hi))
            return false;
        b = bl.parent;
    }
    return true;
}

int block_depth(const Function* fn, int b)
{
    int d = 0;
    while (b >= 0) {
        d++;
        b = fn->blocks[b].parent;
    }
    return d;
}

/* The variables in scope at link: inner ones hide outer ones of the same
 * name; arguments first. */
void in_scope(const Function* fn, Addr link, ttr::Vector<int>* out)
{
    int pass;
    for (pass = 0; pass < 2; pass++) {
        int i;
        for (i = 0; i < fn->vars.size(); i++) {
            const Variable& v = fn->vars[i];
            if (v.param != (pass == 0) || !block_in_scope(fn, v.block, link))
                continue;
            /* Hidden by another in scope, deeper in? */
            bool hidden = false;
            int j;
            for (j = 0; j < fn->vars.size() && !hidden; j++) {
                const Variable& w = fn->vars[j];
                if (j != i && !strcmp(w.name, v.name) && block_in_scope(fn, w.block, link) &&
                    block_depth(fn, w.block) > block_depth(fn, v.block))
                    hidden = true;
            }
            if (!hidden)
                out->push_back(i);
        }
    }
}

} /* namespace */

int Session::locals(const Frame& f, ttr::Vector<Value>* out)
{
    const Module* m = f.module;
    Info* info = m ? m->info() : 0;
    if (!info || !alive_)
        return 0;
    Addr link = f.pc - m->bias() - (f.caller ? 1 : 0);
    const Function* fn = info->function_at(link);
    if (!fn)
        return 0;
    ttr::Vector<int> idx;
    in_scope(fn, link, &idx);
    int i;
    for (i = 0; i < idx.size(); i++)
        out->push_back(variable_value(f, m, fn, fn->vars[idx[i]]));
    return idx.size();
}

/* A name in a frame: a local, then a global (this module first), an
 * enumerator, a function. */
bool Session::lookup(const Frame& f, const char* name, Value* out)
{
    const Module* m = f.module;
    Info* info = m ? m->info() : 0;
    uint64_t cu = 0;
    if (info) {
        Addr link = f.pc - m->bias() - (f.caller ? 1 : 0);
        const Function* fn = info->function_at(link);
        if (fn) {
            cu = fn->cu;
            ttr::Vector<int> idx;
            in_scope(fn, link, &idx);
            int i;
            for (i = 0; i < idx.size(); i++)
                if (!strcmp(fn->vars[idx[i]].name, name)) {
                    const Variable& var = fn->vars[idx[i]];
                    *out = variable_value(f, m, fn, var);
                    /* A function's static (at a DW_OP_addr) is not the
                     * frame's. */
                    bool fixed = false;
                    if (var.loc.count == 1) {
                        const LocRange& r = info->range(var.loc.first);
                        fixed = r.count == 1 && info->op(r.first).atom == DW_OP_addr;
                    }
                    if (!fixed && !var.has_const)
                        used_local_ = true;
                    return true;
                }
        }
    }
    int pass, i;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < mods_.size(); i++) {
            const Module* mm = mods_[i];
            if ((pass == 0) != (mm == m))
                continue;
            Info* in = mm->info();
            if (!in)
                continue;
            const Variable* g = in->global(name, mm == m ? cu : 0);
            if (g) {
                Frame gf = f;
                gf.module = mm;
                *out = variable_value(gf, mm, 0, *g);
                return true;
            }
        }
    for (i = 0; i < mods_.size(); i++) {
        Info* in = mods_[i]->info();
        int64_t val = 0;
        Type* t = 0;
        if (in && in->enumerator(name, &val, &t)) {
            Value v;
            v.name = name;
            v.expr = name;
            v.type = t;
            v.where = VW_CONSTANT;
            int n = (int)type_size(t);
            if (n <= 0 || n > 8)
                n = 4;
            put_uint(target_->arch(), v.bytes, n, (uint64_t)val);
            *out = v;
            return true;
        }
    }
    for (i = 0; i < mods_.size(); i++) {
        Addr s = 0, e = 0;
        if (mods_[i]->function_named(name, &s, &e)) {
            Value v;
            v.name = name;
            v.expr = name;
            v.type = synth(TK_FUNCTION, 0, 0, false, false);
            v.where = VW_MEMORY;
            v.addr = s;
            *out = v;
            return true;
        }
    }
    return false;
}

/* ---- types expressions make ---- */

Type* Session::synth(TypeKind kind, const char* name, uint64_t size, bool is_signed,
                     bool is_char)
{
    int i;
    for (i = 0; i < synth_.size(); i++) {
        Type* t = synth_[i];
        if (t->kind == kind && t->size == size && t->is_signed == is_signed &&
            t->is_char == is_char && !t->target &&
            ((!t->name && !name) || (t->name && name && !strcmp(t->name, name))))
            return t;
    }
    Type* t = new Type;
    t->kind = kind;
    /* Names given here are string literals. */
    t->name = name;
    t->size = size;
    t->is_signed = is_signed;
    t->is_char = is_char;
    synth_.push_back(t);
    return t;
}

Type* Session::pointer_to(const Type* target)
{
    int i;
    for (i = 0; i < synth_.size(); i++)
        if (synth_[i]->kind == TK_POINTER && synth_[i]->target == target)
            return synth_[i];
    Type* t = new Type;
    t->kind = TK_POINTER;
    t->size = (uint64_t)target_->arch()->ptr_size;
    t->target = (Type*)target;
    synth_.push_back(t);
    return t;
}

Type* Session::array_of(const Type* elem, long n)
{
    int i;
    for (i = 0; i < synth_.size(); i++)
        if (synth_[i]->kind == TK_ARRAY && synth_[i]->target == elem && synth_[i]->count == n)
            return synth_[i];
    Type* t = new Type;
    t->kind = TK_ARRAY;
    t->target = (Type*)elem;
    t->count = n;
    t->size = type_size(elem) * (uint64_t)n;
    synth_.push_back(t);
    return t;
}

Type* Session::find_type(const Frame& f, const char* name)
{
    int pass, i;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < mods_.size(); i++) {
            if ((pass == 0) != (mods_[i] == f.module))
                continue;
            Info* in = mods_[i]->info();
            Type* t = in ? in->type_named(name) : 0;
            if (t)
                return t;
        }
    return 0;
}

/* ---- reading ---- */

bool Session::value_bytes(const Value& v, void* buf, size_t n)
{
    if (v.where == VW_MEMORY)
        return alive_ && target_->read(v.addr, buf, n);
    if (v.where == VW_REGISTER || v.where == VW_CONSTANT) {
        if (n > sizeof(v.bytes))
            return false;
        memcpy(buf, v.bytes, n);
        return true;
    }
    return false;
}

/* An integer, character, bool, enum or pointer: its bits, sign-extended
 * if its type is signed. */
bool Session::scalar(const Value& v, uint64_t* bits)
{
    const Type* s = strip(v.type);
    const Arch* arch = target_->arch();
    if (!s)
        return false;
    if (v.bit_size) {
        uint64_t first = v.bit_offset / 8;
        int shift = (int)(v.bit_offset % 8);
        int n = (shift + v.bit_size + 7) / 8;
        unsigned char b[9];
        if (n > 8)
            return false;
        if (v.where == VW_CONSTANT && first + (uint64_t)n <= sizeof(v.bytes))
            memcpy(b, v.bytes + first, (size_t)n);
        else if (v.where != VW_MEMORY || !target_->read(v.addr + first, b, (size_t)n))
            return false;
        uint64_t x = get_uint(arch, b, n);
        if (arch->big_endian)
            x >>= n * 8 - shift - v.bit_size;
        else
            x >>= shift;
        uint64_t mask = v.bit_size >= 64 ? ~(uint64_t)0 : (((uint64_t)1 << v.bit_size) - 1);
        x &= mask;
        if (s->is_signed && v.bit_size < 64 && (x >> (v.bit_size - 1)) & 1)
            x |= ~mask;
        *bits = x;
        return true;
    }
    int n = (int)s->size;
    if (n < 1 || n > 8)
        return false;
    unsigned char b[8];
    if (!value_bytes(v, b, (size_t)n))
        return false;
    uint64_t x = get_uint(arch, b, n);
    if ((s->kind == TK_INT || s->kind == TK_ENUM) && s->is_signed && n < 8 &&
        ((x >> (n * 8 - 1)) & 1))
        x |= ~(((uint64_t)1 << (n * 8)) - 1);
    *bits = x;
    return true;
}

bool Session::as_double(const Value& v, double* d)
{
    const Type* s = strip(v.type);
    if (!s)
        return false;
    if (s->kind == TK_FLOAT) {
        unsigned char b[16];
        int n = (int)s->size;
        if (n < 1 || n > 16 || !value_bytes(v, b, (size_t)n))
            return false;
        return bytes_to_double(target_->arch(), b, n, d);
    }
    uint64_t x = 0;
    if (!scalar(v, &x))
        return false;
    *d = s->is_signed ? (double)(int64_t)x : (double)x;
    return true;
}

/* ---- parts ---- */

Value Session::member_value(const Value& v, const Member& m, uint64_t base)
{
    Value c;
    c.name = m.name ? m.name : m.type && m.type->name ? m.type->name : "<anonymous>";
    c.expr = m.name ? sub_expr(v.expr, ".", m.name) : v.expr;
    c.type = m.type;
    c.hex = v.hex;
    if (v.where == VW_MEMORY) {
        c.where = VW_MEMORY;
        if (m.bit_size) {
            c.addr = v.addr;
            c.bit_size = m.bit_size;
            c.bit_offset = base * 8 + m.bit_offset;
        } else {
            c.addr = v.addr + base + m.offset;
        }
        return c;
    }
    uint64_t size = type_size(m.type);
    if ((v.where == VW_CONSTANT || v.where == VW_REGISTER) && m.bit_size &&
        (base * 8 + m.bit_offset + (uint64_t)m.bit_size + 7) / 8 <= sizeof(v.bytes)) {
        /* The bits from the bytes (a watchpoint's old value). */
        c.where = VW_CONSTANT;
        memcpy(c.bytes, v.bytes, sizeof(c.bytes));
        c.bit_size = m.bit_size;
        c.bit_offset = base * 8 + m.bit_offset;
        return c;
    }
    if ((v.where == VW_CONSTANT || v.where == VW_REGISTER) && !m.bit_size &&
        base + m.offset + size <= sizeof(v.bytes)) {
        c.where = VW_CONSTANT;
        memcpy(c.bytes, v.bytes + base + m.offset, (size_t)size);
        return c;
    }
    c.error = v.error.empty() ? ttr::Str("not in memory") : v.error;
    return c;
}

Value Session::deref(const Value& p)
{
    Value c;
    const Type* s = strip(p.type);
    c.name = sub_expr(p.name, "*", "");
    c.expr = sub_expr(p.expr, "*", "");
    c.hex = p.hex;
    if (!s || (s->kind != TK_POINTER && s->kind != TK_REFERENCE)) {
        c.error = "not a pointer";
        return c;
    }
    c.type = s->target;
    uint64_t a = 0;
    if (!scalar(p, &a)) {
        c.error = p.error.empty() ? ttr::Str("cannot read the pointer") : p.error;
        return c;
    }
    if (!strip(s->target)) {
        c.error = "points to void";
        return c;
    }
    c.where = VW_MEMORY;
    c.addr = a;
    return c;
}

bool Session::has_children(const Value& v)
{
    if (!v.ok())
        return false;
    const Type* s = strip(v.type);
    if (!s)
        return false;
    if (is_aggregate(s))
        return s->nmembers > 0;
    if (s->kind == TK_ARRAY)
        return s->count > 0;
    if (s->kind == TK_POINTER || s->kind == TK_REFERENCE) {
        const Type* t = strip(s->target);
        if (!t || t->kind == TK_FUNCTION || t->kind == TK_VOID)
            return false;
        uint64_t a = 0;
        return scalar(v, &a) && a != 0;
    }
    return false;
}

int Session::children(const Value& v, ttr::Vector<Value>* out, int max)
{
    if (!has_children(v))
        return 0;
    const Type* s = strip(v.type);
    int n0 = out->size();
    if (is_aggregate(s)) {
        int i;
        for (i = 0; i < s->nmembers && out->size() - n0 < max; i++)
            out->push_back(member_value(v, s->members[i], 0));
    } else if (s->kind == TK_ARRAY) {
        uint64_t es = type_size(s->target);
        long i;
        for (i = 0; i < s->count && out->size() - n0 < max; i++) {
            Value c;
            char idx[32];
            snprintf(idx, sizeof(idx), "[%ld]", i);
            c.name = idx;
            c.expr = sub_expr(v.expr, idx, "");
            c.type = s->target;
            c.hex = v.hex;
            if (v.where == VW_MEMORY) {
                c.where = VW_MEMORY;
                c.addr = v.addr + es * (uint64_t)i;
            } else if (es * (uint64_t)(i + 1) <= sizeof(v.bytes)) {
                c.where = VW_CONSTANT;
                memcpy(c.bytes, v.bytes + es * (uint64_t)i, (size_t)es);
            } else {
                c.error = "not in memory";
            }
            out->push_back(c);
        }
    } else {
        /* A pointer: a struct's members directly, as Visual C++ shows
         * them; anything else as *p. */
        Value d = deref(v);
        const Type* t = strip(d.type);
        if (d.ok() && is_aggregate(t)) {
            int i;
            for (i = 0; i < t->nmembers && out->size() - n0 < max; i++) {
                Value c = member_value(d, t->members[i], 0);
                if (t->members[i].name)
                    c.expr = sub_expr(v.expr, "->", t->members[i].name);
                out->push_back(c);
            }
        } else {
            out->push_back(d);
        }
    }
    return out->size() - n0;
}

/* ---- formatting ---- */

ttr::Str Session::format(const Value& v)
{
    Text t;
    format_into(&t, v, 0);
    return ttr::Str(t.c_str());
}

void Session::format_into(Text* out, const Value& v, int depth)
{
    if (!v.ok()) {
        out->add("<");
        out->add(v.error.empty() ? "no value" : v.error.c_str());
        out->add(">");
        return;
    }
    const Arch* arch = target_->arch();
    const Type* s = strip(v.type);
    if (!s) {
        out->add("<void>");
        return;
    }
    switch (s->kind) {
    case TK_INT:
    case TK_BOOL:
    case TK_ENUM: {
        uint64_t x = 0;
        if (!scalar(v, &x)) {
            out->add("<cannot read>");
            return;
        }
        if (s->kind == TK_BOOL) {
            if (x == 0 || x == 1)
                out->add(x ? "true" : "false");
            else
                out->add_uint(x);
            return;
        }
        if (s->kind == TK_ENUM) {
            int i;
            for (i = 0; i < s->nenums; i++) {
                uint64_t e = (uint64_t)s->enums[i].value;
                if (s->size < 8)
                    e &= ((uint64_t)1 << (s->size * 8)) - 1;
                uint64_t y = s->size < 8 ? x & (((uint64_t)1 << (s->size * 8)) - 1) : x;
                if (e == y) {
                    out->add(s->enums[i].name);
                    return;
                }
            }
        }
        int bytes = v.bit_size ? (v.bit_size + 3) / 4 : (int)s->size * 2;
        if (v.hex) {
            uint64_t y = x;
            if (s->size < 8)
                y &= ((uint64_t)1 << (s->size * 8)) - 1;
            out->add_hex(y, v.bit_size ? 1 : bytes);
        } else if (s->is_signed) {
            out->add_int((int64_t)x);
        } else {
            out->add_uint(x);
        }
        if (s->is_char && s->size == 1) {
            out->add(" '");
            add_char_literal(out, (int)(x & 0xff), '\'');
            out->add("'");
        }
        return;
    }
    case TK_FLOAT: {
        double d = 0;
        if (!as_double(v, &d)) {
            out->add("<cannot read>");
            return;
        }
        /* The shortest that reads back the same. */
        char b[64];
        int prec = s->size == 4 ? 6 : 15, last = s->size == 4 ? 9 : 17;
        for (; prec <= last; prec++) {
            snprintf(b, sizeof(b), "%.*g", prec, d);
            if (s->size == 4) {
                if ((float)strtod(b, 0) == (float)d)
                    break;
            } else if (strtod(b, 0) == d) {
                break;
            }
        }
        out->add(b);
        return;
    }
    case TK_POINTER:
    case TK_REFERENCE: {
        uint64_t a = 0;
        if (!scalar(v, &a)) {
            out->add("<cannot read>");
            return;
        }
        const Type* t = strip(s->target);
        if (s->kind == TK_REFERENCE) {
            Value d = deref(v);
            format_into(out, d, depth);
            return;
        }
        out->add_hex(a, arch->ptr_size * 2);
        if (!a)
            return;
        if (is_char_type(s->target)) {
            unsigned char b[k_max_string + 1];
            int n = 0;
            while (n < k_max_string + 1 && target_->read(a + (Addr)n, b + n, 1) && b[n])
                n++;
            if (n == 0 && !target_->read(a, b, 1)) {
                out->add(" <Bad Ptr>");
                return;
            }
            out->add(" \"");
            int i;
            for (i = 0; i < n && i < k_max_string; i++)
                add_char_literal(out, b[i], '"');
            out->add(n > k_max_string ? "\"..." : "\"");
            return;
        }
        if (t && t->kind == TK_FUNCTION) {
            const Module* m = module_at(a);
            Addr fs = 0;
            const char* fname = m ? m->function_at(a, &fs, 0) : 0;
            if (fname) {
                out->add(" <");
                out->add(fname);
                if (a != fs) {
                    out->add("+");
                    out->add_uint(a - fs);
                }
                out->add(">");
            }
            return;
        }
        if (depth == 0 && is_aggregate(t)) {
            unsigned char b;
            if (!target_->read(a, &b, 1)) {
                out->add(" <Bad Ptr>");
                return;
            }
            out->add(" ");
            format_into(out, deref(v), depth + 1);
        }
        return;
    }
    case TK_FUNCTION: {
        out->add_hex(v.addr, arch->ptr_size * 2);
        const Module* m = module_at(v.addr);
        const char* fname = m ? m->function_at(v.addr, 0, 0) : 0;
        if (fname) {
            out->add(" <");
            out->add(fname);
            out->add(">");
        }
        return;
    }
    case TK_ARRAY: {
        if (is_char_type(s->target) && s->count > 0) {
            int n = s->count < k_max_string ? (int)s->count : k_max_string;
            unsigned char b[k_max_string];
            Value e = v;
            if (!value_bytes(e, b, (size_t)n)) {
                out->add("<cannot read>");
                return;
            }
            int len = 0;
            while (len < n && b[len])
                len++;
            out->add("\"");
            int i;
            for (i = 0; i < len; i++)
                add_char_literal(out, b[i], '"');
            out->add(len == n && s->count > n ? "\"..." : "\"");
            return;
        }
        if (depth > 1) {
            out->add("{...}");
            return;
        }
        out->add("{");
        ttr::Vector<Value> kids;
        children(v, &kids, k_max_elements + 1);
        int i;
        for (i = 0; i < kids.size() && i < k_max_elements; i++) {
            if (i)
                out->add(", ");
            format_into(out, kids[i], depth + 1);
            if ((int)out->length() > k_max_summary)
                break;
        }
        if (kids.size() > k_max_elements || i < kids.size())
            out->add(", ...");
        out->add("}");
        return;
    }
    case TK_STRUCT:
    case TK_UNION:
    case TK_CLASS: {
        if (s->decl && !s->nmembers) {
            out->add("{incomplete type}");
            return;
        }
        if (depth > 1) {
            out->add("{...}");
            return;
        }
        size_t start = out->length();
        out->add("{");
        ttr::Vector<Value> kids;
        children(v, &kids, 64);
        int i;
        for (i = 0; i < kids.size(); i++) {
            if (i)
                out->add(" ");
            out->add(kids[i].name.c_str());
            out->add("=");
            format_into(out, kids[i], depth + 1);
            if (out->length() - start > (size_t)k_max_summary) {
                i++;
                break;
            }
        }
        if (i < kids.size())
            out->add(" ...");
        out->add("}");
        return;
    }
    case TK_VOID:
    default: {
        /* Bytes of a type td does not show. */
        int n = (int)s->size;
        unsigned char b[16];
        if (n <= 0 || n > 16 || !value_bytes(v, b, (size_t)n)) {
            out->add("?");
            return;
        }
        int i;
        for (i = 0; i < n; i++)
            out->addf(i ? " %02x" : "%02x", b[i]);
        return;
    }
    }
}

} /* namespace td */
