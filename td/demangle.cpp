/*
 * td/demangle.cpp - C++ names from mangled ones (demangle.h): the Itanium
 * C++ ABI's (https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling)
 * and MIPSpro's cfront scheme. Written as c++filt prints them:
 * "char const*", "std::vector<int, std::allocator<int> >".
 *
 * The Itanium parser builds types as small trees, so declarators come out
 * right ("void (*)(int)", "int (*) [3]"), keeps the substitution table and
 * the template arguments, and gives up (false) on what it does not know
 * (decltype, most expressions) rather than guess.
 */
#include "demangle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/vector.h"

namespace td {

namespace {

/* ------------------------------------------------------------------ */
/* Itanium                                                             */

enum NodeKind {
    NK_NAME, NK_QUAL, NK_PTR, NK_REF, NK_RREF, NK_FUNC, NK_ARRAY, NK_PTRMEM,
    NK_PACK,        /* a template parameter pack's arguments */
    NK_TPARAM,      /* T_: the template argument it is where it is printed */
    NK_EXPAND       /* Dp: inner once for each argument of the packs in it */
};

struct Node {
    NodeKind kind;
    const char* text;   /* NAME: the name; QUAL: " const"...; FUNC: "(params)" and
                         * qualifiers; ARRAY: the dimension; PTRMEM: the class */
    const char* base;   /* NAME: its last plain name (a constructor's) */
    Node* inner;        /* the type qualified, pointed to, returned, the
                         * element, the member's, the pattern expanded */
    Node** elems;       /* NK_PACK */
    int nelems;         /* NK_PACK; NK_TPARAM: which argument */
};

/* What a <name> was, for its encoding. */
struct NameInfo {
    bool templ;         /* ends in template arguments: a return type follows */
    bool ctor_dtor;     /* a constructor or destructor: none does */
    bool conv;          /* a conversion operator: neither */
    const char* cv;     /* a member function's " const" */
    const char* ref;    /* its " &" or " &&" */
    NameInfo() : templ(false), ctor_dtor(false), conv(false), cv(""), ref("") {}
};

struct Operator {
    const char code[3];
    const char* name;
    int arity;
};

const Operator k_ops[] = {
    { "nw", "new", 1 }, { "na", "new[]", 1 }, { "dl", "delete", 1 }, { "da", "delete[]", 1 },
    { "ps", "+", 1 }, { "ng", "-", 1 }, { "ad", "&", 1 }, { "de", "*", 1 }, { "co", "~", 1 },
    { "pl", "+", 2 }, { "mi", "-", 2 }, { "ml", "*", 2 }, { "dv", "/", 2 }, { "rm", "%", 2 },
    { "an", "&", 2 }, { "or", "|", 2 }, { "eo", "^", 2 }, { "aS", "=", 2 }, { "pL", "+=", 2 },
    { "mI", "-=", 2 }, { "mL", "*=", 2 }, { "dV", "/=", 2 }, { "rM", "%=", 2 }, { "aN", "&=", 2 },
    { "oR", "|=", 2 }, { "eO", "^=", 2 }, { "ls", "<<", 2 }, { "rs", ">>", 2 },
    { "lS", "<<=", 2 }, { "rS", ">>=", 2 }, { "eq", "==", 2 }, { "ne", "!=", 2 },
    { "lt", "<", 2 }, { "gt", ">", 2 }, { "le", "<=", 2 }, { "ge", ">=", 2 },
    { "ss", "<=>", 2 }, { "nt", "!", 1 }, { "aa", "&&", 2 }, { "oo", "||", 2 },
    { "pp", "++", 1 }, { "mm", "--", 1 }, { "cm", ",", 2 }, { "pm", "->*", 2 },
    { "pt", "->", 2 }, { "cl", "()", 2 }, { "ix", "[]", 2 }, { "qu", "?", 3 },
};
const int k_nops = (int)(sizeof(k_ops) / sizeof(k_ops[0]));

const char* builtin(char c)
{
    switch (c) {
    case 'v': return "void";
    case 'w': return "wchar_t";
    case 'b': return "bool";
    case 'c': return "char";
    case 'a': return "signed char";
    case 'h': return "unsigned char";
    case 's': return "short";
    case 't': return "unsigned short";
    case 'i': return "int";
    case 'j': return "unsigned int";
    case 'l': return "long";
    case 'm': return "unsigned long";
    case 'x': return "long long";
    case 'y': return "unsigned long long";
    case 'n': return "__int128";
    case 'o': return "unsigned __int128";
    case 'f': return "float";
    case 'd': return "double";
    case 'e': return "long double";
    case 'g': return "__float128";
    case 'z': return "...";
    default: return 0;
    }
}

class Itanium {
public:
    explicit Itanium(const char* s) : p_(s), ok_(true), last_source_(""), pack_index_(-1), in_lambda_(false), depth_(0) {}
    ~Itanium();
    bool run(Text* out);

private:
    const char* p_;
    bool ok_;
    ttr::Vector<Node*> nodes_;      /* all, to free */
    ttr::Vector<char*> strs_;
    ttr::Vector<Node*> subs_;       /* the substitution table */
    ttr::Vector<Node*> tparams_;    /* the function template's arguments */
    const char* last_source_;       /* the last plain name: a constructor's */
    int pack_index_;                /* printing an expansion: which argument */
    bool in_lambda_;                /* T_ in a generic lambda's signature: auto */
    int depth_;                     /* types within types: a limit for bad input */

    void fail() { ok_ = false; }
    bool eat(char c)
    {
        if (*p_ != c)
            return false;
        p_++;
        return true;
    }
    bool eat2(const char* s)
    {
        if (p_[0] != s[0] || p_[1] != s[1])
            return false;
        p_ += 2;
        return true;
    }
    const char* keep(const char* s);
    const char* keep(const Text& t) { return keep(t.c_str()); }
    Node* make(NodeKind k, const char* text, Node* inner);
    void push(Node* n);
    Node* name_node(const char* text);

    void print(const Node* n, Text* out);
    const Node* resolve(const Node* n) const;
    bool wraps(const Node* n) const;
    const Node* collapse(const Node* n, NodeKind* kind) const;
    int pack_size(const Node* n) const;
    void left(const Node* n, Text* out);
    void right(const Node* n, Text* out);

    long number();
    bool encoding(Text* out, bool top, bool with_ret = true);
    bool encoding_body(Text* out, bool top, bool with_ret);
    bool special_name(Text* out);
    bool call_offset();
    const char* name(NameInfo* ni, bool top);
    const char* nested_name(NameInfo* ni, bool top);
    const char* local_name(NameInfo* ni);
    const char* unqualified_name(NameInfo* ni);
    const char* source_name();
    const char* operator_name(NameInfo* ni);
    const char* lambda();
    bool discriminator();
    Node* substitution();
    Node* template_param();
    bool template_args(Text* out, bool top);
    Node* template_arg();
    bool expr_primary(Text* out);
    bool expression(Text* out);
    bool expression_body(Text* out);
    Node* type();
    Node* type_body();
    Node* function_type();
    bool params(Text* out, bool stop_at_dot);
};

Itanium::~Itanium()
{
    int i;
    for (i = 0; i < nodes_.size(); i++) {
        delete[] nodes_[i]->elems;
        delete nodes_[i];
    }
    for (i = 0; i < strs_.size(); i++)
        free(strs_[i]);
}

/* A name or type's text, kept until the end. 64 KB at most: a bad name
 * whose substitutions refer to each other could otherwise double it
 * again and again. */
const char* Itanium::keep(const char* s)
{
    size_t n = strlen(s);
    if (n > 65536) {
        fail();
        return "";
    }
    char* c = (char*)malloc(n + 1);
    if (!c) {
        fail();
        return "";
    }
    memcpy(c, s, n + 1);
    strs_.push_back(c);
    return c;
}

Node* Itanium::make(NodeKind k, const char* text, Node* inner)
{
    Node* n = new Node;
    n->kind = k;
    n->text = text ? text : "";
    n->base = "";
    n->inner = inner;
    n->elems = 0;
    n->nelems = 0;
    nodes_.push_back(n);
    return n;
}

/* A substitution candidate. */
void Itanium::push(Node* n)
{
    subs_.push_back(n);
#ifdef TD_DEMANGLE_DEBUG
    Text d;
    print(n, &d);
    fprintf(stderr, "S%d: %s\n", subs_.size() - 2, d.c_str());
#endif
}

Node* Itanium::name_node(const char* text)
{
    Node* n = make(NK_NAME, text, 0);
    n->base = last_source_;
    return n;
}

/* ---- printing: what goes left of a declarator's name, what right ---- */

/* A pack, inside an expansion: its argument for this round. */
const Node* Itanium::resolve(const Node* n) const
{
    int guard = 0;
    while (n && guard++ < 64) {
        if (n->kind == NK_TPARAM && !in_lambda_ && n->nelems < tparams_.size())
            n = tparams_[n->nelems];
        else if (n->kind == NK_PACK && pack_index_ >= 0 && pack_index_ < n->nelems)
            n = n->elems[pack_index_];
        else
            break;
    }
    return n;
}

bool Itanium::wraps(const Node* n) const
{
    n = resolve(n);
    return n && (n->kind == NK_FUNC || n->kind == NK_ARRAY);
}

/* A reference to a reference is one: & if either is (T&& with T = X&
 * is X&). */
const Node* Itanium::collapse(const Node* n, NodeKind* kind) const
{
    *kind = n->kind;
    const Node* in = resolve(n->inner);
    while (in && (in->kind == NK_REF || in->kind == NK_RREF)) {
        if (in->kind == NK_REF)
            *kind = NK_REF;
        in = resolve(in->inner);
    }
    return in;
}

/* How many arguments the packs in a pattern have, -1 if it has none. */
int Itanium::pack_size(const Node* n) const
{
    if (!n)
        return -1;
    if (n->kind == NK_TPARAM) {
        const Node* r = resolve(n);
        return r == n ? -1 : pack_size(r);
    }
    if (n->kind == NK_PACK)
        return n->nelems;
    if (n->kind == NK_EXPAND)
        return -1;
    return pack_size(n->inner);
}

void Itanium::print(const Node* n, Text* out)
{
    left(n, out);
    right(n, out);
}

void Itanium::left(const Node* n, Text* out)
{
    n = resolve(n);
    if (!n)
        return;
    NodeKind k;
    const Node* in;
    switch (n->kind) {
    case NK_PACK: {
        /* Not in an expansion: all its arguments. */
        int i;
        for (i = 0; i < n->nelems; i++) {
            if (i)
                out->add(", ");
            print(n->elems[i], out);
        }
        break;
    }
    case NK_EXPAND: {
        int count = pack_size(n->inner), i, saved = pack_index_;
        if (count < 0) {
            print(n->inner, out);
            break;
        }
        for (i = 0; i < count; i++) {
            if (i)
                out->add(", ");
            pack_index_ = i;
            print(n->inner, out);
        }
        pack_index_ = saved;
        break;
    }
    case NK_NAME:
        out->add(n->text);
        break;
    case NK_TPARAM:
        if (in_lambda_)
            out->addf("auto:%d", n->nelems + 1);
        else
            out->add(n->text);      /* not known: its position */
        break;
    case NK_QUAL:
        left(n->inner, out);
        out->add(n->text);
        break;
    case NK_PTR:
        left(n->inner, out);
        if (wraps(n->inner))
            out->add("(");
        out->add("*");
        break;
    case NK_REF:
    case NK_RREF:
        in = collapse(n, &k);
        left(in, out);
        if (wraps(in))
            out->add("(");
        out->add(k == NK_REF ? "&" : "&&");
        break;
    case NK_FUNC:
        print(n->inner, out);
        out->add(" ");
        break;
    case NK_ARRAY:
        left(n->inner, out);
        if (!resolve(n->inner) || resolve(n->inner)->kind != NK_ARRAY)
            out->add(" ");
        break;
    case NK_PTRMEM:
        left(n->inner, out);
        out->add(wraps(n->inner) ? "(" : " ");
        out->add(n->text);
        out->add("::*");
        break;
    }
}

void Itanium::right(const Node* n, Text* out)
{
    n = resolve(n);
    if (!n)
        return;
    NodeKind k;
    const Node* in;
    switch (n->kind) {
    case NK_NAME:
    case NK_TPARAM:
    case NK_PACK:
    case NK_EXPAND:
        break;
    case NK_QUAL:
        right(n->inner, out);
        break;
    case NK_REF:
    case NK_RREF:
        in = collapse(n, &k);
        if (wraps(in))
            out->add(resolve(in)->kind == NK_ARRAY ? ") " : ")");
        right(in, out);
        break;
    case NK_PTR:
    case NK_PTRMEM:
        if (wraps(n->inner))
            out->add(resolve(n->inner)->kind == NK_ARRAY ? ") " : ")");
        right(n->inner, out);
        break;
    case NK_FUNC:
        out->add(n->text);
        break;
    case NK_ARRAY:
        out->add("[");
        out->add(n->text);
        out->add("]");
        right(n->inner, out);
        break;
    }
}

/* ---- the grammar ---- */

/* [n] <decimal>: n is minus. */
long Itanium::number()
{
    bool neg = eat('n');
    if (*p_ < '0' || *p_ > '9') {
        fail();
        return 0;
    }
    long v = 0;
    while (*p_ >= '0' && *p_ <= '9')
        v = v * 10 + (*p_++ - '0');
    return neg ? -v : v;
}

bool Itanium::run(Text* out)
{
    if (!eat2("_Z"))
        return false;
    Text t;
    if (!encoding(&t, true) || !ok_)
        return false;
    /* GCC's clones: ".cold", ".isra.0", ".constprop.0", ".part.0". */
    while (*p_ == '.' && ((p_[1] >= 'a' && p_[1] <= 'z') || p_[1] == '_')) {
        const char* s = p_++;
        while ((*p_ >= 'a' && *p_ <= 'z') || *p_ == '_')
            p_++;
        while (*p_ == '.' && p_[1] >= '0' && p_[1] <= '9') {
            p_++;
            while (*p_ >= '0' && *p_ <= '9')
                p_++;
        }
        t.add(" [clone ");
        t.add(s, (size_t)(p_ - s));
        t.add("]");
    }
    if (*p_)
        return false;
    out->add(t.c_str());
    return true;
}

/* <encoding>: a function's name and parameters, a variable's name, or a
 * special name (vtables, thunks, guards). */
bool Itanium::encoding(Text* out, bool top, bool with_ret)
{
    if (depth_ > 200) {
        fail();
        return false;
    }
    depth_++;
    bool ok = encoding_body(out, top, with_ret);
    depth_--;
    return ok;
}

bool Itanium::encoding_body(Text* out, bool top, bool with_ret)
{
    if (*p_ == 'T' || (*p_ == 'G' && (p_[1] == 'V' || p_[1] == 'R' || p_[1] == 'T')))
        return special_name(out);
    NameInfo ni;
    const char* nm = name(&ni, top);
    if (!ok_)
        return false;
    if (!*p_ || *p_ == 'E' || *p_ == '.') {
        out->add(nm);       /* data */
        return true;
    }
    Node* ret = 0;
    if (ni.templ && !ni.ctor_dtor && !ni.conv) {
        ret = type();
        if (!ok_)
            return false;
    }
    Text ps;
    if (!params(&ps, true))
        return false;
    if (!with_ret)
        ret = 0;
    if (ret) {
        left(ret, out);
        out->add(" ");
    }
    out->add(nm);
    out->add(ps.c_str());
    out->add(ni.cv);
    out->add(ni.ref);
    if (ret)
        right(ret, out);
    return true;
}

/* A function's parameter types, "(int, char const*)"; "v" alone is
 * "()". Up to 'E', the end, or a clone's '.'. */
bool Itanium::params(Text* out, bool stop_at_dot)
{
    out->add("(");
    if (p_[0] == 'v' && (!p_[1] || p_[1] == 'E' || (stop_at_dot && p_[1] == '.'))) {
        p_++;
        out->add(")");
        return true;
    }
    bool first = true;
    while (*p_ && *p_ != 'E' && !(stop_at_dot && *p_ == '.')) {
        if ((*p_ == 'R' || *p_ == 'O') && p_[1] == 'E')
            break;      /* a function type's ref-qualifier */
        Node* t = type();
        if (!ok_)
            return false;
        Text one;
        print(t, &one);
        if (!one.length())
            continue;       /* an empty pack's expansion */
        if (!first)
            out->add(", ");
        out->add(one.c_str());
        first = false;
    }
    out->add(")");
    return true;
}

bool Itanium::call_offset()
{
    if (eat('h')) {
        number();
        return eat('_');
    }
    if (eat('v')) {
        number();
        if (!eat('_'))
            return false;
        number();
        return eat('_');
    }
    return false;
}

bool Itanium::special_name(Text* out)
{
    static const struct {
        const char code[3];
        const char* what;
    } k_type_specials[] = {
        { "TV", "vtable for " }, { "TT", "VTT for " }, { "TI", "typeinfo for " },
        { "TS", "typeinfo name for " },
    };
    int i;
    for (i = 0; i < 4; i++)
        if (eat2(k_type_specials[i].code)) {
            Node* t = type();
            if (!ok_)
                return false;
            out->add(k_type_specials[i].what);
            print(t, out);
            return true;
        }
    if (*p_ == 'T' && (p_[1] == 'h' || p_[1] == 'v')) {
        bool virt = p_[1] == 'v';
        p_++;
        if (!call_offset()) {
            fail();
            return false;
        }
        out->add(virt ? "virtual thunk to " : "non-virtual thunk to ");
        return encoding(out, false);
    }
    if (eat2("Tc")) {
        if (!call_offset() || !call_offset()) {
            fail();
            return false;
        }
        out->add("covariant return thunk to ");
        return encoding(out, false);
    }
    if (eat2("TH") || eat2("TW")) {
        bool init = p_[-1] == 'H';
        NameInfo ni;
        const char* nm = name(&ni, false);
        out->add(init ? "TLS init function for " : "TLS wrapper function for ");
        out->add(nm);
        return ok_;
    }
    if (p_[0] == 'G' && p_[1] == 'T' && (p_[2] == 't' || p_[2] == 'n')) {
        p_ += 3;
        out->add("transaction clone for ");
        return encoding(out, false);
    }
    if (eat2("GV")) {
        NameInfo ni;
        const char* nm = name(&ni, false);
        out->add("guard variable for ");
        out->add(nm);
        return ok_;
    }
    if (eat2("GR")) {
        NameInfo ni;
        const char* nm = name(&ni, false);
        while (*p_ && *p_ != '_')
            p_++;
        eat('_');
        out->add("reference temporary for ");
        out->add(nm);
        return ok_;
    }
    fail();
    return false;
}

const char* Itanium::name(NameInfo* ni, bool top)
{
    if (*p_ == 'N')
        return nested_name(ni, top);
    if (*p_ == 'Z')
        return local_name(ni);
    Text t;
    if (*p_ == 'S' && p_[1] != 't') {
        /* <substitution> <template-args> */
        Node* s = substitution();
        if (!ok_)
            return "";
        print(s, &t);
        if (*p_ == 'I') {
            if (!template_args(&t, top))
                return "";
            ni->templ = true;
        }
        return keep(t);
    }
    if (eat2("St"))
        t.add("std::");
    const char* u = unqualified_name(ni);
    if (!ok_)
        return "";
    t.add(u);
    if (*p_ == 'I') {
        push(name_node(keep(t)));    /* the template's name */
        if (!template_args(&t, top))
            return "";
        ni->templ = true;
    }
    return keep(t);
}

/* N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E:
 * every prefix is a substitution candidate, the whole name not. */
const char* Itanium::nested_name(NameInfo* ni, bool top)
{
    eat('N');
    Text cv;
    if (eat('r'))
        cv.add(" restrict");
    if (eat('V'))
        cv.add(" volatile");
    if (eat('K'))
        cv.add(" const");
    /* const first, as c++filt */
    if (strstr(cv.c_str(), "const") && strstr(cv.c_str(), "volatile")) {
        cv.clear();
        cv.add(" const volatile");
    }
    ni->cv = keep(cv);
    if (eat('R'))
        ni->ref = " &";
    else if (eat('O'))
        ni->ref = " &&";
    Text t;
    bool any = false, std_next = false;
    const char* base = last_source_;   /* the last component's plain name */
    while (ok_ && *p_ && *p_ != 'E') {
        bool templ = false, sub = false;
        last_source_ = base;
        if (!any && eat2("St")) {
            std_next = true;
            continue;
        } else if (*p_ == 'S') {
            Node* s = substitution();
            if (!ok_)
                return "";
            t.clear();
            print(s, &t);
            sub = true;     /* already in the table */
            base = last_source_;
        } else if (*p_ == 'T') {
            Node* s = template_param();
            if (!ok_)
                return "";
            t.clear();
            print(s, &t);
            push(name_node(keep(t)));
            any = true;
            continue;
        } else if (*p_ == 'I') {
            if (!any || !template_args(&t, top)) {
                fail();
                return "";
            }
            templ = true;
        } else if (*p_ == 'D' && (p_[1] == 't' || p_[1] == 'T')) {
            fail();     /* decltype */
            return "";
        } else {
            const char* u = unqualified_name(ni);
            if (!ok_)
                return "";
            if (any)
                t.add("::");
            else if (std_next)
                t.add("std::");
            t.add(u);
            base = last_source_;
        }
        last_source_ = base;
        any = true;
        ni->templ = templ;
        if (*p_ != 'E' && !sub)
            push(name_node(keep(t)));
    }
    if (!eat('E') || !any) {
        fail();
        return "";
    }
    return keep(t);
}

/* Z <encoding> E <name> [<discriminator>]: a function's local entity. */
const char* Itanium::local_name(NameInfo* ni)
{
    eat('Z');
    Text t;
    /* The function, without its return type; its template arguments are
     * what T_ means in it and after. */
    if (!encoding(&t, true, false) || !eat('E')) {
        fail();
        return "";
    }
    if (eat('s')) {
        t.add("::string literal");
        discriminator();
        return keep(t);
    }
    if (*p_ == 'd') {
        fail();     /* a default argument's */
        return "";
    }
    const char* n = name(ni, false);
    if (!ok_)
        return "";
    t.add("::");
    t.add(n);
    discriminator();
    return keep(t);
}

bool Itanium::discriminator()
{
    if (*p_ != '_')
        return true;
    p_++;
    if (eat('_')) {
        number();
        return eat('_');
    }
    if (*p_ >= '0' && *p_ <= '9') {
        p_++;
        return true;
    }
    fail();
    return false;
}

const char* Itanium::source_name()
{
    long n = number();
    if (!ok_ || n <= 0 || (long)strlen(p_) < n) {
        fail();
        return "";
    }
    Text t;
    if (n >= 10 && !strncmp(p_, "_GLOBAL__N", 10))
        t.add("(anonymous namespace)");
    else
        t.add(p_, (size_t)n);
    p_ += n;
    last_source_ = keep(t);
    return last_source_;
}

const char* Itanium::unqualified_name(NameInfo* ni)
{
    const char* r;
    Text t;
    if (*p_ >= '0' && *p_ <= '9') {
        r = source_name();
    } else if (*p_ == 'L') {
        p_++;       /* internal linkage (GCC's statics) */
        r = source_name();
        discriminator();
    } else if (*p_ == 'C' && (p_[1] == '1' || p_[1] == '2' || p_[1] == '3' || p_[1] == '4' ||
                              p_[1] == '5' || p_[1] == 'I')) {
        p_ += 2;
        if (p_[-1] == 'I') {        /* an inheriting constructor */
            p_++;
            type();
        }
        ni->ctor_dtor = true;
        r = last_source_;
    } else if (*p_ == 'D' && (p_[1] == '0' || p_[1] == '1' || p_[1] == '2' || p_[1] == '4' ||
                              p_[1] == '5')) {
        p_ += 2;
        ni->ctor_dtor = true;
        t.add("~");
        t.add(last_source_);
        r = keep(t);
    } else if (*p_ == 'U' && p_[1] == 't') {
        p_ += 2;
        long n = (*p_ >= '0' && *p_ <= '9') ? number() + 2 : 1;
        if (!eat('_')) {
            fail();
            return "";
        }
        t.addf("{unnamed type#%ld}", n);
        r = keep(t);
    } else if (*p_ == 'U' && p_[1] == 'l') {
        r = lambda();
    } else if (*p_ >= 'a' && *p_ <= 'z') {
        r = operator_name(ni);
    } else {
        fail();
        return "";
    }
    if (!ok_)
        return "";
    /* ABI tags: "[abi:cxx11]". */
    if (*p_ == 'B') {
        t.clear();
        t.add(r);
        while (eat('B')) {
            const char* saved = last_source_;
            const char* tag = source_name();
            last_source_ = saved;
            t.add("[abi:");
            t.add(tag);
            t.add("]");
        }
        r = keep(t);
    }
    return r;
}

/* Ul <lambda-sig> E [<number>] _: "{lambda(int)#1}". */
const char* Itanium::lambda()
{
    p_ += 2;
    Text ps;
    bool saved = in_lambda_;
    in_lambda_ = true;
    bool ok = params(&ps, false);
    in_lambda_ = saved;
    if (!ok || !eat('E')) {
        fail();
        return "";
    }
    long n = (*p_ >= '0' && *p_ <= '9') ? number() + 2 : 1;
    if (!eat('_')) {
        fail();
        return "";
    }
    Text t;
    t.add("{lambda");
    t.add(ps.c_str());
    t.addf("#%ld}", n);
    return keep(t);
}

const char* Itanium::operator_name(NameInfo* ni)
{
    Text t;
    if (eat2("cv")) {
        Node* ty = type();
        if (!ok_)
            return "";
        t.add("operator ");
        print(ty, &t);
        ni->conv = true;
        return keep(t);
    }
    if (eat2("li")) {
        const char* s = source_name();
        t.add("operator\"\" ");
        t.add(s);
        return keep(t);
    }
    int i;
    for (i = 0; i < k_nops; i++)
        if (p_[0] == k_ops[i].code[0] && p_[1] == k_ops[i].code[1]) {
            p_ += 2;
            t.add("operator");
            const char* n = k_ops[i].name;
            if (n[0] >= 'a' && n[0] <= 'z')
                t.add(" ");         /* operator new */
            t.add(n);
            return keep(t);
        }
    fail();
    return "";
}

/* S_, S <seq-id> _, and the std:: abbreviations. */
Node* Itanium::substitution()
{
    eat('S');
    /* gdb's short names; c++filt's longer ones with
     * TD_DEMANGLE_VERBOSE (to compare with it). */
    static const struct {
        char c;
        const char* name;
        const char* base;
    } k_abbrev[] = {
        { 'a', "std::allocator", "allocator" },
        { 'b', "std::basic_string", "basic_string" },
#ifdef TD_DEMANGLE_VERBOSE
        { 's', "std::basic_string<char, std::char_traits<char>, std::allocator<char> >",
          "basic_string" },
        { 'i', "std::basic_istream<char, std::char_traits<char> >", "basic_istream" },
        { 'o', "std::basic_ostream<char, std::char_traits<char> >", "basic_ostream" },
        { 'd', "std::basic_iostream<char, std::char_traits<char> >", "basic_iostream" },
#else
        { 's', "std::string", "basic_string" },
        { 'i', "std::istream", "basic_istream" },
        { 'o', "std::ostream", "basic_ostream" },
        { 'd', "std::iostream", "basic_iostream" },
#endif
    };
    int i;
    for (i = 0; i < 6; i++)
        if (eat(k_abbrev[i].c)) {
            last_source_ = k_abbrev[i].base;
            Node* n = make(NK_NAME, k_abbrev[i].name, 0);
            n->base = k_abbrev[i].base;
            return n;
        }
    long idx = 0;
    if (!eat('_')) {
        /* base 36, digits and capitals, then '_' */
        long v = 0;
        bool any = false;
        while ((*p_ >= '0' && *p_ <= '9') || (*p_ >= 'A' && *p_ <= 'Z')) {
            v = v * 36 + (*p_ <= '9' ? *p_ - '0' : *p_ - 'A' + 10);
            p_++;
            any = true;
        }
        if (!any || !eat('_')) {
            fail();
            return make(NK_NAME, "", 0);
        }
        idx = v + 1;
    }
    if (idx >= subs_.size()) {
        fail();
        return make(NK_NAME, "", 0);
    }
    Node* n = subs_[(int)idx];
    if (n->kind == NK_NAME && n->base[0])
        last_source_ = n->base;
    return n;
}

/* T_, T <number> _: the function template's arguments. */
Node* Itanium::template_param()
{
    eat('T');
    long idx = 0;
    if (!eat('_')) {
        idx = number() + 1;
        if (!eat('_')) {
            fail();
            return make(NK_NAME, "", 0);
        }
    }
    Text t;
    /* Which argument, looked up where it is printed (a substitution of
     * it may be printed in another template's context); in a generic
     * lambda's signature, its auto parameter. */
    t.addf("T%ld", idx);
    Node* n = make(NK_TPARAM, keep(t), 0);
    n->nelems = (int)idx;
    return n;
}

/* I <template-arg>+ E: "<int, char>". top: these are the function
 * template's, which T_ refers to. */
bool Itanium::template_args(Text* out, bool top)
{
    eat('I');
    ttr::Vector<Node*> args;
    Text t;
    t.add("<");
    bool first = true, space = false;
    while (ok_ && *p_ && *p_ != 'E') {
        Node* a = template_arg();
        if (!ok_)
            return false;
        args.push_back(a);
        Text one;
        print(a, &one);
        /* "> >", unless the last argument is an empty pack (as c++filt) */
        space = one.length() && one.c_str()[one.length() - 1] == '>';
        if (!one.length() && a->kind == NK_PACK)
            continue;
        if (!first)
            t.add(", ");
        t.add(one.c_str());
        first = false;
    }
    if (!eat('E')) {
        fail();
        return false;
    }
    if (space)
        t.add(" ");
    t.add(">");
    if (out->length() && out->c_str()[out->length() - 1] == '<')
        out->add(" ");      /* operator< <int> */
    out->add(t.c_str());
    if (top) {
        tparams_.clear();
        int i;
        for (i = 0; i < args.size(); i++)
            tparams_.push_back(args[i]);
    }
    return true;
}

Node* Itanium::template_arg()
{
    Text t;
    if (*p_ == 'L') {
        if (!expr_primary(&t))
            fail();
        return make(NK_NAME, keep(t), 0);
    }
    if (eat('X')) {
        if (!expression(&t) || !eat('E'))
            fail();
        return make(NK_NAME, keep(t), 0);
    }
    if (eat('J')) {
        /* a pack: its arguments */
        ttr::Vector<Node*> el;
        if (++depth_ > 200)
            fail();
        while (ok_ && *p_ && *p_ != 'E')
            el.push_back(template_arg());
        depth_--;
        if (!eat('E'))
            fail();
        Node* n = make(NK_PACK, "", 0);
        n->nelems = el.size();
        n->elems = new Node*[el.size() ? el.size() : 1];
        int i;
        for (i = 0; i < el.size(); i++)
            n->elems[i] = el[i];
        return n;
    }
    return type();
}

/* L <type> <value> E, L _Z <encoding> E: 42, true, 3u, (char)65. */
bool Itanium::expr_primary(Text* out)
{
    eat('L');
    if (eat2("_Z")) {
        if (!encoding(out, false))
            return false;
        return eat('E');
    }
    if (eat2("Dn")) {
        out->add("nullptr");
        eat('0');
        return eat('E');
    }
    char c = *p_;
    const char* bt = builtin(c);
    Node* ty = 0;
    if (bt)
        p_++;
    else
        ty = type();
    if (!ok_)
        return false;
    bool neg = eat('n');
    const char* v = p_;
    while (*p_ && *p_ != 'E')
        p_++;
    Text val;
    if (neg)
        val.add("-");
    val.add(v, (size_t)(p_ - v));
    if (!eat('E'))
        return false;
    if (bt && c == 'b' && !strcmp(val.c_str(), "0"))
        out->add("false");
    else if (bt && c == 'b' && !strcmp(val.c_str(), "1"))
        out->add("true");
    else if (bt && c == 'i')
        out->add(val.c_str());
    else if (bt && (c == 'j' || c == 'l' || c == 'm' || c == 'x' || c == 'y')) {
        out->add(val.c_str());
        out->add(c == 'j' ? "u" : c == 'l' ? "l" : c == 'm' ? "ul" : c == 'x' ? "ll" : "ull");
    } else {
        out->add("(");
        if (bt)
            out->add(bt);
        else
            print(ty, out);
        out->add(")");
        out->add(val.c_str());
    }
    return true;
}

/* A little of <expression>: template parameters, literals, function
 * parameters, and operators on them. */
bool Itanium::expression(Text* out)
{
    if (depth_ > 200)
        return false;
    depth_++;
    bool ok = expression_body(out);
    depth_--;
    return ok;
}

bool Itanium::expression_body(Text* out)
{
    if (*p_ == 'T') {
        print(template_param(), out);
        return ok_;
    }
    if (*p_ == 'L')
        return expr_primary(out);
    if (eat2("fp")) {
        long n = (*p_ >= '0' && *p_ <= '9') ? number() + 2 : 1;
        if (!eat('_'))
            return false;
        out->addf("{parm#%ld}", n);
        return true;
    }
    int i;
    for (i = 0; i < k_nops; i++)
        if (p_[0] == k_ops[i].code[0] && p_[1] == k_ops[i].code[1]) {
            p_ += 2;
            Text a, b;
            if (!expression(&a))
                return false;
            if (k_ops[i].arity == 1) {
                out->add(k_ops[i].name);
                out->add("(");
                out->add(a.c_str());
                out->add(")");
                return true;
            }
            if (k_ops[i].arity != 2 || !expression(&b))
                return false;
            out->add("(");
            out->add(a.c_str());
            out->add(")");
            out->add(k_ops[i].name);
            out->add("(");
            out->add(b.c_str());
            out->add(")");
            return true;
        }
    return false;
}

/* F [Y] <return type> <parameter types> [<ref-qualifier>] E */
Node* Itanium::function_type()
{
    eat('F');
    eat('Y');
    Node* ret = type();
    if (!ok_)
        return ret;
    Text ps;
    if (!params(&ps, false)) {
        fail();
        return ret;
    }
    if (eat('R'))
        ps.add(" &");
    else if (eat('O'))
        ps.add(" &&");
    if (!eat('E'))
        fail();
    return make(NK_FUNC, keep(ps), ret);
}

/* Types nest (and names in them): 200 deep is more than any program's. */
Node* Itanium::type()
{
    if (!ok_ || !*p_ || depth_ > 200) {
        fail();
        return make(NK_NAME, "", 0);
    }
    depth_++;
    Node* n = type_body();
    depth_--;
    return n;
}

Node* Itanium::type_body()
{
    const char* bt = builtin(*p_);
    if (bt) {
        p_++;
        return make(NK_NAME, bt, 0);
    }
    Node* n = 0;
    Text t;
    switch (*p_) {
    case 'r':
    case 'V':
    case 'K': {
        bool r = eat('r'), v = eat('V'), k = eat('K');
        if (k)
            t.add(" const");
        if (v)
            t.add(" volatile");
        if (r)
            t.add(" restrict");
        if (*p_ == 'F') {
            /* A member function's type, "(int) const": one entity. */
            Node* fn = function_type();
            Text f;
            f.add(fn->text);
            f.add(t.c_str());
            n = make(NK_FUNC, keep(f), fn->inner);
            break;
        }
        Node* in = type();
        /* A template parameter's type, as it is here. */
        const Node* rin = resolve(in);
        if (rin && rin != in && (rin->kind == NK_QUAL || rin->kind == NK_ARRAY))
            in = (Node*)rin;
        if (in->kind == NK_QUAL) {
            /* const on a const type (a template parameter's): once */
            Text q;
            q.add(in->text);
            if (k && !strstr(in->text, " const"))
                q.add(" const");
            if (v && !strstr(in->text, " volatile"))
                q.add(" volatile");
            if (r && !strstr(in->text, " restrict"))
                q.add(" restrict");
            n = make(NK_QUAL, keep(q), in->inner);
        } else if (in->kind == NK_ARRAY) {
            /* A const array is an array of const elements. */
            n = make(NK_ARRAY, in->text, make(NK_QUAL, keep(t), in->inner));
        } else {
            n = make(NK_QUAL, keep(t), in);
        }
        break;
    }
    case 'P':
        p_++;
        n = make(NK_PTR, "", type());
        break;
    case 'R':
        p_++;
        n = make(NK_REF, "", type());
        break;
    case 'O':
        p_++;
        n = make(NK_RREF, "", type());
        break;
    case 'F':
        n = function_type();
        break;
    case 'A': {
        p_++;
        if (*p_ >= '0' && *p_ <= '9')
            t.addf("%ld", number());
        else if (*p_ != '_') {
            fail();
            return make(NK_NAME, "", 0);
        }
        if (!eat('_')) {
            fail();
            return make(NK_NAME, "", 0);
        }
        Node* el = type();
        n = make(NK_ARRAY, keep(t), el);
        break;
    }
    case 'M': {
        p_++;
        Node* cls = type();
        Node* mem = type();
        print(cls, &t);
        n = make(NK_PTRMEM, keep(t), mem);
        break;
    }
    case 'T': {
        Node* tp = template_param();
        push(tp);
        if (*p_ != 'I')
            return tp;
        print(tp, &t);
        if (!template_args(&t, false))
            return tp;
        n = name_node(keep(t));
        break;
    }
    case 'S':
        if (p_[1] != 't') {
            Node* s = substitution();
            if (*p_ != 'I')
                return s;
            print(s, &t);
            if (!template_args(&t, false))
                return s;
            n = name_node(keep(t));
            break;
        }
        /* St: a class in std:: */
        {
            NameInfo ni;
            n = name_node(name(&ni, false));
        }
        break;
    case 'D':
        p_++;
        switch (*p_++) {
        case 'p':
            n = make(NK_EXPAND, "", type());    /* a pack's expansion */
            break;
        case 'n':
            return make(NK_NAME, "decltype(nullptr)", 0);
        case 'a':
            return make(NK_NAME, "auto", 0);
        case 'c':
            return make(NK_NAME, "decltype(auto)", 0);
        case 'i':
            return make(NK_NAME, "char32_t", 0);
        case 's':
            return make(NK_NAME, "char16_t", 0);
        case 'u':
            return make(NK_NAME, "char8_t", 0);
        case 'h':
            return make(NK_NAME, "half", 0);
        case 'F': {
            /* DF <bits> _ : _FloatN; DF <bits> x : _FloatNx; DF16b */
            long bits = number();
            if (eat('b'))
                return make(NK_NAME, "std::bfloat16_t", 0);
            t.addf("_Float%ld", bits);
            if (eat('x'))
                t.add("x");
            else if (!eat('_'))
                fail();
            return make(NK_NAME, keep(t), 0);
        }
        case 'f':
            return make(NK_NAME, "decimal32", 0);
        case 'd':
            return make(NK_NAME, "decimal64", 0);
        case 'e':
            return make(NK_NAME, "decimal128", 0);
        default:
            fail();     /* decltype, vectors */
            return make(NK_NAME, "", 0);
        }
        break;      /* Dp */
    case 'u':
        p_++;
        return make(NK_NAME, source_name(), 0);
    case 'N':
    case 'Z':
    default:
        if (*p_ == 'N' || *p_ == 'Z' || (*p_ >= '0' && *p_ <= '9')) {
            NameInfo ni;
            n = name_node(name(&ni, false));
        } else {
            fail();
            return make(NK_NAME, "", 0);
        }
        break;
    }
    if (!ok_)
        return make(NK_NAME, "", 0);
    push(n);
    return n;
}

} /* namespace */

bool demangle_itanium(const char* name, Text* out)
{
    Itanium d(name);
    return d.run(out);
}

namespace {

/* ------------------------------------------------------------------ */
/* cfront, as MIPSpro's EDG front end writes it                        */

bool eat_underscore(const char** p)
{
    if (**p != '_')
        return false;
    (*p)++;
    return true;
}

/* name__ [class] [C][S][V] G params: "get__1SCGv" is "S::get() const".
 * Classes and namespaces are <length><name>, or Q<n>_ and n of them;
 * a template's name has __pt__<length>_<arguments> in it. Functions are
 * G (cfront's F) and their parameter types; T<n> repeats parameter n,
 * N<count><n> count times. A function template's signature has Z<n>Z for
 * its parameters and _<return type> after it. Written as IRIX's own
 * demangler does: "const char*". */
class Cfront {
public:
    explicit Cfront(const char* s) : p_(s), ok_(true), depth_(0) {}
    bool run(Text* out);

private:
    const char* p_;
    bool ok_;
    ttr::Vector<char*> params_;     /* for T and N */
    ttr::Vector<char*> targs_;      /* a function template's arguments, for Z */
    int depth_;                     /* types within types: a limit for bad input */

    void fail() { ok_ = false; }
    long number();
    bool qualifier(Text* out, Text* last);
    bool class_name(Text* out, Text* plain);
    bool template_args(const char* s, size_t n, Text* out, ttr::Vector<char*>* keep);
    bool type(Text* left, Text* right);
    bool type_body(Text* left, Text* right);
    bool function_type(Text* out, bool have_ret, Text* ret);
    bool params(Text* out);
    void free_all();
};

const struct {
    const char* code;
    const char* name;
} k_cfront_ops[] = {
    { "nwa", "operator new[]" }, { "dla", "operator delete[]" },
    { "nw", "operator new" }, { "dl", "operator delete" }, { "as", "operator=" },
    { "eq", "operator==" }, { "ne", "operator!=" }, { "lt", "operator<" }, { "gt", "operator>" },
    { "le", "operator<=" }, { "ge", "operator>=" }, { "pl", "operator+" }, { "mi", "operator-" },
    { "ml", "operator*" }, { "dv", "operator/" }, { "md", "operator%" }, { "er", "operator^" },
    { "ad", "operator&" }, { "or", "operator|" }, { "co", "operator~" }, { "nt", "operator!" },
    { "aa", "operator&&" }, { "oo", "operator||" }, { "apl", "operator+=" },
    { "ami", "operator-=" }, { "amu", "operator*=" }, { "adv", "operator/=" },
    { "amd", "operator%=" }, { "aer", "operator^=" }, { "aad", "operator&=" },
    { "aor", "operator|=" }, { "als", "operator<<=" }, { "ars", "operator>>=" },
    { "ls", "operator<<" }, { "rs", "operator>>" }, { "pp", "operator++" }, { "mm", "operator--" },
    { "rf", "operator->" }, { "rm", "operator->*" }, { "cm", "operator," }, { "cl", "operator()" },
    { "vc", "operator[]" },
};

void Cfront::free_all()
{
    int i;
    for (i = 0; i < params_.size(); i++)
        free(params_[i]);
    for (i = 0; i < targs_.size(); i++)
        free(targs_[i]);
    params_.clear();
    targs_.clear();
}

char* dup_text(const Text& t)
{
    char* c = (char*)malloc(t.length() + 1);
    if (c)
        memcpy(c, t.c_str(), t.length() + 1);
    return c;
}

long Cfront::number()
{
    if (*p_ < '0' || *p_ > '9') {
        fail();
        return 0;
    }
    long v = 0;
    while (*p_ >= '0' && *p_ <= '9' && v < 100000)
        v = v * 10 + (*p_++ - '0');
    return v;
}

/* "<n>_<args>" of __pt__: types and literals (X <type> L <len> <digits>). */
bool Cfront::template_args(const char* s, size_t n, Text* out, ttr::Vector<char*>* keep)
{
    const char* save = p_;
    const char* end = s + n;
    p_ = s;
    if (!eat_underscore(&p_)) {
        p_ = save;
        return false;
    }
    out->add("<");
    bool first = true;
    while (ok_ && p_ < end) {
        Text one;
        if (*p_ == 'X') {
            /* a value: its type, then L <length> <digits> */
            p_++;
            Text l, r;
            if (!type(&l, &r) || *p_ != 'L') {
                fail();
                break;
            }
            p_++;
            bool neg = false;
            if (*p_ == 'n') {
                neg = true;
                p_++;
            }
            /* one digit of length, then that many of the value */
            long len = (*p_ >= '1' && *p_ <= '9') ? *p_++ - '0' : 0;
            if (len <= 0 || p_ + len > end) {
                fail();
                break;
            }
            size_t ln = l.length();
            if (ln >= 4 && !strcmp(l.c_str() + ln - 4, "bool") && len == 1 &&
                (*p_ == '0' || *p_ == '1')) {
                one.add(*p_ == '1' ? "true" : "false");
            } else {
                if (neg)
                    one.add("-");
                one.add(p_, (size_t)len);
            }
            p_ += len;
        } else {
            Text l, r;
            if (!type(&l, &r)) {
                fail();
                break;
            }
            one.add(l.c_str());
            one.add(r.c_str());
        }
        if (!first)
            out->add(", ");
        out->add(one.c_str());
        if (keep)
            keep->push_back(dup_text(one));
        first = false;
    }
    if (out->length() && out->c_str()[out->length() - 1] == '>')
        out->add(" ");
    out->add(">");
    bool good = ok_ && p_ == end;
    p_ = save;
    return good;
}

/* <length><name>, the name perhaps a template's: "3Box" "19Box__pt__9_1SXCiL12".
 * plain: the name without its arguments (a constructor's). */
bool Cfront::class_name(Text* out, Text* plain)
{
    long n = number();
    if (!ok_ || n <= 0 || (long)strlen(p_) < n) {
        fail();
        return false;
    }
    const char* s = p_;
    p_ += n;
    const char* pt = 0;
    const char* q;
    for (q = s; q + 6 <= s + n; q++)
        if (!strncmp(q, "__pt__", 6)) {
            pt = q;
            break;
        }
    if (!pt) {
        out->add(s, (size_t)n);
        if (plain) {
            plain->clear();
            plain->add(s, (size_t)n);
        }
        return true;
    }
    out->add(s, (size_t)(pt - s));
    if (plain) {
        plain->clear();
        plain->add(s, (size_t)(pt - s));
    }
    /* __pt__ <len> _<args>: len counts from the '_' */
    const char* save = p_;
    p_ = pt + 6;
    long len = number();
    const char* a = p_;
    p_ = save;
    if (!ok_ || a + len != s + n)
        return false;
    return template_args(a, (size_t)len, out, 0);
}

/* A class or namespace: <length><name>, or Q<count>_ and count of them. */
bool Cfront::qualifier(Text* out, Text* last)
{
    if (*p_ == 'Q') {
        p_++;
        long count;
        if (*p_ == '_') {
            p_++;
            count = number();
            if (*p_ == '_')
                p_++;
        } else {
            if (*p_ < '1' || *p_ > '9') {
                fail();
                return false;
            }
            count = *p_++ - '0';
            if (*p_ == '_')
                p_++;
        }
        long i;
        for (i = 0; i < count && ok_; i++) {
            if (i)
                out->add("::");
            class_name(out, last);
        }
        return ok_;
    }
    return class_name(out, last);
}

/* A type, as what goes left of a declarator and what right ("int (*" and
 * ")(char)"). 200 deep at most, and 64 KB (repeats of repeats). */
bool Cfront::type(Text* left, Text* right)
{
    if (!ok_ || depth_ > 200 || left->length() + right->length() > 65536) {
        fail();
        return false;
    }
    depth_++;
    bool ok = type_body(left, right);
    depth_--;
    if (left->length() + right->length() > 65536)
        fail();
    return ok && ok_;
}

bool Cfront::type_body(Text* left, Text* right)
{
    Text quals;
    bool is_unsigned = false, is_signed = false;
    for (;;) {
        if (*p_ == 'C') {
            quals.add("const ");
            p_++;
        } else if (*p_ == 'V') {
            quals.add("volatile ");
            p_++;
        } else if (*p_ == 'U') {
            is_unsigned = true;
            p_++;
        } else if (*p_ == 'S') {
            is_signed = true;
            p_++;
        } else {
            break;
        }
    }
    const char* b = 0;
    switch (*p_) {
    case 'v': b = "void"; break;
    case 'c': b = "char"; break;
    case 's': b = "short"; break;
    case 'i': b = "int"; break;
    case 'l': b = "long"; break;
    case 'L': b = "long long"; break;
    case 'x': b = "long long"; break;
    case 'f': b = "float"; break;
    case 'd': b = "double"; break;
    case 'r': b = "long double"; break;
    case 'b': b = "bool"; break;
    case 'w': b = "wchar_t"; break;
    case 'e': b = "..."; break;
    default: break;
    }
    if (b) {
        p_++;
        left->add(quals.c_str());
        if (is_unsigned)
            left->add("unsigned ");
        else if (is_signed)
            left->add("signed ");
        left->add(b);
        return true;
    }
    if (is_unsigned || is_signed) {
        fail();
        return false;
    }
    if (*p_ == 'P' || *p_ == 'R') {
        const char* op = *p_ == 'P' ? "*" : "&";
        p_++;
        Text l, r;
        if (!type(&l, &r))
            return false;
        left->add(l.c_str());
        if (r.length()) {
            left->add("(");
            left->add(op);
            right->add(")");
            right->add(r.c_str());
        } else {
            left->add(op);
        }
        if (quals.length()) {
            left->add(" ");
            left->add(quals.c_str());
            left->truncate(left->length() - 1);
        }
        return true;
    }
    if (*p_ == 'A') {
        /* A <dim> _ <element> */
        p_++;
        long dim = number();
        if (!ok_ || *p_ != '_') {
            fail();
            return false;
        }
        p_++;
        Text l, r;
        if (!type(&l, &r))
            return false;
        left->add(quals.c_str());
        left->add(l.c_str());
        right->addf("[%ld]", dim);
        right->add(r.c_str());
        return true;
    }
    if (*p_ == 'F' || *p_ == 'G') {
        /* A function type: (params) then _ and what it returns. */
        Text ps, ret;
        if (!function_type(&ps, true, &ret))
            return false;
        left->add(ret.c_str());
        left->add(" ");
        right->add(ps.c_str());
        return true;
    }
    if (*p_ == 'M') {
        /* M <class> <member type>: "int S::*", "void (S::*)(int)" */
        p_++;
        Text cls, l, r;
        if (!qualifier(&cls, 0))
            return false;
        bool cnst = false;
        if (*p_ == 'C' && (p_[1] == 'F' || p_[1] == 'G')) {
            cnst = true;
            p_++;
        }
        if (!type(&l, &r))
            return false;
        left->add(l.c_str());
        if (r.length()) {
            left->add("(");
            left->add(cls.c_str());
            left->add("::*");
            right->add(")");
            right->add(r.c_str());
            if (cnst)
                right->add(" const");
        } else {
            left->add(" ");
            left->add(cls.c_str());
            left->add("::*");
        }
        return true;
    }
    if (*p_ == 'Z') {
        /* A function template's parameter: Z <n> Z */
        p_++;
        long n = number();
        if (!ok_ || *p_ != 'Z' || n < 1) {
            fail();
            return false;
        }
        p_++;
        left->add(quals.c_str());
        if (n <= targs_.size())
            left->add(targs_[(int)n - 1]);
        else
            left->addf("T%ld", n);
        return true;
    }
    if ((*p_ >= '0' && *p_ <= '9') || *p_ == 'Q') {
        left->add(quals.c_str());
        return qualifier(left, 0);
    }
    fail();
    return false;
}

/* G <params> [_ <return>]: "(int, char)" into out, the return type into
 * ret when there is one (a pointer to function, a function template). */
bool Cfront::function_type(Text* out, bool want_ret, Text* ret)
{
    p_++;   /* F or G */
    ttr::Vector<char*> saved;
    int i;
    for (i = 0; i < params_.size(); i++)
        saved.push_back(params_[i]);
    params_.clear();
    bool ok = params(out);
    for (i = 0; i < params_.size(); i++)
        free(params_[i]);
    params_.clear();
    for (i = 0; i < saved.size(); i++)
        params_.push_back(saved[i]);
    if (!ok)
        return false;
    if (*p_ == '_') {
        p_++;
        Text l, r;
        if (!type(&l, &r))
            return false;
        if (ret) {
            ret->add(l.c_str());
            ret->add(r.c_str());
        }
    } else if (want_ret && ret) {
        ret->add("void");
    }
    return true;
}

/* Parameter types until the end or '_': "(int, ...)"; v alone is "()". */
bool Cfront::params(Text* out)
{
    out->add("(");
    if (*p_ == 'v' && (!p_[1] || p_[1] == '_')) {
        p_++;
        out->add(")");
        return true;
    }
    bool first = true;
    while (ok_ && *p_ && *p_ != '_') {
        long count = 1, idx;
        Text one;
        if (*p_ == 'T') {
            /* one digit, or _ <number> _ past 9 */
            p_++;
            if (*p_ == '_') {
                p_++;
                idx = number();
                if (*p_ == '_')
                    p_++;
            } else {
                idx = (*p_ >= '1' && *p_ <= '9') ? *p_++ - '0' : 0;
            }
            if (!ok_ || idx < 1 || idx > params_.size())
                return false;
            one.add(params_[(int)idx - 1]);
        } else if (*p_ == 'N') {
            /* N <count> <index>, one digit each */
            p_++;
            if (*p_ < '1' || *p_ > '9' || p_[1] < '1' || p_[1] > '9')
                return false;
            count = *p_++ - '0';
            idx = *p_++ - '0';
            if (idx > params_.size())
                return false;
            one.add(params_[(int)idx - 1]);
        } else {
            Text l, r;
            if (!type(&l, &r))
                return false;
            one.add(l.c_str());
            one.add(r.c_str());
        }
        if (out->length() + one.length() * (size_t)count > 65536) {
            fail();
            return false;
        }
        long k;
        for (k = 0; k < count; k++) {
            if (!first)
                out->add(", ");
            out->add(one.c_str());
            params_.push_back(dup_text(one));
            first = false;
        }
    }
    out->add(")");
    return ok_;
}

bool Cfront::run(Text* out)
{
    const char* s = p_;
    size_t n = strlen(s);
    /* The function's name: an operator's code, or up to a "__" that the
     * rest follows. */
    const char* start = s + 1;
    Text fname;
    bool special = false;
    if (s[0] == '_' && s[1] == '_') {
        const char* c = s + 2;
        if (!strncmp(c, "ct__", 4) || !strncmp(c, "dt__", 4)) {
            fname.add(c[0] == 'c' ? "ctor" : "dtor");
            start = c + 2;
            special = true;
        } else if (!strncmp(c, "op", 2)) {
            /* a conversion: __op <type> __ */
            p_ = c + 2;
            Text l, r;
            if (type(&l, &r) && p_[0] == '_' && p_[1] == '_') {
                fname.add("operator ");
                fname.add(l.c_str());
                fname.add(r.c_str());
                start = p_;
                special = true;
            }
            ok_ = true;
        } else {
            int i;
            for (i = 0; i < (int)(sizeof(k_cfront_ops) / sizeof(k_cfront_ops[0])); i++) {
                size_t l = strlen(k_cfront_ops[i].code);
                if (!strncmp(c, k_cfront_ops[i].code, l) && c[l] == '_' && c[l + 1] == '_') {
                    fname.add(k_cfront_ops[i].name);
                    start = c + l;
                    special = true;
                    break;
                }
            }
        }
    }
    const char* sep;
    for (sep = strstr(start, "__"); sep; sep = strstr(sep + 1, "__")) {
        Text name, cls, last, targs, ps, ret;
        ok_ = true;
        free_all();
        if (special)
            name.add(fname.c_str());
        else
            name.add(s, (size_t)(sep - s));
        p_ = sep + 2;
        /* A function template's arguments: name__pt__<n>_<args>__ */
        if (!special && !strncmp(p_, "pt__", 4)) {
            p_ += 4;
            long len = number();
            if (!ok_ || (long)strlen(p_) < len)
                continue;
            ttr::Vector<char*> keep;
            if (!template_args(p_, (size_t)len, &targs, &keep)) {
                int k;
                for (k = 0; k < keep.size(); k++)
                    free(keep[k]);
                continue;
            }
            int k;
            for (k = 0; k < keep.size(); k++)
                targs_.push_back(keep[k]);
            p_ += len;
            if (p_[0] != '_' || p_[1] != '_')
                continue;
            p_ += 2;
        }
        if ((*p_ >= '0' && *p_ <= '9') || *p_ == 'Q') {
            if (!qualifier(&cls, &last))
                continue;
        }
        bool cnst = false, stat = false, vol = false;
        for (;;) {
            if (*p_ == 'C' && !cnst) {
                cnst = true;
                p_++;
            } else if (*p_ == 'S' && !stat) {
                stat = true;
                p_++;
            } else if (*p_ == 'V' && !vol) {
                vol = true;
                p_++;
            } else {
                break;
            }
        }
        if (*p_ != 'F' && *p_ != 'G')
            continue;
        if (!function_type(&ps, false, &ret) || !ok_ || *p_)
            continue;
        if (special && !strcmp(fname.c_str(), "ctor")) {
            if (cls.length() == 0)
                continue;
            name.clear();
            name.add(last.c_str());
        } else if (special && !strcmp(fname.c_str(), "dtor")) {
            if (cls.length() == 0)
                continue;
            name.clear();
            name.add("~");
            name.add(last.c_str());
        }
        if (ret.length()) {
            out->add(ret.c_str());
            out->add(" ");
        }
        if (cls.length()) {
            out->add(cls.c_str());
            out->add("::");
        }
        out->add(name.c_str());
        out->add(targs.c_str());
        out->add(ps.c_str());
        if (cnst)
            out->add(" const");
        if (vol)
            out->add(" volatile");
        free_all();
        return true;
    }
    (void)n;
    free_all();
    return false;
}

} /* namespace */

bool demangle_cfront(const char* name, Text* out)
{
    Cfront d(name);
    Text t;
    if (!d.run(&t))
        return false;
    out->add(t.c_str());
    return true;
}

bool demangle(const char* name, Text* out)
{
    if (name[0] == '_' && name[1] == 'Z')
        return demangle_itanium(name, out);
    return demangle_cfront(name, out);
}

} /* namespace td */
