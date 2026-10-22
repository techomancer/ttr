/*
 * tu/screen.h - a screen of cells drawn by the application and sent to
 * the terminal as a minimal update (ttrc/DESIGN.md section 3).
 *
 * Drawing goes into a back buffer; flush() compares it with what the
 * terminal shows and writes only the difference. Frames and shadows are
 * requested by style and drawn as well as the terminal allows: rounded,
 * heavy and double Unicode lines on a modern terminal, the alternate
 * character set on iris-ansi or vt100, ASCII as the last resort.
 */
#ifndef TU_SCREEN_H
#define TU_SCREEN_H

#include "term.h"
#include "tu.h"

namespace tu {

enum FrameStyle {
    FRAME_SINGLE,
    FRAME_DOUBLE,
    FRAME_HEAVY,
    FRAME_ROUNDED
};

struct Cell {
    uint32_t ch;        /* 0: the right half of a wide character */
    Style style;
};

class Screen {
public:
    explicit Screen(Term& term);
    ~Screen();

    /* To the terminal's size; everything is redrawn on the next flush. */
    void resize();
    int cols() const { return cols_; }
    int rows() const { return rows_; }
    Term& term() { return term_; }

    void clear(const Style& s);
    void fill(int x, int y, int w, int h, uint32_t ch, const Style& s);
    void put(int x, int y, uint32_t ch, const Style& s);
    /* UTF-8 text (bytes that are not UTF-8 count as Latin-1), at most
     * max_cols columns (-1: to the edge). Returns the columns used. */
    int text(int x, int y, const char* s, const Style& st, int max_cols = -1);
    /* Restyle cells without changing their characters. */
    void restyle(int x, int y, int w, const Style& s);

    /* A frame; the style is degraded to what the terminal can show. */
    void frame(int x, int y, int w, int h, FrameStyle f, const Style& s);
    /* A line across a frame, joining its sides (or top and bottom). */
    void hline(int x, int y, int w, FrameStyle frame, const Style& s);
    void vline(int x, int y, int h, FrameStyle frame, const Style& s);
    /* Darken what is under a dialog's shadow. */
    void shadow(int x, int y, int w, int h);

    void set_cursor(int x, int y, bool visible);

    /* Forget what the terminal shows: the next flush redraws all. */
    void invalidate() { full_ = true; }
    void flush();

    /* Columns UTF-8 text takes, with the same decoding as text(). */
    static int text_width(const char* s);

private:
    FrameStyle effective(FrameStyle f) const;
    Cell* back(int x, int y) { return &back_[y * cols_ + x]; }

    Term& term_;
    int cols_, rows_;
    Cell* back_;
    Cell* front_;
    bool full_;
    int cur_x_, cur_y_;
    bool cur_visible_;

    Screen(const Screen&);
    Screen& operator=(const Screen&);
};

/* Decode one character of UTF-8, or one Latin-1 byte where the bytes are
 * not valid UTF-8. Returns the bytes used (0 at the end). */
int decode_char(const char* s, uint32_t* cp);

} /* namespace tu */

#endif
