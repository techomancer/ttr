#include "elf.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace td {

namespace {

enum {
    EM_MIPS_ = 8,
    EM_X86_64_ = 62,
    SHT_SYMTAB_ = 2,
    SHT_DYNSYM_ = 11,
    STT_OBJECT_ = 1,
    STT_FUNC_ = 2
};

int compare_symbols(const void* pa, const void* pb)
{
    const Symbol* a = (const Symbol*)pa;
    const Symbol* b = (const Symbol*)pb;
    if (a->addr != b->addr)
        return a->addr < b->addr ? -1 : 1;
    /* Functions before data at the same address, then longer first. */
    if (a->func != b->func)
        return a->func ? -1 : 1;
    if (a->size != b->size)
        return a->size > b->size ? -1 : 1;
    /* Aliases: puts before _IO_puts. */
    int ua = 0, ub = 0;
    while (a->name[ua] == '_')
        ua++;
    while (b->name[ub] == '_')
        ub++;
    if (ua != ub)
        return ua - ub;
    return strcmp(a->name, b->name);
}

} /* namespace */

ElfFile::ElfFile()
    : data_(0), size_(0), is64_(false), big_(false), type_(0), machine_(0), entry_(0),
      load_lo_(0), load_hi_(0), dyn_(0)
{
}

ElfFile::~ElfFile()
{
    free(data_);
}

uint64_t ElfFile::u16(const unsigned char* p) const
{
    return big_ ? ((uint64_t)p[0] << 8) | p[1] : ((uint64_t)p[1] << 8) | p[0];
}

uint64_t ElfFile::u32(const unsigned char* p) const
{
    return big_ ? (u16(p) << 16) | u16(p + 2) : (u16(p + 2) << 16) | u16(p);
}

uint64_t ElfFile::u64(const unsigned char* p) const
{
    return big_ ? (u32(p) << 32) | u32(p + 4) : (u32(p + 4) << 32) | u32(p);
}

bool ElfFile::load(const char* path, ttr::Str* err)
{
    path_ = path;
    FILE* f = fopen(path, "rb");
    if (!f) {
        if (err) {
            char m[600];
            snprintf(m, sizeof(m), "%.400s: %s", path, strerror(errno));
            *err = m;
        }
        return false;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data_ = n > 0 ? (unsigned char*)malloc((size_t)n) : 0;
    if (!data_ || fread(data_, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        if (err)
            *err = "cannot read the file";
        return false;
    }
    fclose(f);
    size_ = (size_t)n;
    if (size_ < 52 || memcmp(data_, "\177ELF", 4) != 0) {
        if (err)
            *err = "not an ELF file";
        return false;
    }
    is64_ = data_[4] == 2;
    big_ = data_[5] == 2;
    type_ = (int)u16(data_ + 16);
    machine_ = (int)u16(data_ + 18);
    entry_ = word(data_ + 24);
    read_segments();
    read_symbols(SHT_SYMTAB_);
    if (syms_.empty())
        read_symbols(SHT_DYNSYM_);
    if (!syms_.empty())
        qsort(&syms_[0], (size_t)syms_.size(), sizeof(Symbol), compare_symbols);
    return true;
}

void ElfFile::read_segments()
{
    uint64_t phoff = is64_ ? u64(data_ + 32) : u32(data_ + 28);
    int phentsize = (int)u16(data_ + (is64_ ? 54 : 42));
    int phnum = (int)u16(data_ + (is64_ ? 56 : 44));
    if (!phoff || phoff + (uint64_t)phnum * phentsize > size_)
        return;
    bool first = true;
    int i;
    for (i = 0; i < phnum; i++) {
        const unsigned char* ph = data_ + phoff + (uint64_t)i * phentsize;
        int type = (int)u32(ph);
        uint64_t vaddr = is64_ ? u64(ph + 16) : u32(ph + 8);
        uint64_t memsz = is64_ ? u64(ph + 40) : u32(ph + 20);
        if (type == 1) {                /* PT_LOAD */
            Segment sg;
            sg.vaddr = vaddr;
            sg.offset = is64_ ? u64(ph + 8) : u32(ph + 4);
            sg.filesz = is64_ ? u64(ph + 32) : u32(ph + 16);
            segs_.push_back(sg);
            if (first || vaddr < load_lo_)
                load_lo_ = vaddr;
            if (first || vaddr + memsz > load_hi_)
                load_hi_ = vaddr + memsz;
            first = false;
        } else if (type == 2) {         /* PT_DYNAMIC */
            dyn_ = vaddr;
        }
    }
}

void ElfFile::read_symbols(int symtab_type)
{
    const unsigned char* h = data_;
    uint64_t shoff = is64_ ? u64(h + 40) : u32(h + 32);
    int shentsize = (int)u16(h + (is64_ ? 58 : 46));
    int shnum = (int)u16(h + (is64_ ? 60 : 48));
    if (!shoff || shoff + (uint64_t)shnum * shentsize > size_)
        return;
    int i;
    for (i = 0; i < shnum; i++) {
        const unsigned char* sh = data_ + shoff + (uint64_t)i * shentsize;
        if ((int)u32(sh + 4) != symtab_type)
            continue;
        uint64_t off = is64_ ? u64(sh + 24) : u32(sh + 16);
        uint64_t size = is64_ ? u64(sh + 32) : u32(sh + 20);
        int link = (int)u32(sh + (is64_ ? 40 : 24));
        uint64_t entsize = is64_ ? u64(sh + 56) : u32(sh + 36);
        if (link >= shnum || !entsize || off + size > size_)
            continue;
        const unsigned char* strsh = data_ + shoff + (uint64_t)link * shentsize;
        uint64_t stroff = is64_ ? u64(strsh + 24) : u32(strsh + 16);
        uint64_t strsize = is64_ ? u64(strsh + 32) : u32(strsh + 20);
        if (stroff + strsize > size_)
            continue;
        const char* strtab = (const char*)data_ + stroff;
        uint64_t k;
        for (k = entsize; k + entsize <= size; k += entsize) {     /* 0 is null */
            const unsigned char* s = data_ + off + k;
            uint64_t name, value, ssize;
            int info, shndx;
            if (is64_) {
                name = u32(s);
                info = s[4];
                shndx = (int)u16(s + 6);
                value = u64(s + 8);
                ssize = u64(s + 16);
            } else {
                name = u32(s);
                value = u32(s + 4);
                ssize = u32(s + 8);
                info = s[12];
                shndx = (int)u16(s + 14);
            }
            int type = info & 15;
            if ((type != STT_FUNC_ && type != STT_OBJECT_) || shndx == 0 || name >= strsize ||
                !value)
                continue;
            Symbol sym;
            sym.name = strtab + name;
            sym.addr = value;
            sym.size = ssize;
            sym.func = type == STT_FUNC_;
            syms_.push_back(sym);
        }
    }
}

bool ElfFile::read_at(Addr a, void* buf, size_t n) const
{
    int i;
    for (i = 0; i < segs_.size(); i++) {
        const Segment& s = segs_[i];
        if (a < s.vaddr || a + n > s.vaddr + s.filesz)
            continue;
        uint64_t off = s.offset + (a - s.vaddr);
        if (off + n > size_)
            return false;
        memcpy(buf, data_ + off, n);
        return true;
    }
    return false;
}

const Arch* ElfFile::arch() const
{
    if (machine_ == EM_X86_64_)
        return arch_x86_64();
    if (machine_ == EM_MIPS_)
        return is64_ ? arch_mips64() : arch_mips32();
    return 0;
}

const Symbol* ElfFile::find(const char* name) const
{
    int i;
    for (i = 0; i < syms_.size(); i++)
        if (syms_[i].func && strcmp(syms_[i].name, name) == 0)
            return &syms_[i];
    return 0;
}

const Symbol* ElfFile::function_at(Addr a) const
{
    /* The last function starting at or before a, if a is inside it. */
    int lo = 0, hi = syms_.size() - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (syms_[mid].addr <= a) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    for (; best >= 0; best--) {
        const Symbol& s = syms_[best];
        if (!s.func)
            continue;
        if (a < s.addr + (s.size ? s.size : 1))
            return &s;
        return 0;
    }
    return 0;
}

} /* namespace td */
