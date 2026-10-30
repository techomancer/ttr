#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace td {

namespace {

/* Single steps before giving up on reaching another line. */
const long k_max_steps = 2000000;
/* Single steps through code without lines (a PLT entry, IRIX's stubs) to
 * find the function it leads to before running past it instead. */
const int k_stub_steps = 200;

const char* base_name(const char* p)
{
    const char* s = strrchr(p, '/');
    return s ? s + 1 : p;
}

} /* namespace */

Session::Session()
    : target_(0), alive_(false), started_(false), next_id_(1), pending_sig_(0)
{
}

Session::~Session()
{
    delete target_;
    int i;
    for (i = 0; i < mods_.size(); i++)
        delete mods_[i];
}

bool Session::load_exe(ttr::Str* err)
{
    ttr::Str path = target_->exe_path();
    Module* exe = new Module;
    mods_.push_back(exe);
    if (!exe->load(path.c_str(), err))
        return false;
    if (exe->elf().arch() != target_->arch()) {
        if (err)
            *err = "the program is not for this processor";
        return false;
    }
    if (exe->elf().position_independent())
        exe->set_bias(target_->exe_bias(exe->elf().entry()));
    return true;
}

bool Session::start(char* const* argv, ttr::Str* err)
{
    target_ = Target::create();
    if (!target_->launch(argv, err))
        return false;
    alive_ = true;
    if (!load_exe(err))
        return false;
    /* At the entry point the dynamic linker has loaded the shared objects
     * the program starts with: read them there. */
    Breakpoint b;
    b.id = -1;
    b.addr = mods_[0]->elf().entry() + mods_[0]->bias();
    b.inserted = false;
    memset(b.saved, 0, sizeof(b.saved));
    bps_.push_back(b);
    return true;
}

bool Session::attach(int pid, ttr::Str* err)
{
    target_ = Target::create();
    if (!target_->attach(pid, err))
        return false;
    alive_ = true;
    if (!load_exe(err))
        return false;
    started_ = true;
    refresh_modules();
    return true;
}

/* ------------------------------------------------------------------ */
/* Modules                                                             */

int Session::refresh_modules()
{
    if (!alive_ || mods_.empty() || !mods_[0]->elf().dynamic())
        return 0;
    ttr::Vector<SharedObject> list;
    target_->shared_objects(mods_[0]->elf().dynamic() + mods_[0]->bias(), &list);
    int added = 0;
    int i, k;
    for (i = 0; i < list.size(); i++) {
        for (k = 1; k < mods_.size(); k++)
            if (mods_[k]->bias() == list[i].bias && !strcmp(mods_[k]->path(), list[i].path.c_str()))
                break;
        if (k < mods_.size())
            continue;
        Module* m = new Module;
        ttr::Str err;
        if (!m->load(list[i].path.c_str(), &err)) {
            delete m;
            continue;
        }
        m->set_bias(list[i].bias);
        mods_.push_back(m);
        added++;
    }
    return added;
}

const Module* Session::module_at(Addr pc) const
{
    int i;
    for (i = 0; i < mods_.size(); i++)
        if (mods_[i]->contains(pc))
            return mods_[i];
    return 0;
}

const char* Session::func_at(Addr pc, Addr* start, Addr* end) const
{
    const Module* m = module_at(pc);
    return m ? m->function_at(pc, start, end) : 0;
}

bool Session::place_at(Addr pc, Place* p) const
{
    const Module* m = module_at(pc);
    return m && m->place_of(pc, p);
}

/* ------------------------------------------------------------------ */
/* Breakpoints                                                         */

bool Session::resolve(const char* spec, ttr::Str* err, ttr::Vector<Addr>* out)
{
    if (spec[0] == '*') {
        out->push_back(parse_number(spec + 1, 0));
        return true;
    }
    int i;
    const char* colon = strrchr(spec, ':');
    if (colon && colon[1] >= '0' && colon[1] <= '9') {
        char file[1024];
        size_t n = (size_t)(colon - spec);
        if (n >= sizeof(file))
            n = sizeof(file) - 1;
        memcpy(file, spec, n);
        file[n] = 0;
        for (i = 0; i < mods_.size(); i++)
            mods_[i]->line_addresses(file, atoi(colon + 1), out);
        if (out->empty() && err)
            *err = "no code at that line";
        return !out->empty();
    }
    /* A function: the executable's first, then the shared objects'. */
    for (i = 0; i < mods_.size(); i++) {
        Addr start, end;
        if (mods_[i]->function_named(spec, &start, &end)) {
            out->push_back(mods_[i]->has_lines() ? mods_[i]->after_prologue(start, end) : start);
            return true;
        }
    }
    if (err)
        *err = "no function by that name";
    return false;
}

void Session::add_location(int id, const char* spec, Addr a)
{
    Breakpoint b;
    b.id = id;
    b.addr = a;
    b.inserted = false;
    b.where = spec;
    memset(b.saved, 0, sizeof(b.saved));
    bps_.push_back(b);
}

int Session::add_breakpoint(const char* spec, ttr::Str* err, bool* pending)
{
    if (pending)
        *pending = false;
    if (mods_.empty()) {
        if (err)
            *err = "no program";
        return 0;
    }
    ttr::Vector<Addr> addrs;
    if (!resolve(spec, err, &addrs)) {
        /* Before the shared objects are read, it may be in one of them. */
        if (started_ || spec[0] == '*')
            return 0;
        int id = next_id_++;
        add_location(id, spec, 0);
        if (pending)
            *pending = true;
        return id;
    }
    int id = next_id_++;
    int i;
    for (i = 0; i < addrs.size(); i++)
        add_location(id, spec, addrs[i]);
    return id;
}

void Session::resolve_pending()
{
    int i;
    for (i = 0; i < bps_.size(); i++) {
        if (bps_[i].addr || bps_[i].id <= 0)
            continue;
        ttr::Vector<Addr> addrs;
        if (!resolve(bps_[i].where.c_str(), 0, &addrs))
            continue;
        int id = bps_[i].id;
        ttr::Str spec = bps_[i].where;
        bps_.remove_at(i);
        int k;
        for (k = 0; k < addrs.size(); k++)
            add_location(id, spec.c_str(), addrs[k]);
        i = -1;     /* the vector changed: from the start */
    }
}

bool Session::remove_breakpoint(int id)
{
    bool found = false;
    int i;
    if (id <= 0)
        return false;
    for (i = bps_.size() - 1; i >= 0; i--)
        if (bps_[i].id == id) {
            bps_.remove_at(i);
            found = true;
        }
    return found;
}

/* The breakpoint at a: a user's before an internal one. */
int Session::bp_at(Addr a) const
{
    int i, best = -1;
    for (i = 0; i < bps_.size(); i++)
        if (bps_[i].addr == a && a && (best < 0 || bps_[i].id > bps_[best].id))
            best = i;
    return best;
}

/* Breakpoints are in memory only while the program runs, so reading its
 * code never shows them. Several at one address share one instruction. */
void Session::insert_all()
{
    const Arch* arch = target_->arch();
    int i, j;
    for (i = 0; i < bps_.size(); i++) {
        Breakpoint& b = bps_[i];
        if (b.inserted || !b.addr)
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

Stop Session::cont_once()
{
    return run_until_stop(false);
}

/* Continue; the stop at the entry point (reading the shared objects) is
 * passed through unless a user breakpoint is there too. */
Stop Session::cont()
{
    for (;;) {
        Stop s = cont_once();
        if (s.kind == EV_BREAK && s.bp == -1) {
            int i;
            for (i = bps_.size() - 1; i >= 0; i--)
                if (bps_[i].id == -1)
                    bps_.remove_at(i);
            started_ = true;
            refresh_modules();
            resolve_pending();
            continue;
        }
        if (alive_ && started_ && refresh_modules())
            resolve_pending();          /* dlopen since the last stop */
        return s;
    }
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
        add_location(0, "", a);
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
        if (!min_sp || r.r[arch->sp_reg] >= min_sp || s.bp > 0)
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
    if (!place_at(r.pc, &p0))
        return step_instruction();      /* no lines here: one instruction */
    Addr f0 = 0;
    func_at(r.pc, &f0, 0);

    Addr prev = r.pc;
    long n;
    for (n = 0; n < k_max_steps; n++) {
        /* Breakpoints are out of memory while it is stopped, so a single
         * step needs nothing special. */
        s = step_instruction();
        if (s.kind != EV_STEP || !target_->get_regs(&r))
            return s;
        Addr pc = r.pc;
        Addr fstart = 0, fend = 0;
        bool known = func_at(pc, &fstart, &fend) != 0;
        bool lines = place_at(pc, 0);
        /* At the first instruction of a function: a call. Step into it
         * when it has lines, else run until it returns. */
        if (known && pc == fstart && pc != prev) {
            if (!over && lines) {
                Addr body = module_at(pc)->after_prologue(pc, fend);
                if (body != pc)
                    return run_to(body, 0);
                s.kind = EV_STEP;
                s.pc = pc;
                return s;
            }
            Addr ra = 0;
            if (!arch->entry_return_address(r, target_, &ra))
                return run_to(pc, 0);
            s = run_to(ra, r.r[arch->sp_reg]);
            if (s.kind != EV_BREAK || s.bp || s.pc != ra)
                return s;
            target_->get_regs(&r);
            pc = r.pc;
        } else if (!lines) {
            /* Into code without lines that is not a function's start (a
             * PLT entry, a stub). Stepping into: a few instructions more,
             * in case it leads to a function with lines. Otherwise back to
             * where it was called from, or on until something stops it. */
            Addr ra = 0;
            bool have_ra = arch->entry_return_address(r, target_, &ra) && place_at(ra, 0);
            if (!over && have_ra) {
                Regs at = r;
                int k;
                for (k = 0; k < k_stub_steps; k++) {
                    s = step_instruction();
                    if (s.kind != EV_STEP || !target_->get_regs(&at))
                        return s;
                    if (at.pc == ra)
                        break;
                    Addr gs = 0, ge = 0;
                    if (func_at(at.pc, &gs, &ge) && at.pc == gs && place_at(at.pc, 0)) {
                        Addr body = module_at(at.pc)->after_prologue(at.pc, ge);
                        if (body != at.pc)
                            return run_to(body, 0);
                        s.kind = EV_STEP;
                        s.pc = at.pc;
                        return s;
                    }
                }
                if (at.pc == ra) {
                    prev = ra;
                    r = at;
                    continue;
                }
            }
            if (have_ra) {
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
        if (!place_at(pc, &p) || !p.exact)
            continue;
        Addr fs = 0;
        func_at(pc, &fs, 0);
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
    Addr look = pc - (caller ? 1 : 0);
    f->pc = pc;
    f->func_start = 0;
    f->module = module_at(look);
    f->func = f->module ? f->module->function_at(look, &f->func_start, 0) : 0;
    f->has_place = f->module && f->module->place_of(look, &f->place);
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
        if (!cur.module)
            break;
        Regs up;
        if (!cur.module->unwind(target_, cur.regs, frames->size() > 1, &up))
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
    else if (f.func && f.module && f.module != 0)
        snprintf(b, sizeof(b), "%s+%s (%s)", f.func, hex(f.pc - f.func_start, h),
                 base_name(f.module->path()));
    else if (f.func)
        snprintf(b, sizeof(b), "%s+%s", f.func, hex(f.pc - f.func_start, h));
    else if (f.module)
        snprintf(b, sizeof(b), "?? (%s)", base_name(f.module->path()));
    else
        snprintf(b, sizeof(b), "??");
    return ttr::Str(b);
}

} /* namespace td */
