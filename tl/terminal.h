/*
 * tl/terminal.h - the terminal: parser, interpreter and both screens.
 * See DESIGN.md sections 3 and 5.
 */
#ifndef TL_TERMINAL_H
#define TL_TERMINAL_H

#include "buffer.h"
#include "charset.h"
#include "grid.h"
#include "host.h"
#include "image.h"
#include "input.h"
#include "parser.h"
#include "style.h"
#include "unicode.h"
#include "view.h"

namespace tl {

/* Mode flags. DEC private modes and ANSI modes that tl implements. */
struct Modes {
    bool insert;            /* IRM (ANSI 4) */
    bool newline;           /* LNM (ANSI 20) */
    bool app_cursor;        /* DECCKM (1) */
    bool col132;            /* DECCOLM (3) */
    bool reverse;           /* DECSCNM (5) */
    bool origin;            /* DECOM (6) */
    bool autowrap;          /* DECAWM (7) */
    bool autorepeat;        /* DECARM (8) */
    bool cursor_blink;      /* (12) */
    bool cursor_visible;    /* DECTCEM (25) */
    bool allow_132;         /* (40) */
    bool reverse_wrap;      /* (45) */
    bool app_keypad;        /* DECNKM (66) / DECKPAM */
    bool focus_events;      /* (1004) */
    bool alt_scroll;        /* (1007) */
    bool bracketed_paste;   /* (2004) */
    bool sync_output;       /* (2026) */
    bool graphemes;         /* (2027): cluster by UAX #29 instead of wcwidth */
    int mouse;              /* 0, 9, 1000, 1002, 1003 */
    int mouse_encoding;     /* 0, 1005, 1006, 1015, 1016 */
    bool sixel_display;     /* DECSDM (80): sixels at the top left, no scrolling */
    bool sixel_right;       /* (8452): the cursor ends right of a sixel image */

    void reset();
};

struct Cursor {
    int x, y;
    bool pending_wrap;      /* at the right margin after printing there */
    Style style;
    uint16_t style_id;      /* style interned in the active screen */
    bool protect;           /* DECSCA */
    bool origin;
    CharsetState cs;
};

struct Screen {
    Grid grid;
    StyleTable styles;
    Cursor saved;           /* DECSC */
    bool has_saved;
};

class Terminal : private ParserSink {
public:
    Terminal();
    ~Terminal();

    /* Size in cells and lines of scrollback for the primary screen. */
    bool init(int cols, int rows, size_t scrollback);
    void set_host(Host* host) { host_ = host; }

    void feed(const uint8_t* data, size_t n);
    void feed(const char* s);
    /* Change the size. The primary screen's soft-wrapped lines are
     * re-wrapped to the new width unless reflow is off; the alternate
     * screen is cut or padded. */
    bool resize(int cols, int rows);
    void set_reflow(bool on) { reflow_ = on; }
    /* RIS: full reset. */
    void reset();
    /* Drop the scrollback of the primary screen. */
    void clear_scrollback();

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    const Grid& grid() const { return screen().grid; }
    Grid& grid() { return screen().grid; }
    const Style& style(const Cell& c) const { return screen().styles.get(c.style); }
    int cell_text(const Cell& c, uint32_t* out, int max) const;
    bool alt_screen() const { return alt_; }
    const Cursor& cursor() const { return cur_; }
    const Modes& modes() const { return modes_; }
    CursorStyle cursor_shape() const { return cursor_shape_; }
    const char* title() const { return title_.c_str(); }
    int scroll_top() const { return top_; }
    int scroll_bottom() const { return bot_; }
    bool tab_stop(int x) const { return x >= 0 && x < cols_ && tabs_[x]; }
    void set_ambiguous_wide(bool on) { ambiguous_wide_ = on; }
    /* A cell's size in pixels, for placing images and answering size
     * queries; 0: unknown (images then assume 10 x 20). */
    void set_cell_pixels(int w, int h) { cell_w_ = w; cell_h_ = h; }
    /* The pixels of image cells (Cell::is_image). */
    const ImageStore& images() const { return images_; }
    ImageStore& images() { return images_; }
    /* UTF-8 (default) or 8-bit input; ESC % G and ESC % @ also switch. */
    void set_utf8(bool on) { parser_.set_utf8(on); }

    /* ---- input from the user ---- */

    /* A non-text key (tl::Key) with modifiers (tl::Mod). Returns false if
     * the key has no encoding. Bytes go to Host::write_pty. */
    bool key(int key, unsigned mods);
    /* Text a key produced (UTF-8, already including control characters
     * from Ctrl). Alt or Meta prefix ESC unless the host says otherwise. */
    void text_input(const char* utf8, size_t n, unsigned mods);
    /* Pasted text: newlines become CR, and with bracketed paste (mode
     * 2004) it is wrapped and stripped of ESC so it cannot end early. */
    void paste(const char* utf8, size_t n);
    /* Focus change, reported in mode 1004. */
    void focus(bool in);
    /* Backspace sends DEL (default) or BS. */
    void set_backspace_is_del(bool on) { backspace_del_ = on; }

    /* The application asked for mouse events (modes 9, 1000-1003). */
    bool mouse_reporting() const { return modes_.mouse != 0; }
    /* Report a mouse event at viewport cell (col, row), pixel (px, py)
     * for mode 1016. Returns true if it was reported; false means the
     * application does not want it and the host may use it (selection). */
    bool mouse(MouseAction action, int button, int col, int row, unsigned mods,
               int px, int py);

    /* ---- selection ---- */

    /* Start a selection at an absolute row and column of the active
     * screen; select_extend moves its other end. The selection follows
     * its text through scrolling and reflow (it is held by anchors) and
     * is dropped when that text goes away or the screen switches. */
    void select_start(uint64_t row, int col, SelectMode mode);
    void select_extend(uint64_t row, int col);
    void select_all();
    void select_clear();
    bool has_selection() const;
    /* The selected text, soft wraps joined, lines separated by '\n'. */
    bool selection_text(Buffer& out) const;
    /* The selection as absolute rows and columns, words and lines already
     * expanded. False if there is none. */
    bool selection_range(uint64_t* r0, int* c0, uint64_t* r1, int* c1) const;

    /* ---- presenting ---- */

    /* What to draw; see view.h. */
    View view() const;
    /* The host has drawn everything the last View reported. */
    void clear_damage();

    /* Scrollback viewport (primary screen only; the alternate screen has
     * no scrollback). Negative scrolls back into history. While scrolled
     * back the viewport stays on its text as output arrives; at the
     * bottom it follows the output. */
    void scroll_viewport(long rows);
    void scroll_to_bottom();
    void scroll_to_top();
    uint64_t viewport_top() const;
    bool viewport_at_bottom() const { return viewport_top() == grid().active_top(); }

    /* Colours. The palette starts as xterm's; hosts set the defaults. */
    void set_default_colors(Rgb fg, Rgb bg);
    void set_palette(int index, Rgb c);
    Rgb palette(int index) const { return palette_[index & 255]; }
    Rgb default_fg() const { return fg_; }
    Rgb default_bg() const { return bg_; }
    /* Bold text in palette colours 0-7 uses 8-15 (xterm's boldColors). */
    void set_bold_is_bright(bool on) { bold_bright_ = on; full_damage_ = true; }
    bool bold_is_bright() const { return bold_bright_; }
    /* Reverse video (DECSCNM) set by the host: xterm's menu toggle, or a
     * visual bell flashing the screen. */
    void set_reverse_video(bool on) { set_mode(5, true, on); }

    /* Text between two positions of the active screen (absolute rows,
     * columns inclusive), soft wraps joined and trailing blanks of each
     * line dropped. For selections and copying. */
    void text(uint64_t row0, int col0, uint64_t row1, int col1, Buffer& out) const;

    /* Row y of the active area as UTF-8, trailing blanks dropped. */
    void row_text(int y, Buffer& out) const;
    void append_cells(const Row& r, int lo, int hi, Buffer& out, bool trim_spaces) const;
    /* The whole active area, rows separated by '\n', trailing blank rows
     * dropped. For tests and debugging. */
    void screen_text(Buffer& out) const;

private:
    /* ParserSink */
    virtual void print(uint32_t cp);
    virtual void print_run(const uint8_t* s, size_t n);
    virtual void execute(uint8_t c);
    virtual void esc_dispatch(const Seq& q);
    virtual void csi_dispatch(const Seq& q);
    virtual void dcs_hook(const Seq& q);
    virtual void dcs_put(const uint8_t* s, size_t n);
    virtual void dcs_unhook(bool aborted);
    virtual void osc_dispatch(const uint8_t* s, size_t n, bool truncated);

    Screen& screen() { return alt_ ? alt_screen_ : primary_; }
    const Screen& screen() const { return alt_ ? alt_screen_ : primary_; }
    Row& row(int y) { return screen().grid.row(y); }

    /* Printing */
    void print_char(uint32_t cp);
    void append_to_previous(uint32_t cp);
    void wrap_line();
    void put_cell(int x, int y, uint32_t cp, WideKind w);

    /* Cursor */
    void set_cursor(int x, int y);
    void move_to(int x, int y);           /* honours origin mode */
    void damage_cursor();
    int region_top() const { return modes_.origin ? top_ : 0; }
    int region_bottom() const { return modes_.origin ? bot_ : rows_ - 1; }
    void save_cursor();
    void restore_cursor();
    void update_style_id();
    uint16_t intern(const Style& s);
    void collect_styles();

    /* Editing */
    Cell blank() ;                        /* erase cell: current background */
    void fix_wide_left(Row& r, int x);
    void fix_wide_right(Row& r, int x);
    void erase_cells(int y, int lo, int hi, bool selective);
    void erase_display(int mode, bool selective);
    void erase_line(int mode, bool selective);
    void insert_chars(int n);
    void delete_chars(int n);
    void insert_lines(int n);
    void delete_lines(int n);
    void index();
    void reverse_index();
    void scroll_up(int n);
    void scroll_down(int n);
    void tab_forward(int n);
    void tab_backward(int n);
    void decaln();

    /* Modes and screens */
    void set_mode(int mode, bool priv, bool on);
    int mode_state(int mode, bool priv) const;
    void switch_screen(bool alt, bool clear);
    void soft_reset();

    /* Sequences */
    void sgr(const Params& p);
    int sgr_color(const Params& p, int e, uint32_t* out);
    void device_status(const Seq& q);
    void device_attributes(const Seq& q);
    void window_ops(const Params& p);
    void graphics_attributes(const Params& p);
    /* Images */
    void place_image(uint32_t* px, int w, int h);
    void collect_images();
    void reply(const char* s);
    void reply(Buffer& b) { reply(b.c_str()); }

    Parser parser_;
    Host* host_;
    Host null_host_;
    Screen primary_;
    Screen alt_screen_;
    bool alt_;
    int cols_, rows_;
    size_t scrollback_;
    Cursor cur_;
    Modes modes_;
    CursorStyle cursor_shape_;
    int top_, bot_;                       /* scroll region, inclusive */
    bool* tabs_;
    Buffer title_;
    bool ambiguous_wide_;
    bool reflow_;
    bool backspace_del_;

    /* Mouse reporting state. */
    int mouse_buttons_;         /* bit per held button 1-3 */
    int mouse_last_col_, mouse_last_row_;

    /* Selection: two anchors in the active screen's grid. */
    int sel_anchor_, sel_point_;
    bool sel_alt_;
    SelectMode sel_mode_;
    void select_damage(uint64_t r0, uint64_t r1);
    /* Text in active row y changes: a selection over it goes away. */
    void sel_touch(int y) { if (sel_anchor_) sel_touch_slow(y); }
    void sel_touch_slow(int y);
    /* Rows top..bottom (active) are about to move: text under a selection
     * that overlaps them changes, so the selection goes away. */
    void sel_rows_moving(int top, int bottom) { if (sel_anchor_) sel_rows_moving_slow(top, bottom); }
    void sel_rows_moving_slow(int top, int bottom);
    /* The range last shown; when the current one differs (anchors lost or
     * moved by the interpreter), both are damaged. */
    bool sel_shown_;
    uint64_t sel_shown_r0_, sel_shown_r1_;
    int sel_shown_c0_, sel_shown_c1_;
    void sel_refresh();
    void select_damage_current();
    bool is_word_char(uint32_t cp) const;
    void word_bounds(uint64_t row, int col, uint64_t* r0, int* c0, uint64_t* r1, int* c1) const;
    void line_bounds(uint64_t row, uint64_t* r0, uint64_t* r1) const;

    /* Presentation state. */
    Rgb palette_[256];
    Rgb fg_, bg_;
    bool bold_bright_;
    bool full_damage_;          /* palette, reverse video, screen switch, viewport jump */
    int vp_anchor_;             /* viewport top in the primary grid, when not following */
    bool follow_;
    /* What the last cleared frame showed. */
    bool last_valid_;
    bool last_at_bottom_;
    bool last_alt_;
    uint64_t last_top_;
    int last_cx_, last_cy_;
    bool last_cwide_;

    /* Last printed cell, for combining characters and REP. */
    bool prev_valid_;
    int prev_x_, prev_y_;
    uint32_t last_cp_;
    GraphemeState gstate_;
    /* Images */
    ImageStore images_;
    SixelDecoder sixel_;
    bool in_sixel_;
    int cell_w_, cell_h_;

    Terminal(const Terminal&);
    Terminal& operator=(const Terminal&);
};

} /* namespace tl */

#endif
