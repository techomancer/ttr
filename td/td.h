/*
 * td/td.h - the debugger library's shared types (td/DESIGN.md section 2):
 * addresses, registers by DWARF number, and the CPUs td knows (Arch).
 */
#ifndef TD_TD_H
#define TD_TD_H

#include "core/port.h"   /* uint64_t: <inttypes.h> on IRIX */

#include <stddef.h>

namespace td {

typedef uint64_t Addr;

/* DWARF register numbers go up to here (MIPS: 0-31 integer, 32-63
 * floating point, 64 SGI's return address column). */
enum { MAX_REGS = 80 };

/* A thread's registers, by DWARF number, plus its pc (x86-64 numbers it
 * 16; MIPS has no DWARF number for it). */
struct Regs {
    uint64_t r[MAX_REGS];
    bool valid[MAX_REGS];
    Addr pc;

    Regs();
    void set(int n, uint64_t v);
    void clear();
};

enum ArchId { ARCH_X86_64, ARCH_MIPS32, ARCH_MIPS64 };

/* What td needs to know about a CPU (DESIGN.md section 6). */
struct Arch {
    ArchId id;
    const char* name;
    int ptr_size;               /* 8 or 4 */
    int reg_size;               /* as saved on the stack: n32 saves 8 */
    bool big_endian;
    int sp_reg;                 /* DWARF numbers */
    int fp_reg;                 /* -1: none to follow */
    int ra_column;              /* the usual return address column */
    unsigned char bp[4];        /* the breakpoint instruction */
    int bp_len;
    int pc_after_bp;            /* how far the pc is past it on the stop */
    int nregs;                  /* DWARF registers worth showing */

    const char* reg_name(int dwarf) const;
    /* Right at a function's entry, where it will return to. */
    bool entry_return_address(const Regs& r, class Target* t, Addr* ra) const;
};

/* Text being put together (messages, values, type names). */
class Text {
public:
    Text();
    ~Text();
    void add(const char* s);
    void add(const char* s, size_t n);
    void addf(const char* fmt, ...);
    void add_char(char c);
    /* Numbers without printf's long long (C++98). */
    void add_int(int64_t v);
    void add_uint(uint64_t v);
    /* "0x" and at least digits hex digits. */
    void add_hex(uint64_t v, int digits = 1);
    const char* c_str() const { return p_ ? p_ : ""; }
    size_t length() const { return n_; }
    void truncate(size_t n);
    void clear() { truncate(0); }

private:
    char* p_;
    size_t n_, cap_;

    Text(const Text&);
    Text& operator=(const Text&);
};

/* "0x" and hex digits into buf (at least 19 bytes); returns buf. */
const char* hex(uint64_t v, char* buf);
/* A number in C notation (0x hex, 0 octal, decimal); *end past it. */
uint64_t parse_number(const char* s, const char** end);

const Arch* arch_x86_64();
const Arch* arch_mips32();      /* n32 and o32: 32-bit pointers */
const Arch* arch_mips64();

} /* namespace td */

#endif
