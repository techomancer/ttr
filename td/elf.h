/*
 * td/elf.h - the parts of an ELF file td reads itself: the header and
 * the symbol tables (libdwarf reads the DWARF sections). 32 and 64 bit,
 * either byte order: Linux x86-64 and IRIX n32/64 files alike.
 */
#ifndef TD_ELF_H
#define TD_ELF_H

#include "td.h"

#include "core/str.h"
#include "core/vector.h"

namespace td {

struct Symbol {
    const char* name;   /* in the file's string table */
    Addr addr;          /* link address */
    uint64_t size;
    bool func;
};

class ElfFile {
public:
    ElfFile();
    ~ElfFile();

    bool load(const char* path, ttr::Str* err);
    const char* path() const { return path_.c_str(); }

    bool is64() const { return is64_; }
    bool big_endian() const { return big_; }
    int machine() const { return machine_; }
    bool position_independent() const { return type_ == 3; }  /* ET_DYN */
    Addr entry() const { return entry_; }
    /* The CPU description for this file, or 0 if td does not know it. */
    const Arch* arch() const;

    /* Functions and data, sorted by address. */
    int symbol_count() const { return syms_.size(); }
    const Symbol& symbol(int i) const { return syms_[i]; }
    /* A function by name (link address), or 0. */
    const Symbol* find(const char* name) const;
    /* The function containing a link address, or 0. */
    const Symbol* function_at(Addr a) const;

private:
    uint64_t u16(const unsigned char* p) const;
    uint64_t u32(const unsigned char* p) const;
    uint64_t u64(const unsigned char* p) const;
    uint64_t word(const unsigned char* p) const { return is64_ ? u64(p) : u32(p); }
    void read_symbols(int symtab_type);

    ttr::Str path_;
    unsigned char* data_;
    size_t size_;
    bool is64_, big_;
    int type_, machine_;
    Addr entry_;
    ttr::Vector<Symbol> syms_;

    ElfFile(const ElfFile&);
    ElfFile& operator=(const ElfFile&);
};

} /* namespace td */

#endif
