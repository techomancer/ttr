/*
 * ttrcode's Disassembly window (td/DESIGN.md section 8.2), as Visual C++
 * 6 showed it: the function of the frame Call Stack shows, each source
 * line followed by its instructions, with the yellow arrow at the
 * instruction the thread is at, the green one where a caller's call
 * returns, and red dots at breakpoints. With it focused F11 and F10 step
 * one instruction (F10 over calls), Ctrl+F10 runs to the selected
 * instruction and F9 sets or clears a breakpoint on it; Enter or a
 * double click shows its source line.
 */
#include "app.h"

#include "../../td/source.h"

#include <stdio.h>
#include <string.h>

#include <X11/keysym.h>

namespace ttrcode {

namespace {

/* Sources read where the line tables say. */
td::Sources g_sources;

const uint32_t k_arrow = 0x25b6, k_dot = 0x25cf;

/* Tabs as spaces, for the grid. */
void expand_tabs(const char* in, int tab, char* out, size_t size)
{
    size_t n = 0;
    int col = 0;
    for (; *in && n + 1 < size; in++) {
        if (*in == '\t') {
            do {
                out[n++] = ' ';
                col++;
            } while (tab > 0 && col % tab && n + 1 < size);
        } else {
            out[n++] = *in;
            col++;
        }
    }
    out[n] = 0;
}

} /* namespace */

td::Addr App::disassembly_selected() const
{
    int row = dis_list_ ? dis_list_->selected() : -1;
    return row >= 0 && row < dis_addr_.size() ? dis_addr_[row] : 0;
}

void App::fill_disassembly(bool keep)
{
    if (!dis_list_)
        return;
    td::Addr keep_addr = keep ? disassembly_selected() : 0;
    dis_list_->clear();
    dis_addr_.clear();
    dis_insn_.clear();
    const td::Frame* f = frame();
    if (!f || dbg_->busy()) {
        dis_list_->done();
        return;
    }
    const td::Arch* arch = dbg_->target()->arch();
    int digits = arch->ptr_size * 2;
    /* The address column, wide enough for line numbers too. */
    static const char* const titles[2] = { "", "" };
    int widths[2] = { digits + 3, 80 };
    dis_list_->set_columns(2, titles, widths);

    /* A caller's frame: its function, where the call returns. Without a
     * function, from the pc (MIPS: a little before it). */
    td::Addr pc = f->pc, look = f->caller ? pc - 1 : pc, from = 0, to = 0;
    int max = 8000;
    if (!dbg_->function_at(look, &from, &to) || to <= from || to - from > 256 * 1024) {
        from = arch->id == td::ARCH_X86_64 ? pc : pc - 64;
        to = 0;
        max = 200;
    }
    ttr::Vector<td::Insn> insns;
    dbg_->disassemble(from, to, max, &insns, &f->regs);

    /* Where the arrows go, and the breakpoints. */
    td::Addr here = frames_.size() ? frames_[0].pc : 0;
    const ttr::Vector<td::Breakpoint>& bps = dbg_->breakpoints();
    ListColors c = list_colors();
    int i, select = -1, last_line = 0;
    const char* last_file = 0;
    for (i = 0; i < insns.size(); i++) {
        const td::Insn& in = insns[i];
        td::Place p;
        if (dbg_->line_at(in.addr, &p) && p.exact && p.file &&
            (p.line != last_line || !last_file || strcmp(p.file, last_file))) {
            last_line = p.line;
            last_file = p.file;
            char text[1024], line[1100];
            if (!g_sources.line(p.file, p.line, text, sizeof(text)))
                text[0] = 0;
            expand_tabs(text, opt_.tab_width, line, sizeof(line));
            char num[24];
            snprintf(num, sizeof(num), "%d:", p.line);
            int r = dis_list_->add_row();
            dis_list_->set_cell(r, 0, num);
            dis_list_->set_cell(r, 1, line);
            dis_addr_.push_back(in.addr);
            dis_insn_.push_back(0);
        }
        char addr[24], code[512];
        if (digits > 8)
            snprintf(addr, sizeof(addr), "%08lx%08lx", (unsigned long)(in.addr >> 32),
                     (unsigned long)(in.addr & 0xffffffffUL));
        else
            snprintf(addr, sizeof(addr), "%08lx", (unsigned long)in.addr);
        int pad = (int)strlen(in.mnemonic) < 12 ? 12 - (int)strlen(in.mnemonic) : 1;
        snprintf(code, sizeof(code), "%s%*s%s%s%s", in.mnemonic, in.operands[0] ? pad : 0, "",
                 in.operands, in.comment[0] ? "  ; " : "", in.comment);
        int r = dis_list_->add_row();
        dis_list_->set_cell(r, 0, addr, c.dim);
        dis_list_->set_cell(r, 1, code);
        dis_addr_.push_back(in.addr);
        dis_insn_.push_back(1);
        int k;
        for (k = 0; k < bps.size(); k++)
            if (bps[k].id > 0 && bps[k].addr == in.addr)
                dis_list_->set_mark(r, k_dot, 0xd00000);
        if (in.addr == here)
            dis_list_->set_mark(r, k_arrow, 0xe0b000);
        else if (cur_frame_ > 0 && in.addr == pc)
            dis_list_->set_mark(r, k_arrow, 0x00a000);
        if (keep_addr ? in.addr == keep_addr : in.addr == pc)
            select = r;
    }
    dis_list_->done();
    if (select >= 0) {
        /* Scrolled to it, with the rows before it (its source line) in
         * view too. */
        if (select < dis_list_->top() || select >= dis_list_->top() + dis_list_->visible_rows())
            dis_list_->scroll_to(select - 3);
        dis_list_->select(select);
    }
}

/* The window, open and focused: for a stop without source. */
void App::show_disassembly()
{
    int i;
    for (i = 0; i < debug_panes_.size(); i++)
        if (!strcmp(debug_panes_[i]->id(), "disassembly")) {
            if (!dis_list_)
                show_pane(debug_panes_[i]);
            else
                dis_list_->focus();
            return;
        }
}

bool App::disassembly_key(KeySym ks, unsigned state)
{
    bool ctrl = (state & ControlMask) != 0, shift = (state & ShiftMask) != 0;
    if (state & Mod1Mask)
        return false;
    if (ks == XK_F9 && !ctrl && !shift) {
        disassembly_toggle_breakpoint();
        return true;
    }
    if (ks == XK_F10 && !shift) {
        command(ctrl ? CMD_DEBUG_RUN_TO_INSN : CMD_DEBUG_NEXT_INSN);
        return true;
    }
    if (ks == XK_F11 && !ctrl && !shift) {
        command(CMD_DEBUG_STEP_INSN);
        return true;
    }
    return false;
}

/* F9: a breakpoint on the selected instruction ("*0x401020"), or none
 * there any more (whatever set it: a line, a function, an address). */
void App::disassembly_toggle_breakpoint()
{
    td::Addr a = disassembly_selected();
    if (!dbg_ || dbg_->busy() || !a)
        return;
    if (dbg_->is_core()) {
        debug_status("A core file does not run: no breakpoints.");
        return;
    }
    const ttr::Vector<td::Breakpoint>& b = dbg_->breakpoints();
    int k, i;
    for (k = 0; k < b.size(); k++)
        if (b[k].id > 0 && b[k].addr == a)
            break;
    if (k < b.size()) {
        ttr::Str where = b[k].where;
        dbg_->remove_breakpoint(b[k].id);
        for (i = 0; i < bps_.size(); i++)
            if (!strcmp(breakpoint_spec(bps_[i]).c_str(), where.c_str())) {
                bps_.remove_at(i);
                break;
            }
    } else {
        char spec[40], h[24];
        snprintf(spec, sizeof(spec), "*%s", td::hex(a, h));
        ttr::Str err;
        if (!dbg_->add_breakpoint(spec, &err)) {
            debug_status(err.c_str());
            return;
        }
        SourceBreakpoint s;
        s.line = 0;
        s.func = spec;
        bps_.push_back(s);
    }
    save_settings();
    fill_breakpoints();
    TwGridDamage(grid_);
    fill_disassembly(true);
}

/* Enter, a double click: the instruction's source line. */
void App::disassembly_activate(int row)
{
    if (!dbg_ || row < 0 || row >= dis_addr_.size())
        return;
    td::Place p;
    if (dbg_->line_at(dis_addr_[row], &p) && p.file)
        goto_source(p.file, p.line - 1);
}

} /* namespace ttrcode */
