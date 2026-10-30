#include "td.h"
#include "target.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace td {

Regs::Regs()
{
    clear();
}

void Regs::clear()
{
    memset(r, 0, sizeof(r));
    memset(valid, 0, sizeof(valid));
    pc = 0;
}

void Regs::set(int n, uint64_t v)
{
    if (n >= 0 && n < MAX_REGS) {
        r[n] = v;
        valid[n] = true;
    }
}

namespace {

/* System V x86-64 psABI, figure 3.36. */
const char* const k_x86_names[17] = {
    "rax", "rdx", "rcx", "rbx", "rsi", "rdi", "rbp", "rsp",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "rip"
};

const char* const k_mips_names[32] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "a4", "a5", "a6", "a7", "t0", "t1", "t2", "t3",    /* n32/n64 names */
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "s8", "ra"
};

Arch make_x86_64()
{
    Arch a;
    a.id = ARCH_X86_64;
    a.name = "x86-64";
    a.ptr_size = 8;
    a.reg_size = 8;
    a.big_endian = false;
    a.sp_reg = 7;
    a.fp_reg = 6;
    a.ra_column = 16;
    a.bp[0] = 0xcc;             /* int3 */
    a.bp[1] = a.bp[2] = a.bp[3] = 0;
    a.bp_len = 1;
    a.pc_after_bp = 1;
    a.nregs = 17;
    return a;
}

Arch make_mips(bool wide)
{
    Arch a;
    a.id = wide ? ARCH_MIPS64 : ARCH_MIPS32;
    a.name = wide ? "mips64" : "mips32";
    a.ptr_size = wide ? 8 : 4;
    a.reg_size = 8;             /* n32 and n64: 64-bit registers (o32: 4, later) */
    a.big_endian = true;
    a.sp_reg = 29;
    a.fp_reg = -1;              /* s8 is not always a frame pointer */
    a.ra_column = 64;           /* SGI's; gcc uses 31, the CIE says which */
    /* break 0: 0x0000000d, big-endian. */
    a.bp[0] = 0x00;
    a.bp[1] = 0x00;
    a.bp[2] = 0x00;
    a.bp[3] = 0x0d;
    a.bp_len = 4;
    a.pc_after_bp = 0;
    a.nregs = 32;
    return a;
}

const Arch g_x86_64 = make_x86_64();
const Arch g_mips32 = make_mips(false);
const Arch g_mips64 = make_mips(true);

} /* namespace */

const char* hex(uint64_t v, char* buf)
{
    char t[17];
    int n = 0;
    do {
        t[n++] = "0123456789abcdef"[v & 15];
        v >>= 4;
    } while (v);
    buf[0] = '0';
    buf[1] = 'x';
    int i;
    for (i = 0; i < n; i++)
        buf[2 + i] = t[n - 1 - i];
    buf[2 + n] = 0;
    return buf;
}

uint64_t parse_number(const char* s, const char** end)
{
    while (*s == ' ' || *s == '\t')
        s++;
    uint64_t v = 0;
    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    } else if (s[0] == '0') {
        base = 8;
    }
    for (;; s++) {
        int d;
        char c = *s;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        v = v * (uint64_t)base + (uint64_t)d;
    }
    if (end)
        *end = s;
    return v;
}

const Arch* arch_x86_64() { return &g_x86_64; }
const Arch* arch_mips32() { return &g_mips32; }
const Arch* arch_mips64() { return &g_mips64; }

const char* Arch::reg_name(int n) const
{
    if (id == ARCH_X86_64)
        return n >= 0 && n < 17 ? k_x86_names[n] : 0;
    if (n >= 0 && n < 32)
        return k_mips_names[n];
    return 0;
}

bool Arch::entry_return_address(const Regs& r, Target* t, Addr* ra) const
{
    if (id == ARCH_X86_64) {
        /* The call pushed it: it is at [rsp]. */
        uint64_t v = 0;
        if (!r.valid[7] || !t->read_uint(r.r[7], 8, &v))
            return false;
        *ra = v;
        return true;
    }
    /* jal / jalr left it in $ra. */
    if (!r.valid[31])
        return false;
    *ra = ptr_size == 4 ? (Addr)(uint32_t)r.r[31] : r.r[31];
    return true;
}

void Target::setup_child(const LaunchOptions& o)
{
    if (o.cwd && *o.cwd && chdir(o.cwd) != 0)
        fprintf(stderr, "cannot change to %s\n", o.cwd);
    if (o.tty && *o.tty) {
        int fd = open(o.tty, O_RDWR);
        if (fd >= 0) {
            dup2(fd, 0);
            dup2(fd, 1);
            dup2(fd, 2);
            if (fd > 2)
                close(fd);
        }
    }
}

/* An insertion sort: there are hundreds at most. */
void Target::sort_processes(ttr::Vector<ProcessInfo>* list)
{
    int i, k;
    for (i = 1; i < list->size(); i++)
        for (k = i; k > 0 && ((*list)[k - 1].start > (*list)[k].start ||
                              ((*list)[k - 1].start == (*list)[k].start &&
                               (*list)[k - 1].pid > (*list)[k].pid));
             k--) {
            ProcessInfo t = (*list)[k];
            (*list)[k] = (*list)[k - 1];
            (*list)[k - 1] = t;
        }
}

bool Target::read_string(Addr a, char* buf, size_t n)
{
    size_t i;
    if (!n)
        return false;
    for (i = 0; i + 1 < n; i++) {
        if (!read(a + i, buf + i, 1)) {
            buf[i] = 0;
            return i > 0;
        }
        if (!buf[i])
            return true;
    }
    buf[i] = 0;
    return true;
}

bool Target::dynamic_entry(Addr dynamic, uint64_t tag, uint64_t* value)
{
    int w = arch()->ptr_size;
    int i;
    for (i = 0; i < 4096; i++) {
        uint64_t t = 0, v = 0;
        if (!read_uint(dynamic + (Addr)(2 * w * i), w, &t) ||
            !read_uint(dynamic + (Addr)(2 * w * i + w), w, &v))
            return false;
        if (t == 0)
            return false;   /* DT_NULL */
        if (t == tag) {
            *value = v;
            return true;
        }
    }
    return false;
}

bool Target::read_uint(Addr a, int n, uint64_t* v)
{
    unsigned char b[8];
    if (n < 1 || n > 8 || !read(a, b, (size_t)n))
        return false;
    uint64_t x = 0;
    int i;
    if (arch()->big_endian)
        for (i = 0; i < n; i++)
            x = (x << 8) | b[i];
    else
        for (i = n - 1; i >= 0; i--)
            x = (x << 8) | b[i];
    *v = x;
    return true;
}

/* glibc's list: DT_DEBUG points to struct r_debug once ld.so has started;
 * its r_map is the link_map list. x86-64 offsets: r_debug.r_map 8;
 * link_map l_addr 0, l_name 8, l_next 24. */
bool Target::glibc_objects(Addr dynamic, ttr::Vector<SharedObject>* out)
{
    uint64_t rdebug = 0, map = 0;
    if (!dynamic || !dynamic_entry(dynamic, 21 /* DT_DEBUG */, &rdebug) || !rdebug)
        return true;        /* not started yet, or linked statically */
    if (!read_uint(rdebug + 8, 8, &map))
        return false;
    int n;
    for (n = 0; map && n < 4096; n++) {
        uint64_t addr = 0, name = 0, next = 0;
        if (!read_uint(map, 8, &addr) || !read_uint(map + 8, 8, &name) ||
            !read_uint(map + 24, 8, &next))
            return false;
        char path[4096];
        /* The executable has an empty name; the vDSO has no file. */
        if (name && read_string(name, path, sizeof(path)) && path[0] == '/' &&
            access(path, R_OK) == 0) {
            SharedObject so;
            so.path = path;
            so.bias = addr;
            out->push_back(so);
        }
        map = next;
    }
    return true;
}

/* rld's list (<objlist.h>): DT_MIPS_RLD_MAP holds the address of
 * __rld_obj_head, which points to the first Elf32_Obj_Info (64-bit
 * programs: Elf64_Obj_Info, every address 8 bytes):
 *               32-bit  64-bit
 *   oi_magic         0       0   0xffffffff
 *   oi_next          8       8
 *   oi_ehdr         16      24   where the ELF header is
 *   oi_orig_ehdr    20      32   where it was linked to be: the bias
 *   oi_pathname     24      40
 * The first entry is the executable. */
bool Target::rld_objects(Addr dynamic, ttr::Vector<SharedObject>* out)
{
    int w = arch()->ptr_size;
    uint64_t map = 0, head = 0;
    if (!dynamic || !dynamic_entry(dynamic, 0x70000016 /* DT_MIPS_RLD_MAP */, &map) || !map ||
        !read_uint(map, w, &head))
        return true;
    const int o_next = 8, o_ehdr = w == 8 ? 24 : 16, o_orig = w == 8 ? 32 : 20,
              o_path = w == 8 ? 40 : 24;
    Addr p = (Addr)head;
    int n;
    for (n = 0; p && n < 4096; n++) {
        uint64_t magic = 0, next = 0, ehdr = 0, orig = 0, name = 0;
        if (!read_uint(p, 4, &magic) || magic != 0xffffffffu)
            return false;       /* the old, pre-ELF list: not read */
        if (!read_uint(p + o_next, w, &next) || !read_uint(p + o_ehdr, w, &ehdr) ||
            !read_uint(p + o_orig, w, &orig) || !read_uint(p + o_path, w, &name))
            return false;
        char path[2048];
        if (n > 0 && name && read_string(name, path, sizeof(path)) && path[0]) {
            SharedObject so;
            so.path = path;
            so.bias = w == 4 ? (Addr)(uint32_t)(ehdr - orig) : (Addr)(ehdr - orig);
            out->push_back(so);
        }
        p = (Addr)next;
    }
    return true;
}

} /* namespace td */
