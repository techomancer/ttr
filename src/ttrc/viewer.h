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
#include "../../ti/term.h"
#include "../../ti/ti.h"
#include "../../tu/input.h"
#include "../../tu/screen.h"
#include "../../tu/term.h"
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
    /* Graphics the tool sends straight to the terminal after the screen
     * is flushed (images through kitty, sixel or iTerm2); 'all': the
     * screen was redrawn or a dialog covered it, so send them again. */
    virtual void paint_over(tu::Term&, bool all) { (void)all; }
    /* Take them away: before a dialog, and when the tool closes. */
    virtual void hide_over(tu::Term&) {}
    /* A note for the status line until the next key. */
    const char* note() const { return note_; }
    void set_note(const char* n);
    /* The tool asks for the whole screen to be redrawn (to clear what
     * paint_over left there). */
    bool take_redraw()
    {
        bool r = redraw_;
        redraw_ = false;
        return r;
    }

protected:
    FullScreen() : redraw_(false) { note_[0] = 0; }
    char note_[256];
    bool redraw_;
};

class Viewer : public FullScreen {
public:
    Viewer();
    ~Viewer();
    /* How to show images (PROTO_OFF: as bytes) on this terminal; before
     * load(). */
    void set_images(ti::Protocol p, const tu::Term* term);
    /* Read the file (up to 64 MB). 0 or an errno value. An image is
     * decoded and shown as one; F4 shows its bytes. */
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
    void paint_over(tu::Term& term, bool all);
    void hide_over(tu::Term& term);

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
    void draw_image(tu::Screen& s, const Theme& t);
    /* Leave the image for the bytes (search, go to). */
    void image_to_hex();

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

    /* Images */
    ti::Protocol proto_;
    const tu::Term* term_;
    ti::Image img_;             /* decoded; empty if not an image */
    ti::Info img_info_;
    bool image_;                /* showing it */
    ti::Image shown_;           /* scaled for the last layout */
    int img_x_, img_y_, img_cols_, img_rows_;   /* its cells */
    unsigned long img_bg_;
    bool over_dirty_, over_shown_;
};

} /* namespace ttrc */

#endif
