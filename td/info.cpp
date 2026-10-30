#include "info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* libdwarf is C (third_party/libdwarf, td/DESIGN.md 5.4). */
#include "dwarf.h"
#include "libdwarf.h"

namespace td {

Type::Type()
    : kind(TK_VOID), name(0), size(0), is_signed(false), is_char(false), target(0), count(-1),
      decl(false), members(0), nmembers(0), enums(0), nenums(0)
{
}

const Type* strip(const Type* t)
{
    while (t && (t->kind == TK_TYPEDEF || t->kind == TK_CONST || t->kind == TK_VOLATILE))
        t = t->target;
    return t;
}

uint64_t type_size(const Type* t)
{
    const Type* s = strip(t);
    if (!s)
        return 0;
    if (s->kind == TK_ARRAY && !s->size && s->count > 0)
        return (uint64_t)s->count * type_size(s->target);
    return s->size;
}

namespace {

/* C's declarator order: the type around a name ("inner"), so pointers
 * to arrays come out "int (*)[4]". */
void name_into(const Type* t, Text* out, const char* inner)
{
    if (t) {
        switch (t->kind) {
        case TK_POINTER:
        case TK_REFERENCE: {
            Text in;
            bool wrap = t->target && (t->target->kind == TK_ARRAY ||
                                      t->target->kind == TK_FUNCTION);
            if (wrap)
                in.add("(");
            in.add(t->kind == TK_POINTER ? "*" : "&");
            in.add(inner);
            if (wrap)
                in.add(")");
            name_into(t->target, out, in.c_str());
            return;
        }
        case TK_ARRAY: {
            Text in;
            in.add(inner);
            in.add("[");
            if (t->count >= 0)
                in.add_int(t->count);
            in.add("]");
            name_into(t->target, out, in.c_str());
            return;
        }
        case TK_FUNCTION: {
            Text in;
            in.add(inner);
            in.add("()");
            name_into(t->target, out, in.c_str());
            return;
        }
        case TK_CONST:
        case TK_VOLATILE: {
            const char* q = t->kind == TK_CONST ? "const" : "volatile";
            if (t->target && t->target->kind == TK_POINTER) {
                /* char *const */
                Text in;
                in.add(q);
                if (*inner) {
                    in.add(" ");
                    in.add(inner);
                }
                name_into(t->target, out, in.c_str());
            } else {
                out->add(q);
                out->add(" ");
                name_into(t->target, out, inner);
            }
            return;
        }
        case TK_TYPEDEF:
            if (!t->name) {
                name_into(t->target, out, inner);
                return;
            }
            break;
        case TK_STRUCT:
            out->add("struct ");
            break;
        case TK_UNION:
            out->add("union ");
            break;
        case TK_CLASS:
            out->add("class ");
            break;
        case TK_ENUM:
            out->add("enum ");
            break;
        default:
            break;
        }
    }
    out->add(!t ? "void" : t->name ? t->name : "<anonymous>");
    if (*inner) {
        /* "char *const p" but "int *" and "int [4]". */
        out->add(" ");
        out->add(inner);
    }
}

} /* namespace */

ttr::Str type_name(const Type* t)
{
    Text out;
    name_into(t, &out, "");
    return ttr::Str(out.c_str());
}

/* ---- libdwarf, briefly ---- */

namespace {

Dwarf_Debug D(void* p)
{
    return (Dwarf_Debug)p;
}

void drop(Dwarf_Debug dbg, Dwarf_Error* e)
{
    if (*e)
        dwarf_dealloc_error(dbg, *e);
    *e = 0;
}

/* An attribute, let go at the end of the scope. */
struct Attr {
    Dwarf_Attribute a;
    Attr() : a(0) {}
    ~Attr()
    {
        if (a)
            dwarf_dealloc_attribute(a);
    }
};

bool get_attr(Dwarf_Debug dbg, Dwarf_Die die, int at, Attr* out)
{
    Dwarf_Error e = 0;
    int r = dwarf_attr(die, (Dwarf_Half)at, &out->a, &e);
    if (r == DW_DLV_ERROR)
        drop(dbg, &e);
    if (r != DW_DLV_OK)
        out->a = 0;
    return r == DW_DLV_OK;
}

bool get_udata(Dwarf_Debug dbg, Dwarf_Die die, int at, uint64_t* v)
{
    Attr a;
    if (!get_attr(dbg, die, at, &a))
        return false;
    Dwarf_Error e = 0;
    Dwarf_Unsigned u = 0;
    if (dwarf_formudata(a.a, &u, &e) == DW_DLV_OK) {
        *v = u;
        return true;
    }
    drop(dbg, &e);
    Dwarf_Signed s = 0;
    if (dwarf_formsdata(a.a, &s, &e) == DW_DLV_OK) {
        *v = (uint64_t)s;
        return true;
    }
    drop(dbg, &e);
    return false;
}

bool get_sdata(Dwarf_Debug dbg, Dwarf_Die die, int at, int64_t* v)
{
    Attr a;
    if (!get_attr(dbg, die, at, &a))
        return false;
    Dwarf_Error e = 0;
    Dwarf_Signed s = 0;
    if (dwarf_formsdata(a.a, &s, &e) == DW_DLV_OK) {
        *v = s;
        return true;
    }
    drop(dbg, &e);
    Dwarf_Unsigned u = 0;
    if (dwarf_formudata(a.a, &u, &e) == DW_DLV_OK) {
        *v = (int64_t)u;
        return true;
    }
    drop(dbg, &e);
    return false;
}

bool get_flag(Dwarf_Debug dbg, Dwarf_Die die, int at)
{
    Attr a;
    if (!get_attr(dbg, die, at, &a))
        return false;
    Dwarf_Error e = 0;
    Dwarf_Bool b = 0;
    if (dwarf_formflag(a.a, &b, &e) != DW_DLV_OK) {
        drop(dbg, &e);
        return false;
    }
    return b != 0;
}

bool get_ref(Dwarf_Debug dbg, Dwarf_Die die, int at, uint64_t* off)
{
    Attr a;
    if (!get_attr(dbg, die, at, &a))
        return false;
    Dwarf_Error e = 0;
    Dwarf_Off o = 0;
    Dwarf_Bool info = 0;
    if (dwarf_global_formref_b(a.a, &o, &info, &e) != DW_DLV_OK) {
        drop(dbg, &e);
        return false;
    }
    *off = o;
    return true;
}

int die_tag(Dwarf_Debug dbg, Dwarf_Die die)
{
    Dwarf_Error e = 0;
    Dwarf_Half t = 0;
    if (dwarf_tag(die, &t, &e) != DW_DLV_OK) {
        drop(dbg, &e);
        return 0;
    }
    return t;
}

uint64_t die_offset(Dwarf_Debug dbg, Dwarf_Die die)
{
    Dwarf_Error e = 0;
    Dwarf_Off o = 0;
    if (dwarf_dieoffset(die, &o, &e) != DW_DLV_OK)
        drop(dbg, &e);
    return o;
}

Dwarf_Die die_at(Dwarf_Debug dbg, uint64_t off)
{
    Dwarf_Error e = 0;
    Dwarf_Die d = 0;
    if (dwarf_offdie_b(dbg, off, 1, &d, &e) != DW_DLV_OK) {
        drop(dbg, &e);
        return 0;
    }
    return d;
}

Dwarf_Die first_child(Dwarf_Debug dbg, Dwarf_Die die)
{
    Dwarf_Error e = 0;
    Dwarf_Die c = 0;
    int r = dwarf_child(die, &c, &e);
    if (r == DW_DLV_ERROR)
        drop(dbg, &e);
    return r == DW_DLV_OK ? c : 0;
}

/* The next sibling; die is let go. */
Dwarf_Die next_sibling(Dwarf_Debug dbg, Dwarf_Die die)
{
    Dwarf_Error e = 0;
    Dwarf_Die n = 0;
    int r = dwarf_siblingof_c(die, &n, &e);
    if (r == DW_DLV_ERROR)
        drop(dbg, &e);
    dwarf_dealloc_die(die);
    return r == DW_DLV_OK ? n : 0;
}

/* A DIE's name, also through what it completes: a C++ member defined
 * outside its class, an inlined function's out-of-line copy. */
const char* die_name(Dwarf_Debug dbg, Dwarf_Die die)
{
    Dwarf_Error e = 0;
    char* n = 0;
    int r = dwarf_diename(die, &n, &e);
    if (r == DW_DLV_OK)
        return n;
    if (r == DW_DLV_ERROR)
        drop(dbg, &e);
    int hops;
    Dwarf_Die cur = 0;
    for (hops = 0; hops < 3; hops++) {
        uint64_t off = 0;
        Dwarf_Die from = cur ? cur : die;
        if (!get_ref(dbg, from, DW_AT_specification, &off) &&
            !get_ref(dbg, from, DW_AT_abstract_origin, &off))
            break;
        Dwarf_Die next = die_at(dbg, off);
        if (cur)
            dwarf_dealloc_die(cur);
        cur = next;
        if (!cur)
            break;
        n = 0;
        r = dwarf_diename(cur, &n, &e);
        if (r == DW_DLV_OK) {
            dwarf_dealloc_die(cur);
            return n;
        }
        if (r == DW_DLV_ERROR)
            drop(dbg, &e);
    }
    if (cur)
        dwarf_dealloc_die(cur);
    return 0;
}

/* The same for DW_AT_type: the DIE offset of its type, 0 for void. */
uint64_t die_type_offset(Dwarf_Debug dbg, Dwarf_Die die)
{
    uint64_t off = 0;
    if (get_ref(dbg, die, DW_AT_type, &off))
        return off;
    uint64_t up = 0;
    if (!get_ref(dbg, die, DW_AT_abstract_origin, &up) &&
        !get_ref(dbg, die, DW_AT_specification, &up))
        return 0;
    Dwarf_Die o = die_at(dbg, up);
    if (!o)
        return 0;
    if (!get_ref(dbg, o, DW_AT_type, &off))
        off = 0;
    dwarf_dealloc_die(o);
    return off;
}

int compare_names(const void* pa, const void* pb)
{
    /* Info::Name is private: its first field is the name. */
    return strcmp(*(const char* const*)pa, *(const char* const*)pb);
}

} /* namespace */

/* ---- Info ---- */

Info::Info(void* dbg, const Arch* arch)
    : dbg_(dbg), arch_(arch), indexed_(false), slots_(0), nslots_(0), used_slots_(0)
{
}

Info::~Info()
{
    int i;
    for (i = 0; i < types_.size(); i++) {
        free(types_[i]->members);
        free(types_[i]->enums);
        delete types_[i];
    }
    for (i = 0; i < functions_.size(); i++)
        delete functions_[i];
    for (i = 0; i < globals_.size(); i++)
        delete globals_[i];
    for (i = 0; i < strings_.size(); i++)
        free(strings_[i]);
    free(slots_);
}

namespace {

/* Info::FuncRange is private: its first field is lo. */
int compare_funcs(const void* pa, const void* pb)
{
    Addr a = *(const Addr*)pa, b = *(const Addr*)pb;
    return a < b ? -1 : a > b ? 1 : 0;
}

/* Info::Decl's first field is its die. */
int compare_decls(const void* pa, const void* pb)
{
    uint64_t a = *(const uint64_t*)pa, b = *(const uint64_t*)pb;
    return a < b ? -1 : a > b ? 1 : 0;
}

} /* namespace */

void Info::index()
{
    indexed_ = true;
    Dwarf_Debug dbg = D(dbg_);
    if (!dbg)
        return;
    Dwarf_Error e = 0;
    for (;;) {
        Dwarf_Die cu = 0;
        Dwarf_Unsigned len = 0, typeoff = 0, next = 0;
        Dwarf_Half ver = 0, asize = 0, lsize = 0, esize = 0, cutype = 0;
        Dwarf_Off abbrev = 0;
        Dwarf_Sig8 sig;
        int res = dwarf_next_cu_header_e(dbg, 1, &cu, &len, &ver, &abbrev, &asize, &lsize,
                                         &esize, &sig, &typeoff, &next, &cutype, &e);
        if (res != DW_DLV_OK) {
            drop(dbg, &e);
            break;
        }
        index_children(cu, die_offset(dbg, cu), 0, "");
        dwarf_dealloc_die(cu);
    }
    if (!names_.empty())
        qsort(&names_[0], (size_t)names_.size(), sizeof(Name), compare_names);
    /* Member functions defined outside their class: their qualified
     * names from the declarations. */
    if (!decls_.empty()) {
        qsort(&decls_[0], (size_t)decls_.size(), sizeof(Decl), compare_decls);
        int i;
        for (i = 0; i < functions_.size(); i++) {
            Function* f = functions_[i];
            if (f->qualified || !f->spec)
                continue;
            int lo = 0, hi = decls_.size() - 1;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                if (decls_[mid].die == f->spec) {
                    f->qualified = decls_[mid].qualified;
                    break;
                }
                if (decls_[mid].die < f->spec)
                    lo = mid + 1;
                else
                    hi = mid - 1;
            }
        }
    }
    if (!funcs_.empty())
        qsort(&funcs_[0], (size_t)funcs_.size(), sizeof(FuncRange), compare_funcs);
}

/* prefix "name::", kept until the Info goes. */
const char* Info::keep(const char* prefix, const char* name)
{
    size_t a = strlen(prefix), b = strlen(name);
    char* s = (char*)malloc(a + b + 1);
    if (!s)
        return "";
    memcpy(s, prefix, a);
    memcpy(s + a, name, b + 1);
    strings_.push_back(s);
    return s;
}

/* The DIEs under parent; prefix: the namespaces and classes they are in,
 * "geo::Rect::" (C++). */
void Info::index_children(void* parent, uint64_t cu, int depth, const char* prefix)
{
    Dwarf_Debug dbg = D(dbg_);
    Dwarf_Die c = first_child(dbg, (Dwarf_Die)parent);
    for (; c; c = next_sibling(dbg, c)) {
        int tag = die_tag(dbg, c);
        Name n;
        n.name = 0;
        n.die = die_offset(dbg, c);
        n.cu = cu;
        n.kind = 0;
        switch (tag) {
        case DW_TAG_subprogram: {
            const char* q = 0;
            if (*prefix) {
                const char* nm = die_name(dbg, c);
                if (nm) {
                    q = keep(prefix, nm);
                    Decl d;
                    d.die = n.die;
                    d.qualified = q;
                    decls_.push_back(d);
                }
            }
            index_function(c, cu, q);
            break;
        }
        case DW_TAG_variable: {
            Attr a;
            if (get_attr(dbg, c, DW_AT_location, &a) || get_attr(dbg, c, DW_AT_const_value, &a)) {
                n.name = die_name(dbg, c);
                n.kind = 'v';
            }
            break;
        }
        case DW_TAG_enumeration_type: {
            if (get_flag(dbg, c, DW_AT_declaration))
                break;
            n.name = die_name(dbg, c);
            n.kind = 't';
            Dwarf_Die en = first_child(dbg, c);
            for (; en; en = next_sibling(dbg, en)) {
                if (die_tag(dbg, en) != DW_TAG_enumerator)
                    continue;
                Name m;
                m.name = die_name(dbg, en);
                m.die = n.die;      /* the enum: its values are read with it */
                m.cu = cu;
                m.kind = 'e';
                if (m.name)
                    names_.push_back(m);
            }
            break;
        }
        case DW_TAG_base_type:
        case DW_TAG_typedef:
            n.name = die_name(dbg, c);
            n.kind = 't';
            break;
        case DW_TAG_structure_type:
        case DW_TAG_union_type:
        case DW_TAG_class_type:
            if (get_flag(dbg, c, DW_AT_declaration))
                break;
            n.name = die_name(dbg, c);
            n.kind = 't';
            /* C++: its member functions, for their qualified names */
            if (n.name && depth < 8) {
                Text p;
                p.add(prefix);
                p.add(n.name);
                p.add("::");
                index_children(c, cu, depth + 1, keep(p.c_str(), ""));
            }
            break;
        case DW_TAG_namespace:
            if (depth < 8) {
                const char* nm = die_name(dbg, c);
                Text p;
                p.add(prefix);
                p.add(nm ? nm : "(anonymous namespace)");
                p.add("::");
                index_children(c, cu, depth + 1, keep(p.c_str(), ""));
            }
            break;
        default:
            break;
        }
        if (n.name && n.kind)
            names_.push_back(n);
    }
}

void Info::index_function(void* die, uint64_t cu, const char* qualified)
{
    Dwarf_Debug dbg = D(dbg_);
    ttr::Vector<LocRange> rs;
    if (!die_ranges(die, &rs) || rs.empty())
        return;
    Function* f = new Function;
    f->name = die_name(dbg, (Dwarf_Die)die);
    f->qualified = qualified;
    uint64_t spec = 0;
    if (get_ref(dbg, (Dwarf_Die)die, DW_AT_specification, &spec))
        f->spec = spec;
    f->die = die_offset(dbg, (Dwarf_Die)die);
    f->cu = cu;
    f->lo = rs[0].lo;
    f->hi = rs[0].hi;
    functions_.push_back(f);
    int i;
    for (i = 0; i < rs.size(); i++) {
        FuncRange r;
        r.lo = rs[i].lo;
        r.hi = rs[i].hi;
        r.f = f;
        funcs_.push_back(r);
        if (r.lo < f->lo)
            f->lo = r.lo;
        if (r.hi > f->hi)
            f->hi = r.hi;
    }
}

/* low_pc and high_pc, or a range list (DWARF 2 to 4 .debug_ranges,
 * DWARF 5 .debug_rnglists). */
bool Info::die_ranges(void* vdie, ttr::Vector<LocRange>* out)
{
    Dwarf_Debug dbg = D(dbg_);
    Dwarf_Die die = (Dwarf_Die)vdie;
    Dwarf_Error e = 0;
    Dwarf_Addr lo = 0;
    int r = dwarf_lowpc(die, &lo, &e);
    if (r == DW_DLV_OK) {
        Dwarf_Addr hi = 0;
        Dwarf_Half form = 0;
        enum Dwarf_Form_Class cls = DW_FORM_CLASS_UNKNOWN;
        if (dwarf_highpc_b(die, &hi, &form, &cls, &e) != DW_DLV_OK) {
            drop(dbg, &e);
            return false;
        }
        if (cls == DW_FORM_CLASS_CONSTANT)
            hi += lo;
        LocRange x;
        x.lo = lo;
        x.hi = hi;
        x.first = x.count = 0;
        if (hi > lo)
            out->push_back(x);
        return true;
    }
    drop(dbg, &e);
    Attr a;
    if (!get_attr(dbg, die, DW_AT_ranges, &a))
        return false;
    Dwarf_Half ver = 2, offsz = 4;
    dwarf_get_version_of_die(die, &ver, &offsz);
    if (ver >= 5) {
        Dwarf_Half form = 0;
        Dwarf_Unsigned val = 0;
        if (dwarf_whatform(a.a, &form, &e) != DW_DLV_OK) {
            drop(dbg, &e);
            return false;
        }
        if (form == DW_FORM_rnglistx) {
            if (dwarf_formudata(a.a, &val, &e) != DW_DLV_OK) {
                drop(dbg, &e);
                return false;
            }
        } else {
            Dwarf_Off o = 0;
            Dwarf_Bool info = 0;
            if (dwarf_global_formref_b(a.a, &o, &info, &e) != DW_DLV_OK) {
                drop(dbg, &e);
                return false;
            }
            val = o;
        }
        Dwarf_Rnglists_Head head = 0;
        Dwarf_Unsigned n = 0, goff = 0;
        if (dwarf_rnglists_get_rle_head(a.a, form, val, &head, &n, &goff, &e) != DW_DLV_OK) {
            drop(dbg, &e);
            return false;
        }
        Dwarf_Unsigned i;
        for (i = 0; i < n; i++) {
            unsigned len = 0, code = 0;
            Dwarf_Unsigned raw1 = 0, raw2 = 0, c1 = 0, c2 = 0;
            Dwarf_Bool unavailable = 0;
            if (dwarf_get_rnglists_entry_fields_a(head, i, &len, &code, &raw1, &raw2,
                                                  &unavailable, &c1, &c2, &e) != DW_DLV_OK) {
                drop(dbg, &e);
                break;
            }
            if (code == DW_RLE_end_of_list)
                break;
            if (code == DW_RLE_base_address || code == DW_RLE_base_addressx || unavailable ||
                c2 <= c1)
                continue;
            LocRange x;
            x.lo = c1;
            x.hi = c2;
            x.first = x.count = 0;
            out->push_back(x);
        }
        dwarf_dealloc_rnglists_head(head);
        return true;
    }
    Dwarf_Off o = 0;
    Dwarf_Bool info = 0;
    if (dwarf_global_formref_b(a.a, &o, &info, &e) != DW_DLV_OK) {
        Dwarf_Unsigned u = 0;
        drop(dbg, &e);
        if (dwarf_formudata(a.a, &u, &e) != DW_DLV_OK) {
            drop(dbg, &e);
            return false;
        }
        o = u;
    }
    Dwarf_Bool known = 0, present = 0;
    Dwarf_Unsigned base = 0, roff = 0;
    if (dwarf_get_ranges_baseaddress(dbg, die, &known, &base, &present, &roff, &e) != DW_DLV_OK)
        drop(dbg, &e);
    Dwarf_Ranges* buf = 0;
    Dwarf_Signed n = 0;
    Dwarf_Unsigned bytes = 0;
    Dwarf_Off real = 0;
    if (dwarf_get_ranges_b(dbg, o, die, &real, &buf, &n, &bytes, &e) != DW_DLV_OK) {
        drop(dbg, &e);
        return false;
    }
    Dwarf_Signed i;
    for (i = 0; i < n; i++) {
        if (buf[i].dwr_type == DW_RANGES_END)
            break;
        if (buf[i].dwr_type == DW_RANGES_ADDRESS_SELECTION) {
            base = buf[i].dwr_addr2;
            continue;
        }
        LocRange x;
        x.lo = base + buf[i].dwr_addr1;
        x.hi = base + buf[i].dwr_addr2;
        x.first = x.count = 0;
        if (x.hi > x.lo)
            out->push_back(x);
    }
    dwarf_dealloc_ranges(dbg, buf, n);
    return true;
}

const Info::Name* Info::find_name(const char* name, char kind, uint64_t cu) const
{
    int lo = 0, hi = names_.size() - 1, first = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(names_[mid].name, name);
        if (c < 0) {
            lo = mid + 1;
        } else {
            if (c == 0)
                first = mid;
            hi = mid - 1;
        }
    }
    if (first < 0)
        return 0;
    const Name* any = 0;
    int i;
    for (i = first; i < names_.size() && !strcmp(names_[i].name, name); i++) {
        if (names_[i].kind != kind)
            continue;
        if (names_[i].cu == cu)
            return &names_[i];
        if (!any)
            any = &names_[i];
    }
    return any;
}

bool Info::function_bounds(Addr a, const char** name, Addr* lo, Addr* hi)
{
    const FuncRange* r = range_at(a);
    if (!r || !(r->f->qualified || r->f->name))
        return false;
    *name = r->f->qualified ? r->f->qualified : r->f->name;
    *lo = r->f->lo;
    *hi = r->f->hi;
    return true;
}

const char* Info::qualified_at(Addr a)
{
    const FuncRange* r = range_at(a);
    return r ? r->f->qualified : 0;
}

/* The name as given, or (MIPSpro's DWARF has no namespaces) one that
 * ends it after a "::": "Rect::area" for "geo::Rect::area". */
bool Info::function_named(const char* qualified, Addr* lo, Addr* hi)
{
    if (!indexed_)
        index();
    size_t n = strlen(qualified);
    int i, pass;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < functions_.size(); i++) {
            const Function* f = functions_[i];
            if (!f->qualified)
                continue;
            size_t m = strlen(f->qualified);
            bool match = pass == 0 ? !strcmp(f->qualified, qualified)
                                   : m + 2 <= n && !strcmp(qualified + n - m, f->qualified) &&
                                         qualified[n - m - 1] == ':' &&
                                         qualified[n - m - 2] == ':';
            if (match) {
                *lo = f->lo;
                *hi = f->hi;
                return true;
            }
        }
    return false;
}

/* The innermost function range holding a. */
const Info::FuncRange* Info::range_at(Addr a)
{
    if (!indexed_)
        index();
    int lo = 0, hi = funcs_.size() - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (funcs_[mid].lo <= a) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    int i;
    for (i = best; i >= 0 && i > best - 8; i--)
        if (a >= funcs_[i].lo && a < funcs_[i].hi)
            return &funcs_[i];
    return 0;
}

const Function* Info::function_at(Addr a)
{
    if (!indexed_)
        index();
    int lo = 0, hi = funcs_.size() - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (funcs_[mid].lo <= a) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    /* Nested ranges (a function's cold part inside another's): look back
     * a little. */
    int i;
    for (i = best; i >= 0 && i > best - 8; i--)
        if (a >= funcs_[i].lo && a < funcs_[i].hi) {
            Function* f = funcs_[i].f;
            if (!f->loaded)
                read_function(f);
            return f;
        }
    return 0;
}

void Info::read_function(Function* f)
{
    f->loaded = true;
    Dwarf_Debug dbg = D(dbg_);
    Dwarf_Die die = die_at(dbg, f->die);
    if (!die)
        return;
    f->frame_base = read_location(die, DW_AT_frame_base);
    read_scope(die, f, -1);
    dwarf_dealloc_die(die);
}

void Info::read_scope(void* parent, Function* f, int block)
{
    Dwarf_Debug dbg = D(dbg_);
    Dwarf_Die c = first_child(dbg, (Dwarf_Die)parent);
    for (; c; c = next_sibling(dbg, c)) {
        int tag = die_tag(dbg, c);
        if (tag == DW_TAG_formal_parameter || tag == DW_TAG_variable) {
            Variable v;
            if (!read_variable(c, &v))
                continue;
            v.param = tag == DW_TAG_formal_parameter;
            v.block = block;
            f->vars.push_back(v);
        } else if (tag == DW_TAG_lexical_block) {
            ttr::Vector<LocRange> rs;
            Block b;
            b.lo = b.hi = 0;
            b.parent = block;
            if (die_ranges(c, &rs) && !rs.empty()) {
                b.lo = rs[0].lo;
                b.hi = rs[0].hi;
                int i;
                for (i = 1; i < rs.size(); i++) {
                    if (rs[i].lo < b.lo)
                        b.lo = rs[i].lo;
                    if (rs[i].hi > b.hi)
                        b.hi = rs[i].hi;
                }
            }
            f->blocks.push_back(b);
            read_scope(c, f, f->blocks.size() - 1);
        }
    }
}

bool Info::read_variable(void* vdie, Variable* v)
{
    Dwarf_Debug dbg = D(dbg_);
    Dwarf_Die die = (Dwarf_Die)vdie;
    v->name = die_name(dbg, die);
    if (!v->name)
        return false;
    v->type = die_type(die);
    v->param = false;
    v->block = -1;
    v->has_const = false;
    v->const_value = 0;
    v->loc = read_location(die, DW_AT_location);
    int64_t cv = 0;
    if (!v->loc.count && get_sdata(dbg, die, DW_AT_const_value, &cv)) {
        v->has_const = true;
        v->const_value = cv;
    }
    return true;
}

Location Info::read_location(void* vdie, int at)
{
    Location loc;
    Dwarf_Debug dbg = D(dbg_);
    Attr a;
    if (!get_attr(dbg, (Dwarf_Die)vdie, at, &a))
        return loc;
    Dwarf_Error e = 0;
    Dwarf_Loc_Head_c head = 0;
    Dwarf_Unsigned n = 0;
    if (dwarf_get_loclist_c(a.a, &head, &n, &e) != DW_DLV_OK) {
        drop(dbg, &e);
        return loc;
    }
    loc.first = ranges_.size();
    Dwarf_Unsigned i;
    for (i = 0; i < n; i++) {
        Dwarf_Small lle = 0, source = 0;
        Dwarf_Unsigned rawlo = 0, rawhi = 0, nops = 0, exoff = 0, descoff = 0;
        Dwarf_Bool unavailable = 0;
        Dwarf_Addr lo = 0, hi = 0;
        Dwarf_Locdesc_c desc = 0;
        if (dwarf_get_locdesc_entry_d(head, i, &lle, &rawlo, &rawhi, &unavailable, &lo, &hi,
                                      &nops, &desc, &source, &exoff, &descoff,
                                      &e) != DW_DLV_OK) {
            drop(dbg, &e);
            continue;
        }
        if (!nops)
            continue;
        LocRange r;
        if (source == DW_LKIND_expression) {
            r.lo = r.hi = 0;
        } else {
            if (unavailable || hi <= lo)
                continue;
            r.lo = lo;
            r.hi = hi;
        }
        r.first = ops_.size();
        r.count = 0;
        Dwarf_Unsigned k;
        for (k = 0; k < nops; k++) {
            Dwarf_Small atom = 0;
            Dwarf_Unsigned a1 = 0, a2 = 0, a3 = 0, off = 0;
            if (dwarf_get_location_op_value_c(desc, k, &atom, &a1, &a2, &a3, &off, &e) !=
                DW_DLV_OK) {
                drop(dbg, &e);
                break;
            }
            Op o;
            o.atom = atom;
            o.a1 = a1;
            o.a2 = a2;
            o.a3 = a3;
            o.offset = off;
            ops_.push_back(o);
            r.count++;
        }
        ranges_.push_back(r);
    }
    loc.count = ranges_.size() - loc.first;
    dwarf_dealloc_loc_head_c(head);
    return loc;
}

/* ---- types ---- */

void Info::insert_type(uint64_t die, Type* t)
{
    if ((used_slots_ + 1) * 2 > nslots_) {
        int n = nslots_ ? nslots_ * 2 : 256;
        Slot* s = (Slot*)calloc((size_t)n, sizeof(Slot));
        if (!s)
            return;
        int i;
        for (i = 0; i < nslots_; i++) {
            if (!slots_[i].type)
                continue;
            unsigned h = (unsigned)((slots_[i].die * 2654435761u) >> 4) & (unsigned)(n - 1);
            while (s[h].type)
                h = (h + 1) & (unsigned)(n - 1);
            s[h] = slots_[i];
        }
        free(slots_);
        slots_ = s;
        nslots_ = n;
    }
    unsigned h = (unsigned)((die * 2654435761u) >> 4) & (unsigned)(nslots_ - 1);
    while (slots_[h].type)
        h = (h + 1) & (unsigned)(nslots_ - 1);
    slots_[h].die = die;
    slots_[h].type = t;
    used_slots_++;
}

Type* Info::new_type()
{
    Type* t = new Type;
    types_.push_back(t);
    return t;
}

Type* Info::type_at(uint64_t off)
{
    if (!off)
        return 0;
    if (nslots_) {
        unsigned h = (unsigned)((off * 2654435761u) >> 4) & (unsigned)(nslots_ - 1);
        while (slots_[h].type) {
            if (slots_[h].die == off)
                return slots_[h].type;
            h = (h + 1) & (unsigned)(nslots_ - 1);
        }
    }
    Dwarf_Die die = die_at(D(dbg_), off);
    if (!die)
        return 0;
    /* In the table before it is read: a struct that points to itself. */
    Type* t = new_type();
    insert_type(off, t);
    read_type(die, t);
    dwarf_dealloc_die(die);
    return t;
}

Type* Info::die_type(void* die)
{
    return type_at(die_type_offset(D(dbg_), (Dwarf_Die)die));
}

Type* Info::read_type(void* vdie, Type* t)
{
    Dwarf_Debug dbg = D(dbg_);
    Dwarf_Die die = (Dwarf_Die)vdie;
    int tag = die_tag(dbg, die);
    t->name = die_name(dbg, die);
    uint64_t size = 0;
    if (get_udata(dbg, die, DW_AT_byte_size, &size))
        t->size = size;
    switch (tag) {
    case DW_TAG_base_type: {
        uint64_t enc = 0;
        get_udata(dbg, die, DW_AT_encoding, &enc);
        t->kind = TK_INT;
        switch (enc) {
        case DW_ATE_signed:
            t->is_signed = true;
            break;
        case DW_ATE_signed_char:
            t->is_signed = true;
            t->is_char = true;
            break;
        case DW_ATE_unsigned_char:
        case DW_ATE_UTF:
            t->is_char = true;
            break;
        case DW_ATE_boolean:
            t->kind = TK_BOOL;
            break;
        case DW_ATE_float:
            t->kind = TK_FLOAT;
            break;
        case DW_ATE_unsigned:
            break;
        default:
            /* complex, decimal float: shown as bytes */
            t->kind = TK_VOID;
            break;
        }
        break;
    }
    case DW_TAG_unspecified_type:
        t->kind = TK_VOID;
        break;
    case DW_TAG_pointer_type:
    case DW_TAG_reference_type:
    case DW_TAG_rvalue_reference_type:
    case DW_TAG_ptr_to_member_type:
        t->kind = tag == DW_TAG_pointer_type || tag == DW_TAG_ptr_to_member_type ? TK_POINTER
                                                                                : TK_REFERENCE;
        if (!t->size)
            t->size = (uint64_t)arch_->ptr_size;
        t->target = die_type(die);
        break;
    case DW_TAG_typedef:
    case DW_TAG_restrict_type:
        t->kind = TK_TYPEDEF;
        if (tag == DW_TAG_restrict_type)
            t->name = 0;
        t->target = die_type(die);
        break;
    case DW_TAG_const_type:
        t->kind = TK_CONST;
        t->target = die_type(die);
        break;
    case DW_TAG_volatile_type:
        t->kind = TK_VOLATILE;
        t->target = die_type(die);
        break;
    case DW_TAG_subroutine_type:
        t->kind = TK_FUNCTION;
        t->target = die_type(die);
        break;
    case DW_TAG_enumeration_type: {
        t->kind = TK_ENUM;
        t->target = die_type(die);
        const Type* u = strip(t->target);
        t->is_signed = u ? u->is_signed : false;
        if (!t->size)
            t->size = u ? u->size : 4;
        ttr::Vector<Enumerator> es;
        Dwarf_Die c = first_child(dbg, die);
        for (; c; c = next_sibling(dbg, c)) {
            if (die_tag(dbg, c) != DW_TAG_enumerator)
                continue;
            Enumerator en;
            en.name = die_name(dbg, c);
            en.value = 0;
            get_sdata(dbg, c, DW_AT_const_value, &en.value);
            if (en.value < 0)
                t->is_signed = true;
            if (en.name)
                es.push_back(en);
        }
        if (!es.empty()) {
            t->enums = (Enumerator*)malloc(sizeof(Enumerator) * (size_t)es.size());
            if (t->enums) {
                memcpy(t->enums, &es[0], sizeof(Enumerator) * (size_t)es.size());
                t->nenums = es.size();
            }
        }
        break;
    }
    case DW_TAG_structure_type:
    case DW_TAG_union_type:
    case DW_TAG_class_type: {
        t->kind = tag == DW_TAG_union_type ? TK_UNION
                  : tag == DW_TAG_class_type ? TK_CLASS
                                             : TK_STRUCT;
        t->decl = get_flag(dbg, die, DW_AT_declaration);
        ttr::Vector<Member> ms;
        Dwarf_Die c = first_child(dbg, die);
        for (; c; c = next_sibling(dbg, c)) {
            int ct = die_tag(dbg, c);
            if (ct != DW_TAG_member && ct != DW_TAG_inheritance)
                continue;
            /* C++ static members (DWARF 4) are declarations here. */
            if (get_flag(dbg, c, DW_AT_declaration) || get_flag(dbg, c, DW_AT_external))
                continue;
            Member m;
            m.name = ct == DW_TAG_member ? die_name(dbg, c) : 0;
            m.type = die_type(c);
            m.offset = 0;
            m.bit_size = 0;
            m.bit_offset = 0;
            Attr a;
            if (get_attr(dbg, c, DW_AT_data_member_location, &a)) {
                Dwarf_Error e = 0;
                Dwarf_Half form = 0, ver = 2, offsz = 4;
                dwarf_get_version_of_die(c, &ver, &offsz);
                if (dwarf_whatform(a.a, &form, &e) != DW_DLV_OK)
                    drop(dbg, &e);
                if (dwarf_get_form_class(ver, DW_AT_data_member_location, offsz, form) ==
                    DW_FORM_CLASS_CONSTANT) {
                    get_udata(dbg, c, DW_AT_data_member_location, &m.offset);
                } else {
                    /* An expression: DWARF 2's DW_OP_plus_uconst N, MIPSpro's
                     * DW_OP_consts N. */
                    Location l = read_location(c, DW_AT_data_member_location);
                    if (l.count == 1 && ranges_[l.first].count == 1) {
                        const Op& o = ops_[ranges_[l.first].first];
                        if (o.atom == DW_OP_plus_uconst || o.atom == DW_OP_constu ||
                            o.atom == DW_OP_consts)
                            m.offset = o.a1;
                    }
                }
            }
            uint64_t bits = 0;
            if (get_udata(dbg, c, DW_AT_bit_size, &bits) && bits) {
                m.bit_size = (int)bits;
                uint64_t dbo = 0;
                if (get_udata(dbg, c, DW_AT_data_bit_offset, &dbo)) {
                    m.bit_offset = dbo;
                    m.offset = dbo / 8;
                } else {
                    /* DWARF 2 and 3: from the high bit of a storage unit of
                     * byte_size at the member's offset. */
                    uint64_t bo = 0, unit = 0;
                    get_udata(dbg, c, DW_AT_bit_offset, &bo);
                    if (!get_udata(dbg, c, DW_AT_byte_size, &unit))
                        unit = type_size(m.type);
                    if (arch_->big_endian)
                        m.bit_offset = m.offset * 8 + bo;
                    else
                        m.bit_offset = m.offset * 8 + unit * 8 - bo - bits;
                }
            }
            ms.push_back(m);
        }
        if (!ms.empty()) {
            t->members = (Member*)malloc(sizeof(Member) * (size_t)ms.size());
            if (t->members) {
                memcpy(t->members, &ms[0], sizeof(Member) * (size_t)ms.size());
                t->nmembers = ms.size();
            }
        }
        break;
    }
    case DW_TAG_array_type: {
        t->kind = TK_ARRAY;
        ttr::Vector<long> dims;
        Dwarf_Die c = first_child(dbg, die);
        for (; c; c = next_sibling(dbg, c)) {
            if (die_tag(dbg, c) != DW_TAG_subrange_type)
                continue;
            uint64_t count = 0, upper = 0;
            int64_t lower = 0;
            long n = -1;
            if (get_udata(dbg, c, DW_AT_count, &count)) {
                n = (long)count;
            } else if (get_udata(dbg, c, DW_AT_upper_bound, &upper)) {
                get_sdata(dbg, c, DW_AT_lower_bound, &lower);
                /* int a[0] and flexible members say upper -1. */
                n = (int64_t)upper < lower ? 0 : (long)((int64_t)upper - lower + 1);
                if ((uint64_t)upper == (uint64_t)-1 || ((uint64_t)upper == 0xffffffffu && arch_->ptr_size == 4))
                    n = 0;
            }
            dims.push_back(n);
        }
        if (dims.empty())
            dims.push_back(-1);
        Type* elem = die_type(die);
        /* int a[2][3]: an array of 2 arrays of 3. */
        Type* cur = t;
        int i;
        for (i = 0; i < dims.size(); i++) {
            cur->kind = TK_ARRAY;
            cur->count = dims[i];
            if (i + 1 < dims.size()) {
                Type* inner = new_type();
                cur->target = inner;
                cur = inner;
            } else {
                cur->target = elem;
            }
        }
        /* Sizes from the inside out. */
        uint64_t es = type_size(elem);
        Type* chain[16];
        int depth = 0;
        for (cur = t; cur && cur->kind == TK_ARRAY && depth < 16 && depth < dims.size();
             cur = cur->target)
            chain[depth++] = cur;
        for (i = depth - 1; i >= 0; i--) {
            uint64_t s = chain[i]->count > 0 ? es * (uint64_t)chain[i]->count : 0;
            if (i > 0 || !chain[i]->size)
                chain[i]->size = s;
            es = chain[i]->size;
        }
        break;
    }
    default:
        t->kind = TK_VOID;
        break;
    }
    return t;
}

/* ---- by name ---- */

const Variable* Info::global(const char* name, uint64_t cu)
{
    if (!indexed_)
        index();
    const Name* n = find_name(name, 'v', cu);
    if (!n)
        return 0;
    int i;
    for (i = 0; i < global_dies_.size(); i++)
        if (global_dies_[i] == n->die)
            return globals_[i];
    Dwarf_Die die = die_at(D(dbg_), n->die);
    if (!die)
        return 0;
    Variable* v = new Variable;
    if (!read_variable(die, v)) {
        delete v;
        dwarf_dealloc_die(die);
        return 0;
    }
    dwarf_dealloc_die(die);
    globals_.push_back(v);
    global_dies_.push_back(n->die);
    return v;
}

bool Info::enumerator(const char* name, int64_t* value, Type** type)
{
    if (!indexed_)
        index();
    const Name* n = find_name(name, 'e', 0);
    if (!n)
        return false;
    Type* t = type_at(n->die);
    if (!t)
        return false;
    int i;
    for (i = 0; i < t->nenums; i++)
        if (!strcmp(t->enums[i].name, name)) {
            *value = t->enums[i].value;
            *type = t;
            return true;
        }
    return false;
}

Type* Info::type_named(const char* name)
{
    if (!indexed_)
        index();
    static const char* const keys[] = { "struct ", "union ", "enum ", "class " };
    TypeKind want = TK_VOID;
    unsigned k;
    for (k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
        size_t l = strlen(keys[k]);
        if (!strncmp(name, keys[k], l)) {
            name += l;
            want = k == 0 ? TK_STRUCT : k == 1 ? TK_UNION : k == 2 ? TK_ENUM : TK_CLASS;
            break;
        }
    }
    int lo = 0, hi = names_.size() - 1, first = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(names_[mid].name, name);
        if (c < 0) {
            lo = mid + 1;
        } else {
            if (c == 0)
                first = mid;
            hi = mid - 1;
        }
    }
    if (first < 0)
        return 0;
    Type* any = 0;
    int i;
    for (i = first; i < names_.size() && !strcmp(names_[i].name, name); i++) {
        if (names_[i].kind != 't')
            continue;
        Type* t = type_at(names_[i].die);
        if (!t)
            continue;
        if (want == TK_VOID || t->kind == want ||
            (want == TK_STRUCT && t->kind == TK_CLASS))
            return t;
        if (!any)
            any = t;
    }
    return want == TK_VOID ? any : 0;
}

} /* namespace td */
