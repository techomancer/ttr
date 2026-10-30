/*
 * td/value.h - a value in the program being debugged (td/DESIGN.md
 * section 7): what a variable, a member, an element or an expression
 * is, where it is, and its type. The Session finds, formats and opens
 * them up (Session::locals, evaluate, format, children).
 */
#ifndef TD_VALUE_H
#define TD_VALUE_H

#include "info.h"
#include "td.h"

#include "core/str.h"

namespace td {

enum ValueWhere {
    VW_NONE,        /* no value: see error */
    VW_MEMORY,      /* at addr */
    VW_REGISTER,    /* in register reg; bytes holds what it has */
    VW_CONSTANT     /* computed: bytes */
};

struct Value {
    ttr::Str name;          /* as shown: "x", "[2]", "*p", the expression */
    ttr::Str expr;          /* a C expression for it, to watch: "a.next->x" */
    const Type* type;       /* 0: void */
    ValueWhere where;
    Addr addr;
    int reg;
    unsigned char bytes[16];    /* the target's byte order */
    int bit_size;           /* a bit field: bits at bit_offset from addr */
    uint64_t bit_offset;
    bool hex;               /* integers in hex (Watch's "x,x") */
    ttr::Str error;         /* why there is no value */

    Value();
    bool ok() const { return where != VW_NONE; }
};

} /* namespace td */

#endif
