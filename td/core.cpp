/*
 * td/core.cpp - CoreTarget (core.h): Linux ELF core files and IRIX
 * core files, read on either system.
 */
#include "core.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

namespace td {

namespace {

uint64_t get(const unsigned char* p, int n, bool big)
{
    uint64_t x = 0;
    int i;
    if (big)
        for (i = 0; i < n; i++)
            x = (x << 8) | p[i];
    else
        for (i = n - 1; i >= 0; i--)
            x = (x << 8) | p[i];
    return x;
}

uint64_t le(const unsigned char* p, int n)
{
    return get(p, n, false);
}

uint64_t be(const unsigned char* p, int n)
{
    return get(p, n, true);
}

/* Linux: ELF types, notes and the x86-64 layouts (<linux/elfcore.h>). */
enum {
    ET_CORE_ = 4,
    EM_X86_64_ = 62,
    PT_LOAD_ = 1,
    PT_NOTE_ = 4,
    NT_PRSTATUS_ = 1,
    NT_PRPSINFO_ = 3,
    NT_AUXV_ = 6,
    NT_FILE_ = 0x46494c45,
    AT_ENTRY_ = 9
};
/* elf_prstatus: pr_cursig at 12, pr_pid at 32, pr_reg (user_regs_struct)
 * at 112. elf_prpsinfo: pr_fname[16] at 40. */
const int k_pr_cursig = 12, k_pr_pid = 32, k_pr_reg = 112, k_ps_fname = 40;
/* user_regs_struct's order, as DWARF numbers (-1: none; 16 is rip). */
const int k_x86_order[27] = {15, 14, 13, 12, 6,  3,  11, 10, 9,  8,  0,  2,  1, 4,
                             5,  -1, 16, -1, -1, 7,  -1, -1, -1, -1, -1, -1, -1};

/* IRIX: <core.out.h>. */
const uint32_t k_core_magic = 0xdeadadb0u;     /* o32 */
const uint32_t k_core_magic64 = 0xdeadad40u;
const uint32_t k_core_magicn32 = 0xbabec0bbu;
enum {
    CORE_NAMESIZE = 80,
    CORE_ARGSIZE = 80,
    CORE_NIDESC = 32,
    VDUMPED = 1,
    VPARTIAL = 2,
    I_GPREGS = 0,
    I_SPECREGS = 2      /* EPC first */
};
/* struct coreout: c_vmapoffset at 8, c_nvmap 12, c_name 16, c_sigcause
 * after the name and arguments, then the descriptors (offset, length,
 * flags). */
const int k_c_name = 16, k_c_sigcause = 16 + CORE_NAMESIZE + CORE_ARGSIZE,
          k_c_idesc = k_c_sigcause + 4, k_core_header = k_c_idesc + CORE_NIDESC * 12;

} /* namespace */

CoreTarget::CoreTarget()
    : fd_(-1), arch_(0), have_regs_(false), sig_(0), pid_(0), entry_(0)
{
}

CoreTarget::~CoreTarget()
{
    if (fd_ >= 0)
        close(fd_);
}

CoreTarget* CoreTarget::open(const char* path, ttr::Str* err)
{
    CoreTarget* c = new CoreTarget;
    c->fd_ = ::open(path, O_RDONLY);
    unsigned char h[k_core_header];
    memset(h, 0, sizeof(h));
    bool ok = false;
    if (c->fd_ < 0) {
        if (err) {
            char m[300];
            snprintf(m, sizeof(m), "%s: %s", path, strerror(errno));
            *err = m;
        }
    } else if (!c->read_file(0, h, 64)) {
        if (err)
            *err = "not a core file";
    } else if (h[0] == 0x7f && h[1] == 'E' && h[2] == 'L' && h[3] == 'F') {
        ok = c->open_elf(h, err);
    } else {
        ok = c->read_file(0, h, sizeof(h)) && c->open_irix(h, err);
        if (!ok && err && err->empty())
            *err = "not a core file";
    }
    if (!ok) {
        delete c;
        return 0;
    }
    return c;
}

bool CoreTarget::read_file(uint64_t off, void* buf, size_t n)
{
    if (lseek(fd_, (off_t)off, SEEK_SET) == (off_t)-1)
        return false;
    return ::read(fd_, buf, n) == (ssize_t)n;
}

/* ---- Linux ---- */

bool CoreTarget::open_elf(const unsigned char* h, ttr::Str* err)
{
    /* x86-64 only: 64-bit, little-endian. */
    if (h[4] != 2 || h[5] != 1 || le(h + 16, 2) != ET_CORE_ || le(h + 18, 2) != EM_X86_64_) {
        if (err)
            *err = "not an x86-64 core file";
        return false;
    }
    arch_ = arch_x86_64();
    uint64_t phoff = le(h + 32, 8);
    int phentsize = (int)le(h + 54, 2), phnum = (int)le(h + 56, 2);
    int i;
    for (i = 0; i < phnum; i++) {
        unsigned char p[56];
        if (phentsize < 56 || !read_file(phoff + (uint64_t)i * (uint64_t)phentsize, p, 56))
            break;
        uint32_t type = (uint32_t)le(p, 4);
        if (type == PT_LOAD_ && le(p + 32, 8)) {
            Segment s;
            s.vaddr = le(p + 16, 8);
            s.size = le(p + 32, 8);
            s.offset = le(p + 8, 8);
            segs_.push_back(s);
        } else if (type == PT_NOTE_) {
            elf_notes(le(p + 8, 8), le(p + 32, 8));
        }
    }
    if (!have_regs_) {
        if (err)
            *err = "the core file has no registers";
        return false;
    }
    return true;
}

/* NT_PRSTATUS (the first: the thread that crashed), NT_PRPSINFO,
 * NT_AUXV and NT_FILE. */
void CoreTarget::elf_notes(uint64_t off, uint64_t size)
{
    if (!size || size > 64 * 1024 * 1024)
        return;
    unsigned char* p = new unsigned char[(size_t)size];
    if (!read_file(off, p, (size_t)size)) {
        delete[] p;
        return;
    }
    uint64_t at = 0;
    while (at + 12 <= size) {
        uint64_t namesz = le(p + at, 4), descsz = le(p + at + 4, 4);
        uint32_t type = (uint32_t)le(p + at + 8, 4);
        uint64_t desc = at + 12 + ((namesz + 3) & ~(uint64_t)3);
        if (desc + descsz > size)
            break;
        const unsigned char* d = p + desc;
        if (type == NT_PRSTATUS_ && !have_regs_ && descsz >= (uint64_t)(k_pr_reg + 27 * 8)) {
            sig_ = (int)le(d + k_pr_cursig, 2);
            pid_ = (int)le(d + k_pr_pid, 4);
            regs_.clear();
            int i;
            for (i = 0; i < 27; i++)
                if (k_x86_order[i] >= 0)
                    regs_.set(k_x86_order[i], le(d + k_pr_reg + 8 * i, 8));
            regs_.pc = regs_.r[16];
            have_regs_ = true;
        } else if (type == NT_PRPSINFO_ && descsz >= (uint64_t)(k_ps_fname + 16)) {
            char n[17];
            memcpy(n, d + k_ps_fname, 16);
            n[16] = 0;
            name_ = n;
        } else if (type == NT_AUXV_) {
            uint64_t i;
            for (i = 0; i + 16 <= descsz; i += 16)
                if (le(d + i, 8) == AT_ENTRY_)
                    entry_ = le(d + i + 8, 8);
        } else if (type == NT_FILE_ && descsz >= 16) {
            /* count, page size, count (start, end, offset), the names. */
            uint64_t count = le(d, 8);
            uint64_t names = 16 + count * 24;
            if (names <= descsz) {
                const char* s = (const char*)d + names;
                const char* end = (const char*)d + descsz;
                uint64_t i;
                for (i = 0; i < count && s < end; i++) {
                    const char* e = s;
                    while (e < end && *e)
                        e++;
                    if (e == end)
                        break;      /* not terminated */
                    Mapped m;
                    m.start = le(d + 16 + i * 24, 8);
                    m.end = le(d + 16 + i * 24 + 8, 8);
                    m.path = s;
                    files_.push_back(m);
                    s = e + 1;
                }
            }
        }
        at = desc + ((descsz + 3) & ~(uint64_t)3);
    }
    delete[] p;
}

/* ---- IRIX ---- */

bool CoreTarget::open_irix(const unsigned char* h, ttr::Str* err)
{
    uint32_t magic = (uint32_t)be(h, 4);
    if (magic == k_core_magic) {
        if (err)
            *err = "an o32 program's core file: td reads n32 and 64-bit ones";
        return false;
    }
    if (magic != k_core_magicn32 && magic != k_core_magic64)
        return false;
    arch_ = magic == k_core_magic64 ? arch_mips64() : arch_mips32();
    char n[CORE_NAMESIZE + 1];
    memcpy(n, h + k_c_name, CORE_NAMESIZE);
    n[CORE_NAMESIZE] = 0;
    name_ = n;
    sig_ = (int)be(h + k_c_sigcause, 4);

    /* The vmaps: struct vmap (16 bytes) for n32, vmap64 (32) for 64-bit.
     * Only the dumped ones hold memory; text is partial, its first page. */
    uint64_t vmoff = be(h + 8, 4);
    int nvmap = (int)be(h + 12, 4), i;
    bool wide = magic == k_core_magic64;
    int vsz = wide ? 32 : 16;
    for (i = 0; i < nvmap && i < 4096; i++) {
        unsigned char v[32];
        if (!read_file(vmoff + (uint64_t)i * (uint64_t)vsz, v, (size_t)vsz))
            break;
        Segment s;
        int flags;
        if (wide) {
            s.vaddr = be(v, 8);
            s.size = be(v + 8, 8);
            s.offset = be(v + 16, 8);
            flags = (int)be(v + 24, 2);
        } else {
            s.vaddr = be(v, 4);
            s.size = be(v + 4, 4);
            s.offset = be(v + 8, 4);
            flags = (int)be(v + 12, 2);
        }
        if ((flags & (VDUMPED | VPARTIAL)) && s.size)
            segs_.push_back(s);
    }

    /* Registers: 32 general ones of 8 bytes, then EPC first of the
     * special ones, whatever the program's ABI. */
    const unsigned char* gd = h + k_c_idesc + I_GPREGS * 12;
    const unsigned char* sd = h + k_c_idesc + I_SPECREGS * 12;
    unsigned char g[256], sp[8];
    if (!(be(gd + 8, 4) & 1) || be(gd + 4, 4) < 256 || !(be(sd + 8, 4) & 1) ||
        be(sd + 4, 4) < 8 || !read_file(be(gd, 4), g, sizeof(g)) ||
        !read_file(be(sd, 4), sp, sizeof(sp))) {
        if (err)
            *err = "the core file has no registers";
        return false;
    }
    regs_.clear();
    for (i = 0; i < 32; i++)
        regs_.set(i, be(g + 8 * i, 8));
    regs_.pc = be(sp, 8);
    if (arch_->ptr_size == 4) {
        /* n32: 64-bit registers holding 32-bit addresses. */
        regs_.pc = (uint32_t)regs_.pc;
        regs_.r[29] = (uint32_t)regs_.r[29];
        regs_.r[31] = (uint32_t)regs_.r[31];
    }
    have_regs_ = true;
    return true;
}

/* ---- Target ---- */

bool CoreTarget::launch(char* const*, const LaunchOptions&, ttr::Str* err)
{
    if (err)
        *err = "a core file cannot run";
    return false;
}

bool CoreTarget::attach(int, ttr::Str* err)
{
    if (err)
        *err = "a core file cannot run";
    return false;
}

ttr::Str CoreTarget::exe_path()
{
    if (!exe_.empty())
        return exe_;
    /* Linux: the file mapped where the program's entry point is. */
    int i;
    for (i = 0; i < files_.size(); i++)
        if (entry_ >= files_[i].start && entry_ < files_[i].end)
            return files_[i].path;
    return ttr::Str("");
}

Addr CoreTarget::exe_bias(Addr elf_entry)
{
    return entry_ ? entry_ - elf_entry : 0;
}

bool CoreTarget::shared_objects(Addr dynamic, ttr::Vector<SharedObject>* out)
{
    if (arch_->id == ARCH_X86_64)
        return glibc_objects(dynamic, out);
    return rld_objects(dynamic, out);
}

bool CoreTarget::wait(Event* ev)
{
    memset(ev, 0, sizeof(*ev));
    ev->kind = EV_ERROR;
    return false;
}

bool CoreTarget::poll(Event* ev, bool* got)
{
    *got = true;
    return wait(ev);
}

bool CoreTarget::get_regs(Regs* r)
{
    *r = regs_;
    return have_regs_;
}

int CoreTarget::add_watch(Addr, size_t, ttr::Str* err)
{
    if (err)
        *err = "a core file cannot run";
    return -1;
}

void CoreTarget::add_image(const ElfFile* f, Addr bias)
{
    Image im;
    im.file = f;
    im.bias = bias;
    images_.push_back(im);
}

/* From the core where it holds the memory, else from the files. */
bool CoreTarget::read(Addr a, void* buf, size_t n)
{
    unsigned char* out = (unsigned char*)buf;
    while (n) {
        size_t got = 0;
        int i;
        for (i = 0; i < segs_.size() && !got; i++) {
            const Segment& s = segs_[i];
            if (a < s.vaddr || a >= s.vaddr + s.size)
                continue;
            size_t k = n;
            if (a + k > s.vaddr + s.size)
                k = (size_t)(s.vaddr + s.size - a);
            if (!read_file(s.offset + (a - s.vaddr), out, k))
                return false;
            got = k;
        }
        for (i = 0; i < images_.size() && !got; i++)
            if (a >= images_[i].bias)
                got = images_[i].file->read_some(a - images_[i].bias, out, n);
        if (!got)
            return false;
        a += got;
        out += got;
        n -= got;
    }
    return true;
}

} /* namespace td */
