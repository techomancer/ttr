/*
 * tw/backgrounds.h - animated backgrounds behind terminal text, drawn by
 * the GL renderer: night (stars), sunset (synthwave grid), ocean (waves,
 * bubbles, fish), aurora, embers and static. From dash, our earlier
 * Motif/GL terminal (Copyright (c) 2025 Dominik Behr, BSD licence).
 */
#ifndef TW_BACKGROUNDS_H
#define TW_BACKGROUNDS_H

namespace tw {

class Background {
public:
    virtual ~Background() {}
    /* The window's size, and the cell grid (for static). */
    virtual void resize(int w, int h, int cols, int rows, int cell_w, int cell_h, int mx, int my)
    {
        w_ = w;
        h_ = h;
        cols_ = cols;
        rows_ = rows;
        cw_ = cell_w;
        ch_ = cell_h;
        mx_ = mx;
        my_ = my;
    }
    /* Advance by dt seconds. */
    virtual void update(float dt) = 0;
    /* Draw over the cleared window, before the text (GL state: blending
     * on, texturing off; leave it so). */
    virtual void draw() = 0;

protected:
    Background() : w_(1), h_(1), cols_(1), rows_(1), cw_(8), ch_(16), mx_(0), my_(0), br_(1.0f) {}
    int w_, h_, cols_, rows_, cw_, ch_, mx_, my_;
    float br_;                  /* brightness */

    friend Background* create_background(const char*, int, int, bool, float);
};

/* "night", "sunset", "ocean", "aurora", "embers" or "static"; 0 for
 * anything else ("none"). impact: textures as GL_LUMINANCE_ALPHA. */
Background* create_background(const char* name, int w, int h, bool impact, float brightness);
/* The names, null-terminated. */
const char* const* background_names();

} /* namespace tw */

#endif
