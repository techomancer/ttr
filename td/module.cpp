#include "module.h"
#include "target.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* libdwarf is C (third_party/libdwarf, td/DESIGN.md 5.4). */
#include "dwarf.h"
#include "libdwarf.h"

namespace td {

namespace {

Dwarf_Debug dbg_of(void* p)
{
    return (Dwarf_Debug)p;
}

void drop_error(Dwarf_Debug dbg, Dwarf_Error* err)
{
    if (*err)
        dwarf_dealloc_error(dbg, *err);
    *err = 0;
}

/* Sort key: address, then end-of-sequence rows first (so a sequence
 * starting where another ends wins the lookup), then input order. */
struct SortRow {
    LineRow row;
    int order;
};

int compare_rows(const void* pa, const void* pb)
{
    const SortRow* a = (const SortRow*)pa;
    const SortRow* b = (const SortRow*)pb;
    if (a->row.addr != b->row.addr)
        return a->row.addr < b->row.addr ? -1 : 1;
    if (a->row.end != b->row.end)
        return a->row.end ? -1 : 1;
    return a->order - b->order;
}

/* "dir/a/b.c" matches "b.c" and "a/b.c", whole components only. */
bool file_matches(const char* full, const char* want)
{
    size_t fl = strlen(full), wl = strlen(want);
    if (wl > fl)
        return false;
    if (strcmp(full + fl - wl, want) != 0)
        return false;
    return wl == fl || full[fl - wl - 1] == '/';
}

} /* namespace */

Module::Module() : bias_(0), dbg_(0), info_(0)
{
    cie_[0] = cie_[1] = fde_[0] = fde_[1] = 0;
    ncie_[0] = ncie_[1] = nfde_[0] = nfde_[1] = 0;
}

Module::~Module()
{
    delete info_;
    Dwarf_Debug dbg = dbg_of(dbg_);
    int k;
    for (k = 0; k < 2; k++)
        if (cie_[k] || fde_[k])
            dwarf_dealloc_fde_cie_list(dbg, (Dwarf_Cie*)cie_[k], ncie_[k], (Dwarf_Fde*)fde_[k],
                                       nfde_[k]);
    if (dbg)
        dwarf_finish(dbg);
    int i;
    for (i = 0; i < files_.size(); i++)
        free(files_[i]);
}

bool Module::load(const char* path, ttr::Str* err)
{
    if (!elf_.load(path, err))
        return false;
    Dwarf_Debug dbg = 0;
    Dwarf_Error de = 0;
    int res = dwarf_init_path(path, 0, 0, DW_GROUPNUMBER_ANY, 0, 0, &dbg, &de);
    if (res != DW_DLV_OK) {
        /* No DWARF: symbols only. */
        drop_error(0, &de);
        return true;
    }
    dbg_ = dbg;
    /* Room for every DWARF register td knows; a register no rule names
     * keeps its value (callee-saved registers the code never touches). */
    dwarf_set_frame_rule_table_size(dbg, MAX_REGS);
    dwarf_set_frame_rule_initial_value(dbg, DW_FRAME_SAME_VAL);
    read_lines();
    read_frames();
    return true;
}

int Module::intern_file(const char* name)
{
    int i;
    for (i = files_.size() - 1; i >= 0; i--)
        if (strcmp(files_[i], name) == 0)
            return i;
    char* copy = (char*)malloc(strlen(name) + 1);
    if (!copy)
        return -1;
    strcpy(copy, name);
    files_.push_back(copy);
    return files_.size() - 1;
}

void Module::read_lines()
{
    Dwarf_Debug dbg = dbg_of(dbg_);
    Dwarf_Error de = 0;
    ttr::Vector<SortRow> all;
    for (;;) {
        Dwarf_Die cu = 0;
        Dwarf_Unsigned len = 0, typeoff = 0, next = 0;
        Dwarf_Half ver = 0, asize = 0, lsize = 0, esize = 0, cutype = 0;
        Dwarf_Off abbrev = 0;
        Dwarf_Sig8 sig;
        int res = dwarf_next_cu_header_e(dbg, 1, &cu, &len, &ver, &abbrev, &asize, &lsize,
                                         &esize, &sig, &typeoff, &next, &cutype, &de);
        if (res != DW_DLV_OK) {
            drop_error(dbg, &de);
            break;
        }
        Dwarf_Unsigned lver = 0;
        Dwarf_Small tables = 0;
        Dwarf_Line_Context ctx = 0;
        if (dwarf_srclines_b(cu, &lver, &tables, &ctx, &de) == DW_DLV_OK) {
            Dwarf_Line* lines = 0;
            Dwarf_Signed n = 0;
            if (dwarf_srclines_from_linecontext(ctx, &lines, &n, &de) == DW_DLV_OK) {
                Dwarf_Signed i;
                for (i = 0; i < n; i++) {
                    Dwarf_Addr a = 0;
                    Dwarf_Unsigned ln = 0;
                    Dwarf_Bool end = 0;
                    char* src = 0;
                    if (dwarf_lineaddr(lines[i], &a, &de) != DW_DLV_OK ||
                        dwarf_lineno(lines[i], &ln, &de) != DW_DLV_OK) {
                        drop_error(dbg, &de);
                        continue;
                    }
                    dwarf_lineendsequence(lines[i], &end, &de);
                    SortRow r;
                    r.row.addr = a;
                    r.row.line = (int)ln;
                    r.row.end = end != 0;
                    r.row.file = -1;
                    if (dwarf_linesrc(lines[i], &src, &de) == DW_DLV_OK) {
                        r.row.file = intern_file(src);
                        dwarf_dealloc(dbg, src, DW_DLA_STRING);
                    } else {
                        drop_error(dbg, &de);
                    }
                    r.order = all.size();
                    all.push_back(r);
                }
            } else {
                drop_error(dbg, &de);
            }
            dwarf_srclines_dealloc_b(ctx);
        } else {
            drop_error(dbg, &de);
        }
        dwarf_dealloc_die(cu);
    }
    if (all.empty())
        return;
    qsort(&all[0], (size_t)all.size(), sizeof(SortRow), compare_rows);
    int i;
    for (i = 0; i < all.size(); i++)
        rows_.push_back(all[i].row);
}

void Module::read_frames()
{
    Dwarf_Debug dbg = dbg_of(dbg_);
    Dwarf_Error de = 0;
    Dwarf_Cie* cie = 0;
    Dwarf_Fde* fde = 0;
    Dwarf_Signed nc = 0, nf = 0;
    if (dwarf_get_fde_list_eh(dbg, &cie, &nc, &fde, &nf, &de) == DW_DLV_OK) {
        cie_[0] = cie;
        fde_[0] = fde;
        ncie_[0] = (long)nc;
        nfde_[0] = (long)nf;
    } else {
        drop_error(dbg, &de);
    }
    cie = 0;
    fde = 0;
    if (dwarf_get_fde_list(dbg, &cie, &nc, &fde, &nf, &de) == DW_DLV_OK) {
        cie_[1] = cie;
        fde_[1] = fde;
        ncie_[1] = (long)nc;
        nfde_[1] = (long)nf;
    } else {
        drop_error(dbg, &de);
    }
}

bool Module::contains(Addr pc) const
{
    if (elf_.load_high() > elf_.load_low())
        return pc >= elf_.load_low() + bias_ && pc < elf_.load_high() + bias_;
    Addr start, end;
    return function_at(pc, &start, &end) != 0 || place_of(pc, 0);
}

bool Module::function_named(const char* name, Addr* start, Addr* end) const
{
    const Symbol* s = elf_.find(name);
    if (!s)
        return false;
    *start = s->addr + bias_;
    *end = s->addr + s->size + bias_;
    return true;
}

const char* Module::function_at(Addr pc, Addr* start, Addr* end) const
{
    const Symbol* s = elf_.function_at(pc - bias_);
    if (!s)
        return 0;
    if (start)
        *start = s->addr + bias_;
    if (end)
        *end = s->addr + s->size + bias_;
    return s->name;
}

bool Module::place_of(Addr pc, Place* p) const
{
    Addr a = pc - bias_;
    int lo = 0, hi = rows_.size() - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (rows_[mid].addr <= a) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (best < 0 || rows_[best].end)
        return false;
    if (p) {
        const LineRow& r = rows_[best];
        p->file = r.file >= 0 ? files_[r.file] : 0;
        p->line = r.line;
        p->exact = r.addr == a;
    }
    return true;
}

int Module::line_addresses(const char* file, int line, ttr::Vector<Addr>* out) const
{
    /* The first line at or after the one asked for that has code. */
    int best = 0;
    int i;
    for (i = 0; i < rows_.size(); i++) {
        const LineRow& r = rows_[i];
        if (r.end || r.file < 0 || r.line < line || (best && r.line >= best))
            continue;
        if (file_matches(files_[r.file], file))
            best = r.line;
    }
    if (!best)
        return 0;
    /* Where each run of that line starts. */
    for (i = 0; i < rows_.size(); i++) {
        const LineRow& r = rows_[i];
        if (r.end || r.line != best || r.file < 0 || !file_matches(files_[r.file], file))
            continue;
        if (i > 0 && !rows_[i - 1].end && rows_[i - 1].line == best &&
            rows_[i - 1].file == r.file)
            continue;
        Addr a = r.addr + bias_;
        if (out->find(a) < 0)
            out->push_back(a);
    }
    return best;
}

/* The second line row in the function: past the prologue in code that is
 * not optimized. 0 if there is none. */
static Addr second_row(const ttr::Vector<LineRow>& rows, Addr s, Addr e)
{
    int i;
    int first_line = -1;
    for (i = 0; i < rows.size(); i++) {
        const LineRow& r = rows[i];
        if (r.end || r.addr < s)
            continue;
        if (r.addr >= e)
            break;
        if (first_line < 0) {
            first_line = r.line;
            continue;
        }
        if (r.addr > s && r.line != first_line)
            return r.addr;
    }
    return 0;
}

namespace {

uint32_t be32(const unsigned char* b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

/* MIPS instructions that set up a frame: stack and frame pointer
 * adjustments, register saves into it, the gp setup of PIC code. */
bool mips_prologue_insn(uint32_t w)
{
    unsigned op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31;
    unsigned rd = (w >> 11) & 31, funct = w & 63;
    if ((op == 9 || op == 25) && rs == 29 && rt == 29)          /* (d)addiu sp,sp,n */
        return true;
    if ((op == 43 || op == 63 || op == 57 || op == 61) && (rs == 29 || rs == 30))
        return true;                                            /* sw sd swc1 sdc1 x,n(sp) */
    if (op == 15 && rt == 28)                                   /* lui gp */
        return true;
    if ((op == 9 || op == 25) && rt == 28)                      /* (d)addiu gp,..,n */
        return true;
    if (op == 0 && rd == 28 && (funct == 0x21 || funct == 0x2d))  /* (d)addu gp,gp,t9 */
        return true;
    if (op == 0 && rd == 30 && rs == 29 && (funct == 0x21 || funct == 0x2d || funct == 0x25))
        return true;                                            /* move s8,sp */
    return false;
}

bool mips_branch(uint32_t w)
{
    unsigned op = w >> 26, rs = (w >> 21) & 31, funct = w & 63;
    if (op == 0)
        return funct == 8 || funct == 9;                        /* jr jalr */
    if (op == 1 || op == 2 || op == 3)                          /* regimm, j, jal */
        return true;
    if ((op >= 4 && op <= 7) || (op >= 20 && op <= 23))         /* beq.. beql.. */
        return true;
    if (op == 17 && rs == 8)                                    /* bc1t/f */
        return true;
    return false;
}

} /* namespace */

Addr Module::after_prologue(Addr start, Addr end) const
{
    Addr s = start - bias_, e = end - bias_;
    Addr line2 = second_row(rows_, s, e);
    const Arch* arch = elf_.arch();
    if (!arch || (arch->id != ARCH_MIPS32 && arch->id != ARCH_MIPS64))
        return line2 ? line2 + bias_ : start;
    /* MIPS: scan the prologue, as dbx does. Optimized code (IRIX's own
     * libraries) can reach the second line row only through a branch, so
     * use it only when straight-line code from the prologue's end gets
     * there. */
    Addr p = s;
    unsigned char b[4];
    int i;
    for (i = 0; i < 64 && p + 4 <= e && elf_.read_at(p, b, 4); i++, p += 4)
        if (!mips_prologue_insn(be32(b)))
            break;
    if (line2 && line2 >= p) {
        Addr q;
        for (q = p; q < line2; q += 4)
            if (!elf_.read_at(q, b, 4) || mips_branch(be32(b)))
                break;
        if (q >= line2)
            return line2 + bias_;
    }
    return p + bias_;
}

Info* Module::info() const
{
    if (!info_ && dbg_ && elf_.arch())
        info_ = new Info(dbg_, elf_.arch());
    return info_;
}

/* The frame table's rules at a pc (look: a link address), and the return
 * address column the CIE names. */
bool Module::frame_rules(Addr look, void* vtable, int* ra_col) const
{
    Dwarf_Debug dbg = dbg_of(dbg_);
    Dwarf_Error de = 0;
    Dwarf_Fde found = 0;
    int k;
    for (k = 0; dbg && k < 2 && !found; k++) {
        Dwarf_Addr lo = 0, hi = 0;
        if (!fde_[k])
            continue;
        if (dwarf_get_fde_at_pc((Dwarf_Fde*)fde_[k], look, &found, &lo, &hi, &de) !=
            DW_DLV_OK) {
            drop_error(dbg, &de);
            found = 0;
        }
    }
    if (!found)
        return false;
    Dwarf_Regtable3* table = (Dwarf_Regtable3*)vtable;
    Dwarf_Addr row_pc = 0, next_pc = 0;
    Dwarf_Bool more = 0;
    if (dwarf_get_fde_info_for_all_regs3_b(found, look, table, &row_pc, &more, &next_pc, &de) !=
        DW_DLV_OK) {
        drop_error(dbg, &de);
        return false;
    }
    Dwarf_Cie cie = 0;
    if (dwarf_get_cie_of_fde(found, &cie, &de) == DW_DLV_OK) {
        Dwarf_Unsigned bytes = 0, initlen = 0, codealign = 0;
        Dwarf_Small ver = 0;
        char* aug = 0;
        Dwarf_Signed dataalign = 0;
        Dwarf_Half rareg = 0, offsize = 0;
        Dwarf_Small* init = 0;
        if (dwarf_get_cie_info_b(cie, &bytes, &ver, &aug, &codealign, &dataalign, &rareg, &init,
                                 &initlen, &offsize, &de) == DW_DLV_OK)
            *ra_col = rareg;
        else
            drop_error(dbg, &de);
    } else {
        drop_error(dbg, &de);
    }
    return true;
}

bool Module::cfa(const Regs& in, bool caller, Addr* out) const
{
    Dwarf_Regtable_Entry3 rules[MAX_REGS];
    memset(rules, 0, sizeof(rules));
    Dwarf_Regtable3 table;
    memset(&table, 0, sizeof(table));
    table.rt3_reg_table_size = MAX_REGS;
    table.rt3_rules = rules;
    int ra_col = 0;
    if (!frame_rules(in.pc - bias_ - (caller ? 1 : 0), &table, &ra_col))
        return false;
    const Dwarf_Regtable_Entry3& c = table.rt3_cfa_rule;
    if (c.dw_value_type != DW_EXPR_OFFSET || c.dw_regnum >= MAX_REGS || !in.valid[c.dw_regnum])
        return false;
    *out = in.r[c.dw_regnum] + (Addr)(int64_t)c.dw_offset;
    return true;
}

bool Module::unwind(Target* t, const Regs& in, bool caller, Regs* out) const
{
    const Arch* arch = elf_.arch();
    if (!arch)
        return false;
    Dwarf_Addr look = in.pc - bias_ - (caller ? 1 : 0);
    Dwarf_Regtable_Entry3 rules[MAX_REGS];
    memset(rules, 0, sizeof(rules));
    Dwarf_Regtable3 table;
    memset(&table, 0, sizeof(table));
    table.rt3_reg_table_size = MAX_REGS;
    table.rt3_rules = rules;
    int ra_col = arch->ra_column;
    out->clear();
    if (!frame_rules(look, &table, &ra_col)) {
        /* No frame information: follow the frame pointer (x86-64). */
        if (arch->fp_reg < 0 || !in.valid[arch->fp_reg] || !in.r[arch->fp_reg])
            return false;
        Addr fp = in.r[arch->fp_reg];
        uint64_t saved_fp = 0, ret = 0;
        if (!t->read_uint(fp, arch->ptr_size, &saved_fp) ||
            !t->read_uint(fp + arch->ptr_size, arch->ptr_size, &ret) || !ret)
            return false;
        out->set(arch->fp_reg, saved_fp);
        out->set(arch->sp_reg, fp + 2 * arch->ptr_size);
        out->pc = ret;
        return true;
    }

    /* The frame's address: a register plus an offset. */
    const Dwarf_Regtable_Entry3& c = table.rt3_cfa_rule;
    if (c.dw_value_type != DW_EXPR_OFFSET || c.dw_regnum >= MAX_REGS || !in.valid[c.dw_regnum])
        return false;
    Addr cfa = in.r[c.dw_regnum] + (Addr)(int64_t)c.dw_offset;

    int i;
    for (i = 0; i < MAX_REGS; i++) {
        const Dwarf_Regtable_Entry3& e = rules[i];
        if (e.dw_regnum == DW_FRAME_SAME_VAL) {
            if (in.valid[i])
                out->set(i, in.r[i]);
        } else if (e.dw_regnum == DW_FRAME_UNDEFINED_VAL) {
            /* not known in the caller */
        } else if (e.dw_value_type == DW_EXPR_OFFSET) {
            if (e.dw_regnum == DW_FRAME_CFA_COL) {
                uint64_t v = 0;
                if (t->read_uint(cfa + (Addr)(int64_t)e.dw_offset, arch->reg_size, &v))
                    out->set(i, v);
            } else if (e.dw_regnum < MAX_REGS && in.valid[e.dw_regnum]) {
                out->set(i, in.r[e.dw_regnum]);
            }
        } else if (e.dw_value_type == DW_EXPR_VAL_OFFSET) {
            out->set(i, cfa + (Addr)(int64_t)e.dw_offset);
        }
        /* DWARF expressions: not yet. */
    }
    out->set(arch->sp_reg, cfa);
    if (ra_col < 0 || ra_col >= MAX_REGS || !out->valid[ra_col] ||
        rules[ra_col].dw_regnum == DW_FRAME_SAME_VAL)
        return false;
    out->pc = out->r[ra_col];
    if (arch->ptr_size == 4)
        out->pc = (uint32_t)out->pc;
    return out->pc != 0;
}

} /* namespace td */
