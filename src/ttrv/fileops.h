/*
 * ttrv's file operations' dialogs (ttrv/DESIGN.md section 2.4): the
 * questions they ask and the progress of a copy, move or delete. Each
 * is modal and returns its answer; a tf::Job runs in steps on the main
 * thread between events, so the window keeps drawing and Abort works.
 */
#ifndef TTRV_FILEOPS_H
#define TTRV_FILEOPS_H

#include "../../tf/ops.h"
#include "core/str.h"

#include <Xm/Xm.h>

namespace ttrv {

class Ops {
public:
    explicit Ops(Widget top);

    /* A name: 'inout' is the start (the part before its extension
     * selected, as Explorer does) and the answer. False: cancelled. */
    bool ask_name(const char* title, const char* label, ttr::Str* inout);
    /* Yes or No; 'yes' is the first button's label. */
    bool confirm(const char* title, const char* message, const char* yes);
    /* One of 'buttons' (0-terminated, the first the default): its index,
     * or -1 if the dialog was closed. */
    int choose(const char* title, const char* message, const char* const* buttons);
    void error(const char* title, const char* message);
    /* Runs the job, with a progress dialog if it takes long enough to
     * see, asking what it asks. True if it finished (files skipped on
     * errors or kept from being overwritten included). */
    bool run(tf::Job& job, const char* title);

    /* Callbacks (public for the C trampolines). */
    void answered(int a) { answer_ = a; }

private:
    Ops(const Ops&);
    Ops& operator=(const Ops&);

    int wait(Widget dialog);
    void progress(Widget* d, tf::Job& job, const char* title);

    Widget top_;
    int answer_;
    Widget p_what_, p_from_, p_to_, p_bar_, p_count_;
};

} /* namespace ttrv */

#endif
