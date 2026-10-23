/*
 * ttrc's viewer (F3), full-screen on tu (ttrc/DESIGN.md section 3.5).
 * The editor (F4) is ttre, a program of its own. Both read through tf, so files inside archives open
 * without being extracted first. The host (the App) runs the loop: it
 * draws them, feeds them events, and answers their requests for a
 * dialog (search, go to line, save as, quit without saving?).
 */
#ifndef TTRC_VIEWER_H
#define TTRC_VIEWER_H

#include "theme.h"

#include "../../tf/tf.h"
#include "../../tu/input.h"
#include "../../tu/screen.h"
#include "core/str.h"
#include "core/vector.h"

namespace ttrc {

/* What a full-screen tool wants from its host. */
enum ToolRequest {
    REQ_NONE,
    REQ_CLOSE,
    REQ_SEARCH,         /* ask for text, call search() */
    REQ_GOTO            /* ask for a line number, call goto_line() */
};

class FullScreen {
public:
    virtual ~FullScreen() {}
    virtual void draw(tu::Screen& s, const Theme& t) = 0;
    /* Returns what the host should do next. */
    virtual ToolRequest handle(const tu::Event& ev) = 0;
    virtual void search(const char* text) = 0;
    virtual void goto_line(long line) = 0;
    /* A note for the status line until the next key. */
    const char* note() const { return note_; }
    void set_note(const char* n);

protected:
    FullScreen() { note_[0] = 0; }
    char note_[256];
};

class Viewer : public FullScreen {
public:
    Viewer();
    ~Viewer();
    /* Read the file (up to 64 MB). 0 or an errno value. */
    int load(tf::Fs* fs, const char* path);
    void draw(tu::Screen& s, const Theme& t);
    ToolRequest handle(const tu::Event& ev);
    void search(const char* text);
    void goto_line(long line);

private:
    size_t line_start(long i) const;
    size_t line_end(long i) const;      /* before the newline */
    /* The byte offsets where each screen row of line i starts. */
    int rows_of(long i, int width, size_t* starts, int max) const;
    void scroll(int rows);
    void top_to(size_t off);
    int width() const { return width_; }
    long line_of(size_t off) const;

    char* data_;
    size_t len_;
    bool truncated_;
    ttr::Vector<size_t> lines_;     /* start offsets */
    char name_[512];
    bool hex_, wrap_;
    long top_line_;
    int top_sub_;               /* screen row within the top line */
    int left_;                  /* first column when not wrapping */
    long hex_top_;              /* first hex row */
    int rows_, width_, bpr_;    /* from the last draw */
    size_t hit_, hit_len_;
    char last_search_[256];
};

} /* namespace ttrc */

#endif
