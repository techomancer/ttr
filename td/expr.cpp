/*
 * The expression evaluator (Session::evaluate): C expressions over the
 * program's variables, as Visual C++'s Watch takes them. Recursive
 * descent over C's precedence levels, from || down to postfix [] . ->.
 * No assignments and no function calls: watching changes nothing.
 */
#include "session.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace td {

namespace {

enum Tok {
    T_END,
    T_NUM,      /* integer */
    T_FLT,      /* floating */
    T_CHR,      /* 'a' */
    T_ID,
    T_REG,      /* $sp, @eax */
    T_OP,       /* text holds it: + - -> && ... */
    T_BAD
};

const char* const k_ops[] = {
    "->", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
    "+", "-", "*", "/", "%", "<", ">", "&", "|", "^", "!", "~",
    "(", ")", "[", "]", ".", "?", ":", 0
};

/* Words that start a type name in a cast or sizeof. */
const char* const k_type_words[] = {
    "struct", "union", "enum", "class", "const", "volatile", "unsigned", "signed",
    "char", "short", "int", "long", "float", "double", "void", "bool", "_Bool", 0
};

bool is_type_word(const char* w)
{
    int i;
    for (i = 0; k_type_words[i]; i++)
        if (!strcmp(w, k_type_words[i]))
            return true;
    return false;
}

} /* namespace */

class Evaluator {
public:
    Evaluator(Session* s, const Frame& f, const char* text)
        : s_(s), f_(f), p_(text), tok_(T_END), num_(0), flt_(0), unsigned_(false)
    {
        text_[0] = 0;
        next();
    }

    Value run();

private:
    void next();
    bool is_op(const char* op) const { return tok_ == T_OP && !strcmp(text_, op); }
    bool accept(const char* op)
    {
        if (!is_op(op))
            return false;
        next();
        return true;
    }

    Value fail(const char* msg);
    Value cond();
    Value binary(int level);
    Value unary();
    Value postfix(Value v);
    Value primary();
    bool type_ahead();
    const Type* parse_type();

    /* Values from numbers. */
    const Arch* arch() { return s_->target()->arch(); }
    Value make_int(const Type* t, uint64_t x);
    Value make_double(double d);
    Type* int_type(int size, bool is_signed);
    Type* double_type() { return s_->synth(TK_FLOAT, "double", 8, true, false); }
    Value decay(const Value& v);
    Value member(const Value& v, const char* name);
    Value arith(const char* op, const Value& a, const Value& b);
    Value cast(const Type* t, const Value& v);

    Session* s_;
    const Frame& f_;
    const char* p_;
    Tok tok_;
    char text_[256];
    uint64_t num_;
    double flt_;
    bool unsigned_;
    ttr::Str error_;
};

void Evaluator::next()
{
    while (*p_ == ' ' || *p_ == '\t')
        p_++;
    text_[0] = 0;
    if (!*p_) {
        tok_ = T_END;
        return;
    }
    char c = *p_;
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)p_[1]))) {
        const char* start = p_;
        bool flt = false;
        if (c == '0' && (p_[1] == 'x' || p_[1] == 'X')) {
            p_ += 2;
            while (isxdigit((unsigned char)*p_))
                p_++;
        } else {
            while (isdigit((unsigned char)*p_))
                p_++;
            if (*p_ == '.') {
                flt = true;
                p_++;
                while (isdigit((unsigned char)*p_))
                    p_++;
            }
            if (*p_ == 'e' || *p_ == 'E') {
                flt = true;
                p_++;
                if (*p_ == '+' || *p_ == '-')
                    p_++;
                while (isdigit((unsigned char)*p_))
                    p_++;
            }
        }
        size_t n = (size_t)(p_ - start);
        if (n >= sizeof(text_))
            n = sizeof(text_) - 1;
        memcpy(text_, start, n);
        text_[n] = 0;
        unsigned_ = false;
        if (flt) {
            flt_ = strtod(text_, 0);
            if (*p_ == 'f' || *p_ == 'F' || *p_ == 'l' || *p_ == 'L')
                p_++;
            tok_ = T_FLT;
            return;
        }
        const char* end = 0;
        num_ = parse_number(text_, &end);
        while (*p_ == 'u' || *p_ == 'U' || *p_ == 'l' || *p_ == 'L') {
            if (*p_ == 'u' || *p_ == 'U')
                unsigned_ = true;
            p_++;
        }
        tok_ = end && !*end ? T_NUM : T_BAD;
        return;
    }
    if (isalpha((unsigned char)c) || c == '_' || ((c == '$' || c == '@') && p_[1])) {
        const char* start = p_;
        tok_ = T_ID;
        if (c == '$' || c == '@') {
            tok_ = T_REG;
            p_++;
            start = p_;
        }
        while (isalnum((unsigned char)*p_) || *p_ == '_')
            p_++;
        /* C++ names: a::b */
        while (tok_ == T_ID && p_[0] == ':' && p_[1] == ':' &&
               (isalpha((unsigned char)p_[2]) || p_[2] == '_')) {
            p_ += 2;
            while (isalnum((unsigned char)*p_) || *p_ == '_')
                p_++;
        }
        size_t n = (size_t)(p_ - start);
        if (n >= sizeof(text_))
            n = sizeof(text_) - 1;
        memcpy(text_, start, n);
        text_[n] = 0;
        return;
    }
    if (c == '\'') {
        p_++;
        int ch = (unsigned char)*p_;
        if (ch == '\\') {
            p_++;
            switch (*p_) {
            case 'n': ch = '\n'; break;
            case 't': ch = '\t'; break;
            case 'r': ch = '\r'; break;
            case '0': ch = 0; break;
            case '\\': ch = '\\'; break;
            case '\'': ch = '\''; break;
            default: ch = (unsigned char)*p_; break;
            }
        }
        if (*p_)
            p_++;
        if (*p_ != '\'') {
            tok_ = T_BAD;
            return;
        }
        p_++;
        num_ = (uint64_t)ch;
        tok_ = T_CHR;
        return;
    }
    int i;
    for (i = 0; k_ops[i]; i++) {
        size_t n = strlen(k_ops[i]);
        if (!strncmp(p_, k_ops[i], n)) {
            strcpy(text_, k_ops[i]);
            p_ += n;
            tok_ = T_OP;
            return;
        }
    }
    text_[0] = c;
    text_[1] = 0;
    tok_ = T_BAD;
}

Value Evaluator::fail(const char* msg)
{
    if (error_.empty())
        error_ = msg;
    Value v;
    v.error = error_;
    return v;
}

Type* Evaluator::int_type(int size, bool is_signed)
{
    const char* n;
    switch (size) {
    case 1: n = is_signed ? "signed char" : "unsigned char"; break;
    case 2: n = is_signed ? "short" : "unsigned short"; break;
    case 8: n = is_signed ? "long long" : "unsigned long long"; break;
    default:
        size = 4;
        n = is_signed ? "int" : "unsigned int";
        break;
    }
    return s_->synth(TK_INT, n, (uint64_t)size, is_signed, false);
}

Value Evaluator::make_int(const Type* t, uint64_t x)
{
    Value v;
    v.type = t;
    v.where = VW_CONSTANT;
    int n = (int)type_size(t);
    if (n < 1 || n > 8)
        n = 8;
    /* The target's byte order: as value.cpp reads it. */
    int i;
    if (arch()->big_endian)
        for (i = n - 1; i >= 0; i--, x >>= 8)
            v.bytes[i] = (unsigned char)x;
    else
        for (i = 0; i < n; i++, x >>= 8)
            v.bytes[i] = (unsigned char)x;
    return v;
}

Value Evaluator::make_double(double d)
{
    Value v;
    v.type = double_type();
    v.where = VW_CONSTANT;
    unsigned char h[8];
    memcpy(h, &d, 8);
    unsigned short one = 1;
    bool host_big = *(unsigned char*)&one == 0;
    int i;
    for (i = 0; i < 8; i++)
        v.bytes[i] = host_big == arch()->big_endian ? h[i] : h[7 - i];
    return v;
}

/* Arrays become pointers to their first element, functions pointers to
 * them, as in C. */
Value Evaluator::decay(const Value& v)
{
    const Type* s = strip(v.type);
    if (s && s->kind == TK_ARRAY) {
        if (v.where != VW_MEMORY)
            return fail("the array is not in memory");
        Value p = make_int(s_->pointer_to(s->target), v.addr);
        p.name = v.name;
        p.expr = v.expr;
        return p;
    }
    if (s && s->kind == TK_FUNCTION && v.where == VW_MEMORY)
        return make_int(s_->pointer_to(v.type), v.addr);
    if (s && s->kind == TK_REFERENCE)
        return s_->deref(v);
    return v;
}

namespace {

/* A member by name, also inside anonymous structs and unions; base: the
 * bytes before the struct it is in. */
bool find_member(const Type* st, const char* name, Member* out, uint64_t* base, int depth)
{
    if (!st || depth > 8)
        return false;
    int i;
    for (i = 0; i < st->nmembers; i++) {
        const Member& m = st->members[i];
        if (m.name && !strcmp(m.name, name)) {
            *out = m;
            return true;
        }
    }
    for (i = 0; i < st->nmembers; i++) {
        const Member& m = st->members[i];
        if (m.name)
            continue;
        uint64_t b = *base + m.offset;
        if (find_member(strip(m.type), name, out, &b, depth + 1)) {
            *base = b;
            return true;
        }
    }
    return false;
}

bool is_aggregate(const Type* s)
{
    return s && (s->kind == TK_STRUCT || s->kind == TK_UNION || s->kind == TK_CLASS);
}

bool is_integral(const Type* s)
{
    return s && (s->kind == TK_INT || s->kind == TK_BOOL || s->kind == TK_ENUM);
}

} /* namespace */

Value Evaluator::member(const Value& v, const char* name)
{
    Value base = v;
    const Type* s = strip(base.type);
    if (s && s->kind == TK_REFERENCE) {
        base = s_->deref(base);
        s = strip(base.type);
    }
    if (!base.ok())
        return base;
    if (!is_aggregate(s))
        return fail("not a struct or union");
    if (s->decl && !s->nmembers)
        return fail("its type is incomplete here");
    Member m;
    uint64_t off = 0;
    if (!find_member(s, name, &m, &off, 0)) {
        Text t;
        t.addf("no member \"%s\"", name);
        return fail(t.c_str());
    }
    return s_->member_value(base, m, off);
}

Value Evaluator::cast(const Type* t, const Value& in)
{
    const Type* to = strip(t);
    Value v = decay(in);
    if (!v.ok())
        return v;
    const Type* from = strip(v.type);
    if (!to) {
        Value r;
        r.type = t;
        r.where = VW_CONSTANT;
        return r;
    }
    if (is_aggregate(to) || to->kind == TK_ARRAY) {
        if (in.where != VW_MEMORY)
            return fail("only something in memory can be cast to that");
        Value r = in;
        r.type = t;
        return r;
    }
    if (to->kind == TK_FLOAT) {
        double d = 0;
        if (!s_->as_double(v, &d))
            return fail("cannot convert that");
        if (to->size == 4) {
            float fl = (float)d;
            Value r = make_double(0);
            unsigned char h[4];
            memcpy(h, &fl, 4);
            unsigned short one = 1;
            bool host_big = *(unsigned char*)&one == 0;
            int i;
            for (i = 0; i < 4; i++)
                r.bytes[i] = host_big == arch()->big_endian ? h[i] : h[3 - i];
            r.type = t;
            return r;
        }
        Value r = make_double(d);
        if (to->size == 8)
            r.type = t;
        return r;
    }
    uint64_t x = 0;
    if (from && from->kind == TK_FLOAT) {
        double d = 0;
        if (!s_->as_double(v, &d))
            return fail("cannot convert that");
        x = to->is_signed ? (uint64_t)(int64_t)d : (uint64_t)d;
    } else if (!s_->scalar(v, &x)) {
        return fail("cannot convert that");
    }
    if (to->kind == TK_BOOL)
        x = x != 0;
    Value r = make_int(t, x);
    return r;
}

namespace {

/* Binary operators by precedence, loosest first. */
const char* const k_levels[][6] = {
    { "||", 0 },
    { "&&", 0 },
    { "|", 0 },
    { "^", 0 },
    { "&", 0 },
    { "==", "!=", 0 },
    { "<", ">", "<=", ">=", 0 },
    { "<<", ">>", 0 },
    { "+", "-", 0 },
    { "*", "/", "%", 0 },
};
const int k_nlevels = sizeof(k_levels) / sizeof(k_levels[0]);

} /* namespace */

Value Evaluator::arith(const char* op, const Value& a0, const Value& b0)
{
    Value a = decay(a0), b = decay(b0);
    if (!a.ok())
        return a;
    if (!b.ok())
        return b;
    const Type* ta = strip(a.type);
    const Type* tb = strip(b.type);
    Type* t_int = int_type(4, true);
    if (!strcmp(op, "&&") || !strcmp(op, "||")) {
        double x = 0, y = 0;
        if (!s_->as_double(a, &x) || !s_->as_double(b, &y))
            return fail("not a number");
        bool r = op[0] == '&' ? (x != 0 && y != 0) : (x != 0 || y != 0);
        return make_int(t_int, r ? 1 : 0);
    }
    bool pa = ta && ta->kind == TK_POINTER, pb = tb && tb->kind == TK_POINTER;
    bool cmp = !strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "<") ||
               !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=");
    if (pa || pb) {
        uint64_t x = 0, y = 0;
        if (!s_->scalar(a, &x) || !s_->scalar(b, &y))
            return fail("cannot read that");
        if (cmp) {
            bool r = !strcmp(op, "==") ? x == y : !strcmp(op, "!=") ? x != y
                     : !strcmp(op, "<") ? x < y : !strcmp(op, ">") ? x > y
                     : !strcmp(op, "<=") ? x <= y : x >= y;
            return make_int(t_int, r ? 1 : 0);
        }
        if (pa && pb && !strcmp(op, "-")) {
            uint64_t es = type_size(ta->target);
            if (!es)
                es = 1;
            return make_int(int_type(arch()->ptr_size, true),
                            (uint64_t)(((int64_t)x - (int64_t)y) / (int64_t)es));
        }
        if ((!strcmp(op, "+") || !strcmp(op, "-")) && (pa != pb) &&
            is_integral(pa ? tb : ta)) {
            const Type* pt = pa ? ta : tb;
            uint64_t es = type_size(pt->target);
            if (!es)
                es = 1;
            uint64_t p = pa ? x : y, n = pa ? y : x;
            if (!strcmp(op, "-") && pb)
                return fail("cannot subtract a pointer from a number");
            uint64_t r = op[0] == '+' ? p + n * es : p - n * es;
            if (arch()->ptr_size == 4)
                r &= 0xffffffffu;
            return make_int(pa ? a.type : b.type, r);
        }
        return fail("not with pointers");
    }
    bool fa = ta && ta->kind == TK_FLOAT, fb = tb && tb->kind == TK_FLOAT;
    if (fa || fb) {
        double x = 0, y = 0;
        if (!s_->as_double(a, &x) || !s_->as_double(b, &y))
            return fail("not a number");
        if (cmp) {
            bool r = !strcmp(op, "==") ? x == y : !strcmp(op, "!=") ? x != y
                     : !strcmp(op, "<") ? x < y : !strcmp(op, ">") ? x > y
                     : !strcmp(op, "<=") ? x <= y : x >= y;
            return make_int(t_int, r ? 1 : 0);
        }
        switch (op[0]) {
        case '+': return make_double(x + y);
        case '-': return make_double(x - y);
        case '*': return make_double(x * y);
        case '/': return make_double(x / y);
        default: return fail("not with floating point");
        }
    }
    if (!is_integral(ta) || !is_integral(tb))
        return fail("not a number");
    uint64_t x = 0, y = 0;
    if (!s_->scalar(a, &x) || !s_->scalar(b, &y))
        return fail("cannot read that");
    /* C's conversions, roughly: at least int; unsigned if the widest is. */
    int size = 4;
    if (ta->size > (uint64_t)size)
        size = (int)ta->size;
    if (tb->size > (uint64_t)size)
        size = (int)tb->size;
    bool uns = (ta->size == (uint64_t)size && !ta->is_signed && ta->kind != TK_BOOL &&
                ta->size >= 4) ||
               (tb->size == (uint64_t)size && !tb->is_signed && tb->kind != TK_BOOL &&
                tb->size >= 4);
    Type* rt = int_type(size, !uns);
    if (cmp) {
        bool r;
        if (uns)
            r = !strcmp(op, "==") ? x == y : !strcmp(op, "!=") ? x != y
                : !strcmp(op, "<") ? x < y : !strcmp(op, ">") ? x > y
                : !strcmp(op, "<=") ? x <= y : x >= y;
        else {
            int64_t sx = (int64_t)x, sy = (int64_t)y;
            r = !strcmp(op, "==") ? sx == sy : !strcmp(op, "!=") ? sx != sy
                : !strcmp(op, "<") ? sx < sy : !strcmp(op, ">") ? sx > sy
                : !strcmp(op, "<=") ? sx <= sy : sx >= sy;
        }
        return make_int(t_int, r ? 1 : 0);
    }
    uint64_t r = 0;
    if (!strcmp(op, "+"))
        r = x + y;
    else if (!strcmp(op, "-"))
        r = x - y;
    else if (!strcmp(op, "*"))
        r = x * y;
    else if (!strcmp(op, "/") || !strcmp(op, "%")) {
        if (!y)
            return fail("division by zero");
        if (uns)
            r = op[0] == '/' ? x / y : x % y;
        else
            r = op[0] == '/' ? (uint64_t)((int64_t)x / (int64_t)y)
                             : (uint64_t)((int64_t)x % (int64_t)y);
    } else if (!strcmp(op, "<<"))
        r = x << (y & 63);
    else if (!strcmp(op, ">>"))
        r = uns ? x >> (y & 63) : (uint64_t)((int64_t)x >> (y & 63));
    else if (!strcmp(op, "&"))
        r = x & y;
    else if (!strcmp(op, "|"))
        r = x | y;
    else if (!strcmp(op, "^"))
        r = x ^ y;
    else
        return fail("unknown operator");
    return make_int(rt, r);
}

Value Evaluator::cond()
{
    Value c = binary(0);
    if (!accept("?"))
        return c;
    Value a = cond();
    if (!accept(":"))
        return fail("expected :");
    Value b = cond();
    if (!c.ok())
        return c;
    double d = 0;
    if (!s_->as_double(decay(c), &d))
        return fail("not a number");
    return d != 0 ? a : b;
}

Value Evaluator::binary(int level)
{
    if (level >= k_nlevels)
        return unary();
    Value a = binary(level + 1);
    for (;;) {
        if (tok_ != T_OP)
            return a;
        const char* op = 0;
        int i;
        for (i = 0; k_levels[level][i]; i++)
            if (!strcmp(text_, k_levels[level][i]))
                op = k_levels[level][i];
        if (!op)
            return a;
        next();
        Value b = binary(level + 1);
        if (!error_.empty())
            return fail("");
        Text name;
        name.add(a.name.c_str());
        name.add(" ");
        name.add(op);
        name.add(" ");
        name.add(b.name.c_str());
        a = arith(op, a, b);
        a.name = name.c_str();
        a.expr = name.c_str();
    }
}

/* At a cast's "(": does a type name follow? */
bool Evaluator::type_ahead()
{
    if (tok_ != T_ID)
        return false;
    if (is_type_word(text_))
        return true;
    /* A typedef or struct name that is not also a variable. */
    Value v;
    if (s_->lookup(f_, text_, &v))
        return false;
    return s_->find_type(f_, text_) != 0;
}

/* A type name: C's base type words, struct/union/enum NAME, a typedef,
 * then const and *. */
const Type* Evaluator::parse_type()
{
    const Type* t = 0;
    bool have = false;
    int nlong = 0, nshort = 0, nchar = 0, nint = 0;
    bool is_uns = false, is_signed = false, is_float = false, is_double = false, is_void = false,
         is_bool = false;
    while (tok_ == T_ID) {
        if (!strcmp(text_, "const") || !strcmp(text_, "volatile")) {
            next();
            continue;
        }
        if (!strcmp(text_, "struct") || !strcmp(text_, "union") || !strcmp(text_, "enum") ||
            !strcmp(text_, "class")) {
            char key[16];
            strcpy(key, text_);
            next();
            if (tok_ != T_ID) {
                fail("expected a name");
                return 0;
            }
            Text full;
            full.add(key);
            full.add(" ");
            full.add(text_);
            t = s_->find_type(f_, full.c_str());
            if (!t) {
                Text m;
                m.addf("no type \"%s\"", full.c_str());
                fail(m.c_str());
                return 0;
            }
            have = true;
            next();
            continue;
        }
        if (!strcmp(text_, "unsigned"))
            is_uns = true;
        else if (!strcmp(text_, "signed"))
            is_signed = true;
        else if (!strcmp(text_, "long"))
            nlong++;
        else if (!strcmp(text_, "short"))
            nshort++;
        else if (!strcmp(text_, "char"))
            nchar++;
        else if (!strcmp(text_, "int"))
            nint++;
        else if (!strcmp(text_, "float"))
            is_float = true;
        else if (!strcmp(text_, "double"))
            is_double = true;
        else if (!strcmp(text_, "void"))
            is_void = true;
        else if (!strcmp(text_, "bool") || !strcmp(text_, "_Bool"))
            is_bool = true;
        else if (!have && !nlong && !nshort && !nchar && !nint && !is_uns && !is_signed &&
                 !is_float && !is_double && !is_void && !is_bool) {
            t = s_->find_type(f_, text_);
            if (!t)
                break;
            have = true;
        } else {
            break;
        }
        next();
    }
    if (!have) {
        int ptr = arch()->ptr_size;
        if (is_void)
            t = s_->synth(TK_VOID, "void", 0, false, false);
        else if (is_bool)
            t = s_->synth(TK_BOOL, "bool", 1, false, false);
        else if (is_float)
            t = s_->synth(TK_FLOAT, "float", 4, true, false);
        else if (is_double)
            t = nlong ? s_->synth(TK_FLOAT, "long double", 16, true, false) : double_type();
        else if (nchar)
            t = s_->synth(TK_INT, is_uns ? "unsigned char" : is_signed ? "signed char" : "char",
                          1, !is_uns && (is_signed || !arch()->big_endian), true);
        else if (nshort)
            t = int_type(2, !is_uns);
        else if (nlong >= 2)
            t = int_type(8, !is_uns);
        else if (nlong)
            t = s_->synth(TK_INT, is_uns ? "unsigned long" : "long", (uint64_t)ptr, !is_uns,
                          false);
        else if (nint || is_uns || is_signed)
            t = int_type(4, !is_uns);
        else {
            fail("expected a type");
            return 0;
        }
        /* void is no type to td: a pointer to it is. */
        if (is_void && !is_op("*"))
            return t;
    }
    while (tok_ == T_ID && (!strcmp(text_, "const") || !strcmp(text_, "volatile")))
        next();
    while (accept("*")) {
        t = s_->pointer_to(is_void && t && t->kind == TK_VOID && !t->target &&
                                   t->name && !strcmp(t->name, "void")
                               ? 0
                               : t);
        is_void = false;
        while (tok_ == T_ID && (!strcmp(text_, "const") || !strcmp(text_, "volatile")))
            next();
    }
    return t;
}

Value Evaluator::unary()
{
    if (tok_ == T_OP) {
        char op[4];
        strcpy(op, text_);
        if (!strcmp(op, "-") || !strcmp(op, "+") || !strcmp(op, "!") || !strcmp(op, "~") ||
            !strcmp(op, "*") || !strcmp(op, "&")) {
            next();
            Value v = unary();
            if (!v.ok())
                return v;
            Text name;
            name.add(op);
            name.add(v.name.c_str());
            Value r;
            if (!strcmp(op, "*")) {
                Value p = decay(v);
                const Type* s = strip(p.type);
                if (!s || s->kind != TK_POINTER)
                    return fail("not a pointer");
                r = s_->deref(p);
                if (!r.ok())
                    return fail(r.error.c_str());
            } else if (!strcmp(op, "&")) {
                if (v.where != VW_MEMORY || v.bit_size)
                    return fail("it has no address");
                r = make_int(s_->pointer_to(v.type), v.addr);
            } else if (!strcmp(op, "+")) {
                r = decay(v);
            } else if (!strcmp(op, "!")) {
                double d = 0;
                if (!s_->as_double(decay(v), &d))
                    return fail("not a number");
                r = make_int(int_type(4, true), d == 0 ? 1 : 0);
            } else {
                Value zero = make_int(int_type(4, true), 0);
                const Type* s = strip(v.type);
                if (!strcmp(op, "-"))
                    r = arith("-", zero, v);
                else if (s && s->kind == TK_FLOAT)
                    return fail("not with floating point");
                else
                    r = arith("^", v, make_int(int_type(s && s->size == 8 ? 8 : 4, true),
                                               ~(uint64_t)0));
            }
            r.name = name.c_str();
            r.expr = name.c_str();
            return r;
        }
        if (!strcmp(op, "(")) {
            /* A cast, or parentheses. */
            next();
            if (type_ahead()) {
                const Type* t = parse_type();
                if (!t)
                    return fail("expected a type");
                if (!accept(")"))
                    return fail("expected )");
                Value v = unary();
                if (!v.ok())
                    return v;
                Text name;
                name.add("(");
                name.add(td::type_name(t).c_str());
                name.add(")");
                name.add(v.name.c_str());
                Value r = cast(t, v);
                r.name = name.c_str();
                r.expr = name.c_str();
                return r;
            }
            Value v = cond();
            if (!accept(")"))
                return fail("expected )");
            Text name;
            name.add("(");
            name.add(v.name.c_str());
            name.add(")");
            v.name = name.c_str();
            v.expr = name.c_str();
            return postfix(v);
        }
    }
    if (tok_ == T_ID && !strcmp(text_, "sizeof")) {
        next();
        uint64_t size = 0;
        if (is_op("(")) {
            next();
            if (type_ahead()) {
                const Type* t = parse_type();
                if (!t)
                    return fail("expected a type");
                size = type_size(t);
            } else {
                Value v = cond();
                if (!error_.empty())
                    return fail("");
                size = type_size(v.type);
            }
            if (!accept(")"))
                return fail("expected )");
        } else {
            Value v = unary();
            if (!error_.empty())
                return fail("");
            size = type_size(v.type);
        }
        return make_int(int_type(arch()->ptr_size, false), size);
    }
    return postfix(primary());
}

Value Evaluator::postfix(Value v)
{
    for (;;) {
        if (!error_.empty())
            return fail("");
        if (accept("[")) {
            Value i = cond();
            if (!accept("]"))
                return fail("expected ]");
            if (!v.ok())
                return v;
            if (!i.ok())
                return i;
            Value p = decay(v);
            const Type* s = strip(p.type);
            if (!s || s->kind != TK_POINTER)
                return fail("not an array or pointer");
            uint64_t base = 0, n = 0;
            const Type* it = strip(i.type);
            if (!is_integral(it) || !s_->scalar(p, &base) || !s_->scalar(i, &n))
                return fail("not an index");
            Value e;
            e.type = s->target;
            e.where = VW_MEMORY;
            e.addr = base + n * type_size(s->target);
            if (arch()->ptr_size == 4)
                e.addr &= 0xffffffffu;
            e.hex = v.hex;
            Text name;
            name.add(v.name.c_str());
            name.add("[");
            name.add(i.name.c_str());
            name.add("]");
            e.name = name.c_str();
            e.expr = name.c_str();
            v = e;
            continue;
        }
        if (is_op(".") || is_op("->")) {
            bool arrow = is_op("->");
            next();
            if (tok_ != T_ID)
                return fail("expected a member name");
            char mname[256];
            strcpy(mname, text_);
            next();
            if (!v.ok())
                return v;
            Value base = v;
            if (arrow) {
                Value p = decay(v);
                const Type* s = strip(p.type);
                if (!s || s->kind != TK_POINTER)
                    return fail("not a pointer");
                base = s_->deref(p);
                if (!base.ok())
                    return fail(base.error.c_str());
            }
            Text name;
            name.add(v.name.c_str());
            name.add(arrow ? "->" : ".");
            name.add(mname);
            Value m = member(base, mname);
            if (!m.ok())
                return m;
            m.name = name.c_str();
            m.expr = name.c_str();
            v = m;
            continue;
        }
        return v;
    }
}

Value Evaluator::primary()
{
    Value v;
    switch (tok_) {
    case T_NUM: {
        uint64_t x = num_;
        bool big = unsigned_ ? x > 0xffffffffu : x > 0x7fffffff;
        v = make_int(int_type(big ? 8 : 4, !unsigned_), x);
        break;
    }
    case T_CHR:
        v = make_int(s_->synth(TK_INT, "char", 1, true, true), num_);
        break;
    case T_FLT:
        v = make_double(flt_);
        break;
    case T_REG: {
        const Arch* a = arch();
        int i, n = -1;
        for (i = 0; i < a->nregs; i++) {
            const char* rn = a->reg_name(i);
            if (rn && !strcmp(rn, text_))
                n = i;
        }
        Type* t = s_->synth(TK_INT, a->reg_size == 8 ? "unsigned long long" : "unsigned int",
                            (uint64_t)a->reg_size, false, false);
        if (!strcmp(text_, "pc")) {
            v = make_int(t, f_.pc);
        } else {
            if (!strcmp(text_, "sp"))
                n = a->sp_reg;
            if (n < 0) {
                Text m;
                m.addf("no register \"%s\"", text_);
                return fail(m.c_str());
            }
            if (!f_.regs.valid[n])
                return fail("that register was not saved in this frame");
            v = make_int(t, f_.regs.r[n]);
            v.where = VW_REGISTER;
            v.reg = n;
        }
        v.hex = true;
        break;
    }
    case T_ID: {
        if (!strcmp(text_, "true") || !strcmp(text_, "false")) {
            v = make_int(int_type(4, true), text_[0] == 't' ? 1 : 0);
            break;
        }
        if (!s_->lookup(f_, text_, &v)) {
            Text m;
            m.addf("no symbol \"%s\" here", text_);
            return fail(m.c_str());
        }
        if (!v.ok()) {
            Text m;
            m.addf("%s: %s", text_, v.error.c_str());
            error_ = m.c_str();
            next();
            return fail("");
        }
        break;
    }
    case T_END:
        return fail("expected an expression");
    default: {
        Text m;
        m.addf("unexpected \"%s\"", text_);
        return fail(m.c_str());
    }
    }
    if (tok_ == T_REG) {
        Text n;
        n.add("$");
        n.add(text_);
        v.name = n.c_str();
    } else if (tok_ == T_CHR) {
        char b[8];
        if (num_ >= 32 && num_ < 127)
            snprintf(b, sizeof(b), "'%c'", (char)num_);
        else
            snprintf(b, sizeof(b), "%d", (int)num_);
        v.name = b;
    } else {
        v.name = text_;
    }
    v.expr = v.name;
    next();
    return v;
}

Value Evaluator::run()
{
    Value v = cond();
    if (error_.empty() && tok_ != T_END) {
        Text m;
        m.addf("unexpected \"%s\"", text_);
        return fail(m.c_str());
    }
    if (!error_.empty())
        return fail("");
    return v;
}

Value Session::evaluate(const Frame& f, const char* text)
{
    /* Visual C++'s format suffixes: "x,x" in hex, "p,10" 10 elements. */
    Text body;
    body.add(text);
    const char* comma = 0;
    int depth = 0;
    const char* p;
    for (p = text; *p; p++) {
        if (*p == '(' || *p == '[')
            depth++;
        else if (*p == ')' || *p == ']')
            depth--;
        else if (*p == ',' && depth == 0)
            comma = p;
    }
    bool hex = false, dec = false;
    long count = 0;
    if (comma) {
        const char* s = comma + 1;
        while (*s == ' ')
            s++;
        if ((s[0] == 'x' || s[0] == 'X') && !s[1])
            hex = true;
        else if (s[0] == 'd' && !s[1])
            dec = true;
        else if (isdigit((unsigned char)s[0]))
            count = atol(s);
        else
            comma = 0;
        if (comma)
            body.truncate((size_t)(comma - text));
    }
    Value v;
    if (!alive_) {
        v.error = "the program is not running";
        v.name = text;
        v.expr = text;
        return v;
    }
    Evaluator e(this, f, body.c_str());
    v = e.run();
    if (v.ok() && count > 0) {
        /* p,10: what p points to, as an array. */
        const Type* s = strip(v.type);
        uint64_t a = 0;
        if (s && s->kind == TK_POINTER && scalar(v, &a) && strip(s->target)) {
            Value arr;
            arr.type = array_of(s->target, count);
            arr.where = VW_MEMORY;
            arr.addr = a;
            v = arr;
        } else if (s && s->kind == TK_ARRAY && v.where == VW_MEMORY) {
            v.type = array_of(s->target, count);
        }
    }
    if (hex)
        v.hex = true;
    if (dec)
        v.hex = false;
    v.name = text;
    v.expr = body.c_str();
    return v;
}

} /* namespace td */
