#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace td {

namespace {

/* Single steps before giving up on reaching another line. */
const long k_max_steps = 2000000;

const char* base_name(const char* p)
{
    const char* s = strrchr(p, '/');
    return s ? s + 1 : p;
}

} /* namespace */

Session::Session() : target_(0), exe_(0), alive_(false), next_id_(1), pending_sig_(0) {}

Session::~Session()
{
    delete target_;
    delete exe_;
}

bool Session::load_exe(ttr::Str* err)
{
    ttr::Str path = target_->exe_path();
    exe_ = new Module;
    if (!exe_->load(path.c_str(), err))
        return false;
    if (exe_->elf().arch() != target_->arch()) {
        if (err)
            *err = "the program is not for this processor";
        return false;
    }
    if (exe_->elf().position_independent())
        exe_->set_bias(target_->exe_bias(exe_->elf().entry()));
    return true;
}

bool Session::start(char* const* argv, ttr::Str* err)
{
    target_ = Target::create();
    if (!target_->launch(argv, err))
        return false;
    alive_ = true;
    return load_exe(err);
}

bool Session::attach(int pid, ttr::Str* err)
{
    target_ = Target::create();
    if (!target_->attach(pid, err))
        return false;
    alive_ = true;
    return load_exe(err);
}

/* ------------------------------------------------------------------ */
/* Breakpoints                                                         */

Addr Session::resolve(const char* spec, ttr::Str* err, ttr::Vector<Addr>* more)
{
    if (spec[0] == '*')
        return parse_number(spec + 1, 0);
    const char* colon = strrchr(spec, ':');
    if (colon && colon[1] >= '0' && colon[1] <= '9') {
        char file[1024];
        size_t n = (size_t)(colon - spec);
        if (n >= sizeof(file))
            n = sizeof(file) - 1;
        memcpy(file, spec, n);
        file[n] = 0;
        ttr::Vector<Addr> addrs;
        if (!exe_->line_addresses(file, atoi(colon + 1), &addrs) || addrs.empty()) {
            if (err)
                *err = "no code at that line";
            return 0;
        }
        int i;
        for (i = 1; i < addrs.size(); i++)
            more->push_back(addrs[i]);
        return addrs[0];
    }
    Addr start, end;
    if (!exe_->function_named(spec, &start, &end)) {
        if (err)
            *err = "no function by that name";
        return 0;
    }
    return exe_->has_lines() ? exe_->after_prologue(start, end) : start;
}

int Session::add_breakpoint(const char* spec, ttr::Str* err)
{
    if (!exe_) {
        if (err)
            *err = "no program";
        return 0;
    }
    ttr::Vector<Addr> more;
    Addr a = resolve(spec, err, &more);
    if (!a)
        return 0;
    more.insert_front(a);
    int id = next_id_++;
    int i;
    for (i = 0; i < more.size(); i++) {
        Breakpoint b;
        b.id = id;
        b.addr = more[i];
        b.inserted = false;
        b.where = spec;
        memset(b.saved, 0, sizeof(b.saved));
        bps_.push_back(b);
    }
    return id;
}

bool Session::remove_breakpoint(int id)
{
    bool found = false;
    int i;
    for (i = bps_.size() - 1; i >= 0; i--)
        if (bps_[i].id == id) {
            bps_.remove_at(i);
            found = true;
        }
    return found;
}

int Session::bp_at(Addr a) const
{
    int i;
    for (i = 0; i < bps_.size(); i++)
        if (bps_[i].addr == a)
            return i;
    return -1;
}

/* Breakpoints are in memory only while the program runs, so reading its
 * code never shows them. Several at one address share one instruction. */
void Session::insert_all()
{
    const Arch* arch = target_->arch();
    int i, j;
    for (i = 0; i < bps_.size(); i++) {
        Breakpoint& b = bps_[i];
        if (b.inserted)
            continue;
        for (j = 0; j < i; j++)
            if (bps_[j].inserted && bps_[j].addr == b.addr)
                break;
        if (j < i) {
            memcpy(b.saved, bps_[j].saved, sizeof(b.saved));
            b.inserted = true;
            continue;
        }
        if (!target_->read(b.addr, b.saved, (size_t)arch->bp_len))
            continue;
        if (target_->write(b.addr, arch->bp, (size_t)arch->bp_len))
            b.inserted = true;
    }
}

void Session::remove_all()
{
    const Arch* arch = target_->arch();
    int i;
    for (i = bps_.size() - 1; i >= 0; i--) {
        Breakpoint& b = bps_[i];
        if (!b.inserted)
            continue;
        if (alive_)
            target_->write(b.addr, b.saved, (size_t)arch->bp_len);
        b.inserted = false;
    }
}

/* ------------------------------------------------------------------ */
/* Running                                                             */

Stop Session::to_stop(const Event& e)
{
    Stop s;
    s.kind = e.kind;
    s.bp = 0;
    s.sig = e.sig;
    s.status = e.status;
    s.pc = e.pc;
    if (e.kind == EV_EXITED || e.kind == EV_KILLED || e.kind == EV_ERROR) {
        alive_ = false;
        pending_sig_ = 0;
    } else if (e.kind == EV_SIGNAL) {
        pending_sig_ = e.sig;     /* delivered when it runs again */
    } else if (e.kind == EV_BREAK) {
        int i = bp_at(e.pc);
        if (i < 0) {
            /* Not ours: the program's own trap. */
            s.kind = EV_SIGNAL;
            s.sig = 5;
        } else {
            s.bp = bps_[i].id;
        }
    }
    return s;
}

/* Off a breakpoint at the pc: run its instruction with the breakpoint
 * out. *done: the step ended the run (a signal, an exit). */
Stop Session::step_over_bp(bool* done)
{
    Stop s;
    memset(&s, 0, sizeof(s));
    *done = false;
    Regs r;
    if (!target_->get_regs(&r) || bp_at(r.pc) < 0)
        return s;
    int sig = pending_sig_;
    pending_sig_ = 0;
    Event e;
    if (!target_->resume(true, sig) || !target_->wait(&e)) {
        e.kind = EV_ERROR;
        *done = true;
        return to_stop(e);
    }
    if (e.kind != EV_STEP) {
        *done = true;
        return to_stop(e);
    }
    return s;
}

Stop Session::run_until_stop(bool step)
{
    Event e;
    memset(&e, 0, sizeof(e));
    if (!alive_) {
        e.kind = EV_ERROR;
        return to_stop(e);
    }
    if (!step) {
        bool done;
        Stop s = step_over_bp(&done);
        if (done)
            return s;
        insert_all();
    }
    int sig = pending_sig_;
    pending_sig_ = 0;
    if (!target_->resume(step, sig) || !target_->wait(&e))
        e.kind = EV_ERROR;
    remove_all();       /* after an exit only the flags are cleared */
    return to_stop(e);
}

Stop Session::cont()
{
    return run_until_stop(false);
}

Stop Session::step_instruction()
{
    return run_until_stop(true);
}

/* Run until the pc is a with the stack pointer at least min_sp (0: any),
 * through a temporary breakpoint. A user breakpoint, a signal or the end
 * stops it first. */
Stop Session::run_to(Addr a, Addr min_sp)
{
    const Arch* arch = target_->arch();
    for (;;) {
        Breakpoint tmp;
        tmp.id = 0;
        tmp.addr = a;
        tmp.inserted = false;
        memset(tmp.saved, 0, sizeof(tmp.saved));
        bps_.push_back(tmp);
        Stop s = cont();
        int i;
        for (i = bps_.size() - 1; i >= 0; i--)
            if (bps_[i].id == 0) {
                bps_.remove_at(i);
                break;
            }
        if (s.kind != EV_BREAK || s.pc != a)
            return s;
        if (s.bp == 0 && bp_at(a) >= 0)
            s.bp = bps_[bp_at(a)].id;   /* a user breakpoint there too */
        Regs r;
        target_->get_regs(&r);
        if (!min_sp || r.r[arch->sp_reg] >= min_sp || s.bp)
            return s;
        /* A deeper recursive call got there first: on. */
    }
}

Stop Session::step_line(bool over)
{
    const Arch* arch = target_->arch();
    Regs r;
    Stop s;
    memset(&s, 0, sizeof(s));
    if (!alive_ || !target_->get_regs(&r)) {
        s.kind = EV_ERROR;
        return s;
    }
    Place p0;
    if (!exe_->place_of(r.pc, &p0))
        return step_instruction();      /* no lines here: one instruction */
    Addr f0 = 0;
    exe_->function_at(r.pc, &f0, 0);

    Addr prev = r.pc;
    long n;
    for (n = 0; n < k_max_steps; n++) {
        /* Breakpoints are out of memory while it is stopped, so a single
         * step needs nothing special. */
        s = step_instruction();
        if (s.kind != EV_STEP || !target_->get_regs(&r))
            return s;
        Addr pc = r.pc;
        Addr fstart = 0;
        bool known = exe_->function_at(pc, &fstart, 0) != 0;
        /* At the first instruction of a function: a call. */
        if (known && pc == fstart && pc != prev) {
            Addr ra = 0;
            Place pp;
            bool lines = exe_->place_of(pc, &pp);
            if (!over && lines) {
                Addr end = 0;
                exe_->function_at(pc, 0, &end);
                Addr body = exe_->after_prologue(pc, end);
                if (body != pc)
                    return run_to(body, 0);
                s.kind = EV_STEP;
                s.pc = pc;
                return s;
            }
            if (!arch->entry_return_address(r, target_, &ra))
                return run_to(pc, 0);
            s = run_to(ra, r.r[arch->sp_reg]);
            if (s.kind != EV_BREAK || s.bp || s.pc != ra)
                return s;
            target_->get_regs(&r);
            pc = r.pc;
        } else if (!known && !exe_->place_of(pc, 0)) {
            /* Into code td has no symbols for (a library, or out of
             * main): run until something stops it. */
            Addr ra = 0;
            if (arch->entry_return_address(r, target_, &ra) && exe_->place_of(ra, 0)) {
                s = run_to(ra, r.r[arch->sp_reg]);
                if (s.kind != EV_BREAK || s.bp || s.pc != ra)
                    return s;
                target_->get_regs(&r);
                pc = r.pc;
            } else {
                return cont();
            }
        }
        prev = pc;
        Place p;
        if (!exe_->place_of(pc, &p) || !p.exact)
            continue;
        Addr fs = 0;
        exe_->function_at(pc, &fs, 0);
        if (p.line != p0.line || fs != f0 ||
            (p.file && p0.file && strcmp(p.file, p0.file) != 0)) {
            s.kind = EV_STEP;
            s.bp = 0;
            s.pc = pc;
            return s;
        }
    }
    s.kind = EV_STEP;
    s.pc = r.pc;
    return s;
}

Stop Session::finish()
{
    ttr::Vector<Frame> frames;
    Stop s;
    memset(&s, 0, sizeof(s));
    if (backtrace(&frames, 2) < 2) {
        s.kind = EV_ERROR;
        return s;
    }
    const Arch* arch = target_->arch();
    Addr sp = frames[1].regs.valid[arch->sp_reg] ? frames[1].regs.r[arch->sp_reg] : 0;
    s = run_to(frames[1].pc, sp);
    if (s.kind == EV_BREAK && !s.bp && s.pc == frames[1].pc)
        s.kind = EV_STEP;
    return s;
}

void Session::kill()
{
    if (!target_)
        return;
    remove_all();
    target_->kill();
    alive_ = false;
}

void Session::detach()
{
    if (!target_ || !alive_)
        return;
    remove_all();
    target_->detach();
    alive_ = false;
}

/* ------------------------------------------------------------------ */
/* Stack                                                               */

bool Session::regs(Regs* r)
{
    return alive_ && target_->get_regs(r);
}

bool Session::describe(Addr pc, bool caller, Frame* f)
{
    f->pc = pc;
    f->func_start = 0;
    f->func = exe_ ? exe_->function_at(pc - (caller ? 1 : 0), &f->func_start, 0) : 0;
    f->has_place = exe_ && exe_->place_of(pc - (caller ? 1 : 0), &f->place);
    if (!f->has_place) {
        f->place.file = 0;
        f->place.line = 0;
        f->place.exact = false;
    }
    return true;
}

int Session::backtrace(ttr::Vector<Frame>* frames, int max)
{
    Regs r;
    if (!alive_ || !target_->get_regs(&r))
        return 0;
    const Arch* arch = target_->arch();
    Frame f;
    f.regs = r;
    describe(r.pc, false, &f);
    frames->push_back(f);
    while (frames->size() < max) {
        const Frame& cur = (*frames)[frames->size() - 1];
        if (!exe_->contains(cur.pc - (frames->size() > 1 ? 1 : 0)))
            break;
        Regs up;
        if (!exe_->unwind(target_, cur.regs, frames->size() > 1, &up))
            break;
        /* The stack grows down: a caller's frame is higher. */
        if (up.valid[arch->sp_reg] && cur.regs.valid[arch->sp_reg] &&
            up.r[arch->sp_reg] <= cur.regs.r[arch->sp_reg] && frames->size() > 1)
            break;
        Frame g;
        g.regs = up;
        describe(up.pc, true, &g);
        frames->push_back(g);
    }
    return frames->size();
}

ttr::Str format_place(const Frame& f)
{
    char b[1400], h[24];
    if (f.func && f.has_place && f.place.file)
        snprintf(b, sizeof(b), "%s (%s:%d)", f.func, base_name(f.place.file), f.place.line);
    else if (f.func)
        snprintf(b, sizeof(b), "%s+%s", f.func, hex(f.pc - f.func_start, h));
    else
        snprintf(b, sizeof(b), "??");
    return ttr::Str(b);
}

} /* namespace td */
