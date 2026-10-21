/* ttrt: a tl::Terminal seen as a tw::Source. */
#ifndef TTRT_SOURCE_H
#define TTRT_SOURCE_H

#include "../../tl/terminal.h"
#include "../../tw/tw.h"

namespace ttrt {

class TermSource : public tw::Source {
public:
    explicit TermSource(tl::Terminal& t) : t_(t), v_(t.view()), cursor_(0), have_cursor_(false) {}

    /* A fixed cursor colour (-cr); otherwise the text colour. */
    void set_cursor_color(uint32_t rgb)
    {
        cursor_ = rgb;
        have_cursor_ = true;
    }
    void clear_cursor_color() { have_cursor_ = false; }

    void frame_begin() { v_ = t_.view(); }
    int cols() const { return v_.cols(); }
    int rows() const { return v_.rows(); }
    bool held() const { return v_.held(); }
    bool full_damage() const { return v_.full_damage(); }
    int scroll_count() const { return v_.scroll_record_count(); }
    void scroll(int i, int* top, int* bottom, int* count) const;
    bool row_dirty(int y, int* lo, int* hi) const { return v_.row_dirty(y, lo, hi); }
    void cells(int y, int lo, int hi, tw::CellData* out) const;
    int cluster(int y, int x, uint32_t* cps, int max) const;
    tw::LineAttr line_attr(int y) const;
    tw::Cursor cursor() const;
    uint32_t background() const;
    uint32_t cursor_color() const;
    void frame_done() { t_.clear_damage(); }

private:
    tl::Terminal& t_;
    tl::View v_;
    uint32_t cursor_;
    bool have_cursor_;
};

} /* namespace ttrt */

#endif
