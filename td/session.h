/*
 * td/session.h - a debugging session (td/DESIGN.md section 7): the
 * Target, the loaded modules (the executable and its shared objects),
 * breakpoints, stepping by line and by instruction, and the call stack.
 * Both user interfaces (ttrdb, later ttrcode) talk only to this.
 *
 * The prototype: one thread, and calls that return when the program
 * stops. While it runs they call the idle function, if one is set, so a
 * user interface can go on handling its events and call interrupt(). Shared objects are read when the program reaches its entry point
 * (the dynamic linker has loaded those it starts with) and again at every
 * stop after that.
 *
 * A core file is a session too (open_core): the same stack and values,
 * of a program that does not run.
 */
#ifndef TD_SESSION_H
#define TD_SESSION_H

#include "module.h"
#include "target.h"
#include "td.h"
#include "value.h"

#include "core/str.h"
#include "core/vector.h"

namespace td {

class CoreTarget;

struct Breakpoint {
    int id;             /* > 0: the user's; 0: a temporary one; -1: the
                         * entry point; -2: the dynamic linker's hook;
                         * -3: where a watched local's frame returns */
    Addr addr;          /* 0: pending, waiting for its module to load */
    unsigned char saved[4];
    bool inserted;
    ttr::Str where;     /* as the user gave it */
};

/* A watched value (data breakpoint): the program stops when it changes.
 * Its ids are the breakpoints'. */
enum { WATCH_MAX = 256 };

struct Watchpoint {
    int id;
    ttr::Str expr;          /* as the user gave it */
    Value value;            /* where it is and its type */
    Addr addr;              /* the bytes watched */
    size_t size;
    unsigned char last[WATCH_MAX];     /* at the last change, or when set */
    unsigned char before[WATCH_MAX];   /* before the last change */
    int handle;             /* the Target's */
    int hits;
    /* A local's: its frame ends when the stack pointer is back up to its
     * caller's (scope_sp), at scope_pc. 0: a global, for good. */
    Addr scope_sp, scope_pc;
};

struct Frame {
    Regs regs;
    Addr pc;
    const char* func;   /* 0 if unknown */
    Addr func_start;
    const Module* module;
    Place place;        /* place.file 0 if unknown */
    bool has_place;
    bool caller;        /* pc is a return address: the call is just before it */
};

/* Why the program stopped, for the user interface. */
struct Stop {
    EventKind kind;
    int bp;             /* the user breakpoint's id, or 0 */
    int watch;          /* EV_WATCH, EV_WATCH_ENDED: the watchpoint's id */
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
    bool start(char* const* argv, ttr::Str* err,
               const LaunchOptions& o = LaunchOptions());
    bool attach(int pid, ttr::Str* err);
    /* A core file, of the program at exe (0: the one the core names,
     * Linux only). *why: the signal it died of, and where. */
    bool open_core(const char* core, const char* exe, ttr::Str* err, Stop* why);
    /* A program, or a core file: there is a stack to look at. */
    bool running() const { return alive_; }
    bool is_core() const { return core_ != 0; }
    CoreTarget* core() { return core_; }
    /* Called over and over while the program runs (it should wait a
     * little, say for its own events). */
    typedef void (*IdleFn)(void* ctx);
    void set_idle(IdleFn fn, void* ctx);
    /* True while one of the calls below is waiting for the program. */
    bool busy() const { return busy_; }
    /* From the idle function: stop the program; the call returns with
     * EV_PAUSED. */
    void interrupt();
    Target* target() { return target_; }

    /* The executable first, then the shared objects in load order. */
    int module_count() const { return mods_.size(); }
    const Module* module(int i) const { return mods_[i]; }
    const Module* module_at(Addr pc) const;
    /* Read the dynamic linker's list again; returns how many are new. */
    int refresh_modules();

    /* Breakpoints: "file.c:12", "function", "*0x401000". Returns its id,
     * or 0 with the reason in err. One that matches nothing before the
     * shared objects are loaded waits for them (*pending set). */
    int add_breakpoint(const char* spec, ttr::Str* err, bool* pending = 0);
    bool remove_breakpoint(int id);     /* or a watchpoint */
    const ttr::Vector<Breakpoint>& breakpoints() const { return bps_; }

    /* Stop when an expression's value (in memory) changes: VC6's data
     * breakpoints. One that uses a frame's locals ends with that frame
     * (EV_WATCH_ENDED). Returns its id, or 0 with the reason in err. */
    int add_watch(const Frame& f, const char* expr, ttr::Str* err);
    const ttr::Vector<Watchpoint>& watchpoints() const { return watches_; }
    /* A watchpoint's value before its last change, and now. */
    bool watch_values(int id, Value* before, Value* now);

    /* Each runs until the program stops again (or ends). */
    Stop cont();
    Stop step_instruction();
    Stop step_line(bool over);
    Stop finish();
    void kill();
    /* Let it go on without the debugger. */
    void detach();

    /* The call stack, innermost first. */
    int backtrace(ttr::Vector<Frame>* frames, int max);
    bool describe(Addr pc, bool caller, Frame* f);
    bool regs(Regs* r);

    /* Values (td/value.h). A frame's arguments and the local variables in
     * scope at its pc, arguments first: VC6's Locals. */
    int locals(const Frame& f, ttr::Vector<Value>* out);
    /* A C expression in a frame: variables, members, elements, pointers,
     * arithmetic, comparisons, casts, sizeof, registers ($sp or @sp).
     * "expr,x" shows it in hex, "p,10" what p points to as 10 elements. */
    Value evaluate(const Frame& f, const char* expr);
    /* One line: 42, 97 'a', 0x0804a000 "text", {x=1 y=2}, {1, 2, 3}. */
    ttr::Str format(const Value& v);
    /* Members, elements, what a pointer points to: at most max. */
    bool has_children(const Value& v);
    int children(const Value& v, ttr::Vector<Value>* out, int max);
    /* Its bytes (n at most its size). */
    bool value_bytes(const Value& v, void* buf, size_t n);

private:
    bool load_exe(ttr::Str* err);
    void insert_all();
    void remove_all();
    int bp_at(Addr a) const;
    void add_location(int id, const char* spec, Addr a);
    Stop run_until_stop(bool step);
    bool run_target(bool step, int sig, Event* e);
    int changed_watch();
    bool same_bits(const Watchpoint& w, const unsigned char* a, const unsigned char* b);
    void end_watch(int i);
    Stop not_running();
    Stop step_over_bp(bool* done);
    Stop to_stop(const Event& e);
    bool resolve(const char* spec, ttr::Str* err, ttr::Vector<Addr>* out);
    void resolve_pending();
    void set_loader_hook();
    Stop run_to(Addr a, Addr min_sp);
    Stop cont_once();
    bool wait_event(Event* e);

    const char* func_at(Addr pc, Addr* start, Addr* end) const;

    friend class Evaluator;
    /* value.cpp */
    bool locate(const Frame& f, const Module* m, const Function* fn, const Location& loc,
                Value* v, int depth);
    Value variable_value(const Frame& f, const Module* m, const Function* fn,
                         const Variable& var);
    bool lookup(const Frame& f, const char* name, Value* out);
    bool scalar(const Value& v, uint64_t* bits);
    bool as_double(const Value& v, double* d);
    Type* synth(TypeKind kind, const char* name, uint64_t size, bool is_signed, bool is_char);
    Type* pointer_to(const Type* t);
    Type* array_of(const Type* t, long n);
    Type* find_type(const Frame& f, const char* name);
    void format_into(Text* out, const Value& v, int depth);
    Value member_value(const Value& v, const Member& m, uint64_t base);
    Value deref(const Value& p);

    bool place_at(Addr pc, Place* p) const;

    Target* target_;
    ttr::Vector<Module*> mods_;
    bool alive_;
    bool started_;          /* past the entry point: shared objects read */
    int next_id_;
    int pending_sig_;       /* a signal it stopped with, for the next run */
    IdleFn idle_;
    void* idle_ctx_;
    bool busy_;
    bool interrupted_;      /* interrupt() asked: its stop is a pause */
    ttr::Vector<Breakpoint> bps_;
    ttr::Vector<Type*> synth_;     /* types expressions make: int, pointers to */
    ttr::Vector<Watchpoint> watches_;
    int hit_watch_;         /* the watchpoint whose value changed: its id */
    bool used_local_;       /* the last evaluate() used a frame's variable */
    CoreTarget* core_;      /* target_, when it is a core file */
};

/* "main (demo.c:12)" style, for messages. */
ttr::Str format_place(const Frame& f);

} /* namespace td */

#endif
