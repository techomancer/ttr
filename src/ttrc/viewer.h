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
    REQ_SEARCH,         /* ask for a SearchQuery, call search() */
    REQ_GOTO            /* ask for where (goto_label()), call go_to() */
};

/* What the search dialog asks for. Hex text is bytes: "de ad be ef",
 * "deadbeef" or "0xde 0xad". */
struct SearchQuery {
    ttr::Str text;
    bool hex;
    bool match_case;    /* text only */
    SearchQuery() : hex(false), match_case(false) {}
};

class FullScreen {
public:
    virtual ~FullScreen() {}
    virtual void draw(tu::Screen& s, const Theme& t) = 0;
    /* Returns what the host should do next. */
    virtual ToolRequest handle(const tu::Event& ev) = 0;
    /* What the search dialog starts with. */
    virtual void search_defaults(SearchQuery* q) const = 0;
    virtual void search(const SearchQuery& q) = 0;
    /* The go-to dialog's title and label, and its answer. */
    virtual const char* goto_title() const = 0;
    virtual const char* goto_label() const = 0;
    virtual void go_to(const char* where) = 0;
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
    void search_defaults(SearchQuery* q) const;
    void search(const SearchQuery& q);
    /* Repeat the last search ('n', Shift+F7). */
    void search_again();
    const char* goto_title() const;
    const char* goto_label() const;
    /* A line number in text; a file offset in hex (0x1f40, 1f40h, or
     * decimal 8000; a-f digits mean hex). */
    void go_to(const char* where);

private:
    size_t line_start(long i) const;
    size_t line_end(long i) const;      /* before the newline */
    /* The byte offsets where each screen row of line i starts. */
    int rows_of(long i, int width, size_t* starts, int max) const;
    void scroll(int rows);
    /* Show off with `above` rows above it. */
    void top_to(size_t off, int above = 2);
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
    SearchQuery last_;
    char pat_[256];             /* the bytes last_ looks for */
    size_t pat_len_;
};

} /* namespace ttrc */

#endif
