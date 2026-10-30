/*
 * td/session.h - a debugging session (td/DESIGN.md section 7): the
 * Target, the executable's Module, breakpoints, stepping by line and by
 * instruction, and the call stack. Both user interfaces (ttrdb, later
 * ttrcode) talk only to this.
 *
 * The prototype: one thread, the executable's own symbols (no shared
 * libraries yet), and calls that block until the program stops.
 */
#ifndef TD_SESSION_H
#define TD_SESSION_H

#include "module.h"
#include "target.h"
#include "td.h"

#include "core/str.h"
#include "core/vector.h"

namespace td {

struct Breakpoint {
    int id;             /* 0: internal (temporary) */
    Addr addr;
    unsigned char saved[4];
    bool inserted;
    ttr::Str where;     /* as the user gave it */
};

struct Frame {
    Regs regs;
    Addr pc;
    const char* func;   /* 0 if unknown */
    Addr func_start;
    Place place;        /* place.file 0 if unknown */
    bool has_place;
};

/* Why the program stopped, for the user interface. */
struct Stop {
    EventKind kind;
    int bp;             /* the user breakpoint's id, or 0 */
    int sig;
    int status;
    Addr pc;
};

class Session {
public:
    Session();
    ~Session();

    /* Start the program (argv[0] its path) and stop at its first
     * instruction; or attach to a process. */
    bool start(char* const* argv, ttr::Str* err);
    bool attach(int pid, ttr::Str* err);
    bool running() const { return alive_; }
    const Module* module() const { return exe_; }
    Target* target() { return target_; }

    /* Breakpoints: "file.c:12", "function", "*0x401000". Returns its id,
     * or 0 with the reason in err. */
    int add_breakpoint(const char* spec, ttr::Str* err);
    bool remove_breakpoint(int id);
    const ttr::Vector<Breakpoint>& breakpoints() const { return bps_; }

    /* Each runs until the program stops again (or ends). */
    Stop cont();
    Stop step_instruction();
    Stop step_line(bool over);
    Stop finish();
    void kill();

    /* The call stack, innermost first. */
    int backtrace(ttr::Vector<Frame>* frames, int max);
    bool describe(Addr pc, bool caller, Frame* f);
    bool regs(Regs* r);

private:
    bool load_exe(ttr::Str* err);
    void insert_all();
    void remove_all();
    int bp_at(Addr a) const;
    Stop run_until_stop(bool step);
    Stop step_over_bp(bool* done);
    Stop to_stop(const Event& e);
    Addr resolve(const char* spec, ttr::Str* err, ttr::Vector<Addr>* more);
    Stop run_to(Addr a, Addr min_sp);

    Target* target_;
    Module* exe_;
    bool alive_;
    int next_id_;
    int pending_sig_;       /* a signal it stopped with, for the next run */
    ttr::Vector<Breakpoint> bps_;
};

/* "file.c:12 in main" style, for messages. */
ttr::Str format_place(const Frame& f);

} /* namespace td */

#endif
