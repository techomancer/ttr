/*
 * td/module.h - one loaded object (the executable, later its shared
 * libraries): its ELF symbols, and through libdwarf its line table and
 * frame tables (td/DESIGN.md section 5). Addresses in and out are the
 * running program's: the module adds its load bias.
 */
#ifndef TD_MODULE_H
#define TD_MODULE_H

#include "elf.h"
#include "info.h"
#include "td.h"

#include "core/str.h"
#include "core/vector.h"

namespace td {

class Target;

/* One row of the line table. */
struct LineRow {
    Addr addr;
    int file;           /* index into the module's file names */
    int line;
    bool end;           /* end of a sequence: the address after it */
};

/* Where an address is in the source. */
struct Place {
    const char* file;   /* full name as the compiler recorded it, or 0 */
    int line;
    bool exact;         /* the address starts a line-table row */
};

class Module {
public:
    Module();
    ~Module();

    bool load(const char* path, ttr::Str* err);
    void set_bias(Addr b) { bias_ = b; }
    Addr bias() const { return bias_; }
    const ElfFile& elf() const { return elf_; }
    const char* path() const { return elf_.path(); }
    bool has_lines() const { return !rows_.empty(); }

    /* Symbols, in program addresses. */
    bool function_named(const char* name, Addr* start, Addr* end) const;
    const char* function_at(Addr pc, Addr* start, Addr* end) const;
    bool contains(Addr pc) const;

    /* Lines. */
    bool place_of(Addr pc, Place* p) const;
    /* The addresses where a line of a file starts (the file named by its
     * full name or its last components, "a/b.c" or "b.c"); the next line
     * with code if that one has none. Returns the line found, 0 if none. */
    int line_addresses(const char* file, int line, ttr::Vector<Addr>* out) const;
    /* After a function's prologue: the second line row in it. */
    Addr after_prologue(Addr start, Addr end) const;

    /* One frame up: from regs at pc, the caller's registers (with its pc
     * the return address). caller: in is itself a caller's frame, whose pc
     * is a return address (looked up one byte earlier). False at the
     * outermost frame or without frame information. */
    bool unwind(Target* t, const Regs& in, bool caller, Regs* out) const;
    /* The frame's canonical frame address (DW_OP_call_frame_cfa). */
    bool cfa(const Regs& in, bool caller, Addr* out) const;

    /* Types, functions and variables (td/info.h), read when first asked;
     * 0 without DWARF. */
    Info* info() const;

private:
    void read_lines();
    void read_frames();
    bool frame_rules(Addr look, void* table, int* ra_col) const;
    int intern_file(const char* name);

    ElfFile elf_;
    Addr bias_;
    void* dbg_;                     /* Dwarf_Debug */
    mutable Info* info_;
    ttr::Vector<LineRow> rows_;     /* sorted by address */
    ttr::Vector<char*> files_;
    void* cie_[2];                  /* Dwarf_Cie*: .eh_frame, .debug_frame */
    long ncie_[2];
    void* fde_[2];                  /* Dwarf_Fde* */
    long nfde_[2];

    Module(const Module&);
    Module& operator=(const Module&);
};

} /* namespace td */

#endif
