/*
 * tf/ops.h - copy, move and delete as jobs over file systems
 * (ttrc/DESIGN.md section 5.4).
 *
 * A Job works in steps: step() does some work and returns, so the caller
 * can show progress, read its other inputs and let the user cancel.
 * Questions (overwrite? an error: retry or skip?) come back as states;
 * the caller asks the user and calls answer(). Copies keep modes and
 * times, and owners where permitted; symbolic links are copied as links;
 * a move is a rename where it can be, else a copy and then a delete of
 * the source, which happens only if its copy went through without errors.
 */
#ifndef TF_OPS_H
#define TF_OPS_H

#include "tf.h"

namespace tf {

enum OpKind {
    OP_COPY,
    OP_MOVE,
    OP_DELETE
};

enum Answer {
    ANS_YES,            /* overwrite this one */
    ANS_NO,             /* skip this one */
    ANS_ALL,            /* overwrite all */
    ANS_NONE,           /* skip all that exist */
    ANS_UPDATE,         /* overwrite all that are older */
    ANS_RETRY,
    ANS_SKIP,
    ANS_SKIP_ALL,       /* skip all errors */
    ANS_ABORT
};

class Job {
public:
    enum State {
        WORKING,
        ASK_OVERWRITE,  /* source() would replace target() */
        ASK_ERROR,      /* error() on what() of source() or target() */
        DONE,
        ABORTED
    };

    Job(OpKind kind, Fs* src, Fs* dst);
    ~Job();

    /* What to work on: absolute paths in the source file system. */
    void add(const char* path);
    /* Copy and move: the target. With into_dir, every source goes into
     * that directory under its own name; otherwise the one source gets
     * this path as its new name. */
    void set_target(const char* path, bool into_dir);
    bool preserve_attrs;
    /* Overwrite without asking. */
    void set_overwrite_all(bool on) { overwrite_all_ = on; }

    State step(int budget_ms);
    State state() const { return state_; }
    void answer(Answer a);

    const char* source() const { return cur_src_; }
    const char* target() const { return cur_dst_; }
    int error() const { return err_; }
    const char* what() const { return what_; }
    /* The target's and source's sizes and times, for an overwrite
     * question. */
    const Stat& source_stat() const { return src_st_; }
    const Stat& target_stat() const { return dst_st_; }

    /* Progress */
    bool counting() const { return counting_; }
    uint64_t bytes_done() const { return bytes_done_; }
    uint64_t bytes_total() const { return bytes_total_; }
    int files_done() const { return files_done_; }
    int files_total() const { return files_total_; }
    int failures() const { return failures_; }

private:
    struct Item {
        char* src;
        char* dst;
        int phase;
        int errors_before;  /* move: delete the source only if unchanged */
    };

    void push(const char* src, const char* dst, int phase, int errors_before = 0);
    void pop();
    Item* top();
    bool count_step();
    bool work_step();
    void fail(int err, const char* what);
    void close_streams(bool remove_partial);
    bool start_item(Item& it);
    bool copy_file(Item& it);
    bool open_target(Item& it);

    OpKind kind_;
    Fs* src_fs_;
    Fs* dst_fs_;
    char** sources_;
    int nsources_, next_source_;
    char* target_;
    bool into_dir_;

    Item* stack_;
    int depth_, cap_;
    State state_;
    bool counting_;
    char** count_stack_;
    int count_depth_, count_cap_;

    Stream* in_;
    Stream* out_;
    char* buf_;
    bool overwrite_all_, skip_existing_, update_all_, skip_errors_;
    bool answered_overwrite_;

    const char* cur_src_;
    const char* cur_dst_;
    int err_;
    const char* what_;
    Stat src_st_, dst_st_;

    uint64_t bytes_done_, bytes_total_;
    int files_done_, files_total_;
    int failures_;

    Job(const Job&);
    Job& operator=(const Job&);
};

/* Does name match a shell pattern (* ? [a-z] [!x])? */
bool match_pattern(const char* pattern, const char* name, bool ignore_case);

} /* namespace tf */

#endif
