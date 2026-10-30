/*
 * td/watch.cpp - the Session's watchpoints (session.h): VC6's data
 * breakpoints. The Target watches the bytes (debug registers, /proc
 * watched areas) and reports a write; the program stops only if the
 * value changed, as gdb's watch does.
 */
#include "session.h"

#include <string.h>

namespace td {

int Session::add_watch(const Frame& f, const char* expr, ttr::Str* err)
{
    if (!alive_ || core_) {
        if (err)
            *err = core_ ? "a core file cannot run" : "the program is not running";
        return 0;
    }
    Value v = evaluate(f, expr);
    bool local = used_local_;
    if (!v.ok()) {
        if (err)
            *err = v.error;
        return 0;
    }
    if (v.where != VW_MEMORY) {
        if (err)
            *err = v.where == VW_REGISTER ? "it is in a register: only memory can be watched"
                                          : "it is not in memory: only memory can be watched";
        return 0;
    }
    Watchpoint w;
    w.id = 0;
    w.expr = expr;
    w.value = v;
    w.addr = v.addr;
    w.size = (size_t)type_size(v.type);
    if (v.bit_size) {
        w.addr = v.addr + v.bit_offset / 8;
        w.size = (size_t)((v.bit_offset % 8 + (uint64_t)v.bit_size + 7) / 8);
    }
    w.handle = -1;
    w.hits = 0;
    w.scope_sp = 0;
    w.scope_pc = 0;
    w.scope_thread = target_->current_thread();
    if (!w.size) {
        if (err)
            *err = "it has no size";
        return 0;
    }
    if (w.size > WATCH_MAX) {
        if (err)
            *err = "too big: 256 bytes at most";
        return 0;
    }
    if (!target_->read(w.addr, w.last, w.size)) {
        if (err)
            *err = "its memory cannot be read";
        return 0;
    }
    memcpy(w.before, w.last, w.size);
    if (local) {
        /* Its frame's caller: where it returns to, and its stack pointer. */
        ttr::Vector<Frame> frames;
        int n = backtrace(&frames, 256), i;
        int sp = target_->arch()->sp_reg;
        for (i = 0; i + 1 < n; i++)
            if (frames[i].pc == f.pc && frames[i].regs.r[sp] == f.regs.r[sp])
                break;
        if (i + 1 >= n || !frames[i + 1].regs.valid[sp]) {
            if (err)
                *err = "its frame's caller is not known, so neither is when the frame ends";
            return 0;
        }
        w.scope_pc = frames[i + 1].pc;
        w.scope_sp = frames[i + 1].regs.r[sp];
    }
    w.handle = target_->add_watch(w.addr, w.size, err);
    if (w.handle < 0)
        return 0;
    w.id = next_id_++;
    if (w.scope_pc)
        add_location(-3, "", w.scope_pc);
    watches_.push_back(w);
    return w.id;
}

/* Remove watches_[i], and its frame's breakpoint. */
void Session::end_watch(int i)
{
    Watchpoint& w = watches_[i];
    if (alive_ && w.handle >= 0)
        target_->remove_watch(w.handle);
    if (w.scope_pc) {
        int k;
        for (k = bps_.size() - 1; k >= 0; k--)
            if (bps_[k].id == -3 && bps_[k].addr == w.scope_pc && !bps_[k].inserted) {
                bps_.remove_at(k);
                break;
            }
    }
    watches_.remove_at(i);
}

/* A bit field's bits in two copies of its bytes; anything else, all of
 * its bytes. */
bool Session::same_bits(const Watchpoint& w, const unsigned char* a, const unsigned char* b)
{
    if (!w.value.bit_size || w.size > sizeof(w.value.bytes))
        return memcmp(a, b, w.size) == 0;
    Value x = w.value, y = w.value;
    x.where = y.where = VW_CONSTANT;
    x.bit_offset = y.bit_offset = w.value.bit_offset % 8;
    memcpy(x.bytes, a, w.size);
    memcpy(y.bytes, b, w.size);
    uint64_t p = 0, q = 0;
    if (!scalar(x, &p) || !scalar(y, &q))
        return memcmp(a, b, w.size) == 0;
    return p == q;
}

/* After a watched write: the first watchpoint whose value changed (all
 * of them take their new values), 0 if none did. */
int Session::changed_watch()
{
    int i, hit = 0;
    for (i = 0; i < watches_.size(); i++) {
        Watchpoint& w = watches_[i];
        unsigned char now[WATCH_MAX];
        if (!target_->read(w.addr, now, w.size) || same_bits(w, w.last, now))
            continue;
        memcpy(w.before, w.last, w.size);
        memcpy(w.last, now, w.size);
        w.hits++;
        if (!hit)
            hit = w.id;
    }
    return hit;
}

/* Resume and wait. A watched write that changed nothing is no stop: on
 * it goes, and a step's ends as the step. False: the target failed. */
bool Session::run_target(bool step, int sig, Event* e)
{
    hit_watch_ = 0;
    for (;;) {
        if (!target_->resume(step, sig) || !wait_event(e)) {
            e->kind = EV_ERROR;
            return false;
        }
        if (e->kind != EV_WATCH)
            return true;
        hit_watch_ = changed_watch();
        if (hit_watch_)
            return true;
        if (step) {
            e->kind = EV_STEP;
            return true;
        }
        sig = 0;
    }
}

bool Session::watch_values(int id, Value* before, Value* now)
{
    int i;
    for (i = 0; i < watches_.size(); i++) {
        const Watchpoint& w = watches_[i];
        if (w.id != id)
            continue;
        *now = w.value;
        *before = w.value;
        if (w.size > sizeof(before->bytes)) {
            before->where = VW_NONE;
            before->error = "too big to keep";
            return true;
        }
        before->where = VW_CONSTANT;
        before->bit_offset = w.value.bit_offset % 8;
        memset(before->bytes, 0, sizeof(before->bytes));
        memcpy(before->bytes, w.before, w.size);
        return true;
    }
    return false;
}

} /* namespace td */
