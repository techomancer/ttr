/*
 * td/info.h - what a module's DWARF says about the program beyond its
 * lines and frames (td/DESIGN.md section 5.3): types, functions with
 * their arguments and local variables, and global variables. Read from
 * libdwarf's DIEs when first asked for: an index of the top-level names
 * and of the functions' address ranges on the first question, a
 * function's variables when it is first shown, a type when something of
 * it is. Addresses here are link addresses; the Module adds its bias.
 */
#ifndef TD_INFO_H
#define TD_INFO_H

#include "td.h"

#include "core/str.h"
#include "core/vector.h"

namespace td {

enum TypeKind {
    TK_VOID,
    TK_INT,         /* integers and characters */
    TK_BOOL,
    TK_FLOAT,
    TK_POINTER,
    TK_REFERENCE,
    TK_STRUCT,
    TK_UNION,
    TK_CLASS,
    TK_ENUM,
    TK_ARRAY,
    TK_FUNCTION,
    TK_TYPEDEF,     /* also restrict, nameless */
    TK_CONST,
    TK_VOLATILE
};

struct Type;

struct Member {
    const char* name;       /* 0: an anonymous struct or union, or a base class */
    Type* type;
    uint64_t offset;        /* bytes from the start */
    int bit_size;           /* 0: not a bit field */
    uint64_t bit_offset;    /* bit fields: bits from the start of the struct, numbered
                             * as DWARF 4's data_bit_offset (from the low bit on a
                             * little-endian CPU, from the high bit on a big-endian one) */
};

struct Enumerator {
    const char* name;
    int64_t value;
};

struct Type {
    TypeKind kind;
    const char* name;       /* 0: anonymous */
    uint64_t size;          /* bytes; see type_size() for typedefs */
    bool is_signed;         /* TK_INT, TK_ENUM */
    bool is_char;           /* TK_INT: shown as a character too */
    Type* target;           /* pointed or referred to, element, aliased, qualified,
                             * returned; 0: void */
    long count;             /* TK_ARRAY: elements, -1 if not known */
    bool decl;              /* declared only ("struct s;") */
    Member* members;
    int nmembers;
    Enumerator* enums;
    int nenums;

    Type();
};

/* Past typedefs, const and volatile (0 stays 0: void). */
const Type* strip(const Type* t);
uint64_t type_size(const Type* t);
/* As C writes it: "struct pt *", "int [4]", "const char *". */
ttr::Str type_name(const Type* t);

/* One DWARF operation, decoded by libdwarf. */
struct Op {
    unsigned char atom;
    uint64_t a1, a2, a3;
    uint64_t offset;        /* in the expression, for branches */
};

/* Where something is over a range of link addresses (lo == hi == 0:
 * everywhere): the operations ops(first) to ops(first + count - 1). */
struct LocRange {
    Addr lo, hi;
    int first, count;
};

/* A location: ranges(first) to ranges(first + count - 1); none: it has
 * no location (optimized away). */
struct Location {
    int first, count;
    Location() : first(0), count(0) {}
};

struct Variable {
    const char* name;
    Type* type;
    bool param;
    Location loc;
    bool has_const;         /* DW_AT_const_value instead of a location */
    int64_t const_value;
    int block;              /* its block in the function, -1: the function's own */
};

/* A lexical block: { ... } with variables of its own. */
struct Block {
    Addr lo, hi;
    int parent;             /* -1: the function */
};

struct Function {
    const char* name;
    Addr lo, hi;
    uint64_t die;
    uint64_t cu;            /* its compilation unit's DIE */
    Location frame_base;
    ttr::Vector<Variable> vars;     /* in the order declared */
    ttr::Vector<Block> blocks;
    bool loaded;            /* vars and blocks read */
    Function() : name(0), lo(0), hi(0), die(0), cu(0), loaded(false) {}
};

class Info {
public:
    /* dbg: the module's Dwarf_Debug. */
    Info(void* dbg, const Arch* arch);
    ~Info();

    /* The function whose code has this address, its variables read; 0 if
     * the DWARF has none there. */
    const Function* function_at(Addr a);
    /* A global (or file static) variable by name; the one in compilation
     * unit cu first. 0 if none. */
    const Variable* global(const char* name, uint64_t cu);
    /* An enumerator by name (RED), its value and type. */
    bool enumerator(const char* name, int64_t* value, Type** type);
    /* A type by name: "int", "unsigned int", "struct pt", "pt". */
    Type* type_named(const char* name);

    const LocRange& range(int i) const { return ranges_[i]; }
    const Op& op(int i) const { return ops_[i]; }

private:
    struct Name {
        const char* name;
        uint64_t die;
        uint64_t cu;
        char kind;          /* 'v' variable, 't' type, 'e' enumerator */
    };
    struct FuncRange {
        Addr lo, hi;
        Function* f;
    };
    struct Slot {
        uint64_t die;
        Type* type;
    };

    void index();
    void index_children(void* parent, uint64_t cu, int depth);
    void index_function(void* die, uint64_t cu);
    const Name* find_name(const char* name, char kind, uint64_t cu) const;
    void read_function(Function* f);
    void read_scope(void* die, Function* f, int block);
    bool read_variable(void* die, Variable* v);
    Type* type_at(uint64_t die);
    Type* read_type(void* die, Type* t);
    Type* new_type();
    Type* die_type(void* die);
    Location read_location(void* die, int attr);
    bool die_ranges(void* die, ttr::Vector<LocRange>* out);
    void insert_type(uint64_t die, Type* t);

    void* dbg_;
    const Arch* arch_;
    bool indexed_;
    ttr::Vector<Name> names_;           /* sorted by name */
    ttr::Vector<FuncRange> funcs_;      /* sorted by lo */
    ttr::Vector<Function*> functions_;  /* all, to free */
    ttr::Vector<Variable*> globals_;    /* read so far */
    ttr::Vector<uint64_t> global_dies_;
    ttr::Vector<Type*> types_;          /* all, to free */
    Slot* slots_;                       /* die offset -> Type, open addressing */
    int nslots_, used_slots_;
    ttr::Vector<LocRange> ranges_;
    ttr::Vector<Op> ops_;

    Info(const Info&);
    Info& operator=(const Info&);
};

} /* namespace td */

#endif
