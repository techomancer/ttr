#ifndef TTR_CORE_GEOM_H
#define TTR_CORE_GEOM_H

namespace ttr {

struct Rect {
    int x, y, w, h;

    Rect() : x(0), y(0), w(0), h(0) {}
    Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}

    bool contains(int px, int py) const
    {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

/* ICCCM WM_NORMAL_HINTS size rules, without aspect ratio. 0 means unset. */
struct SizeConstraints {
    int min_w, min_h;
    int max_w, max_h;
    int inc_w, inc_h;
    int base_w, base_h;

    SizeConstraints();
    void apply(int* w, int* h) const;
};

}

#endif
