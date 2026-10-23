/*
 * Animated backgrounds for the GL renderer, ported from dash
 * (dash-night.c, dash-sunset.c, dash-ocean.c, dash-aurora.c,
 * dash-embers.c, dash-static.c; Copyright (c) 2025 Dominik Behr, BSD
 * licence) to C++98 and OpenGL 1.1. Each draws in window pixels with the
 * origin at the top left.
 */
#if defined(TTR_HAVE_GL)

#include "backgrounds.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>

namespace tw {

namespace {

const float k_pi = 3.14159265358979323846f;

float fsin(float x)
{
    return (float)sin((double)x);
}

float fcos(float x)
{
    return (float)cos((double)x);
}

int rnd(int n)
{
    return n > 0 ? rand() % n : 0;
}

/* ---- night: drifting stars and the odd shooting star ---- */

class Night : public Background {
public:
    void resize(int w, int h, int cols, int rows, int cw, int ch, int mx, int my)
    {
        bool first = w_ <= 1;
        Background::resize(w, h, cols, rows, cw, ch, mx, my);
        if (!first)
            return;
        for (int i = 0; i < NSTARS; i++) {
            Star& s = stars_[i];
            s.x = (float)rnd(w);
            s.y = (float)rnd(h);
            s.speed = 5.0f + (float)rnd(10);
            s.bright = 0.1f + (float)rnd(40) / 100.0f;
            s.cross = rnd(2) == 1;
            s.r = s.g = s.b = 0.8f;
            switch (rnd(6)) {
            case 0: s.r = 1.0f; break;
            case 1: s.g = 1.0f; break;
            case 2: s.b = 1.0f; break;
            default: break;
            }
        }
        shooter_ = false;
    }

    void update(float dt)
    {
        for (int i = 0; i < NSTARS; i++) {
            Star& s = stars_[i];
            s.x += s.speed * dt;
            if (s.x > w_) {
                s.x = 0;
                s.y = (float)rnd(h_);
            }
        }
        if (shooter_) {
            float move = shoot_speed_ * dt;
            sx_ += move;
            sy_ += move;
            if (sx_ > w_ || sy_ > h_)
                shooter_ = false;
        } else if (rnd(1000) < 5) {
            shooter_ = true;
            sx_ = (float)rnd(w_);
            sy_ = 0;
            shoot_speed_ = 300.0f + (float)rnd(200);
            tail_ = 50.0f + (float)rnd(50);
        }
    }

    void draw()
    {
        float br = br_;
        glBegin(GL_POINTS);
        for (int i = 0; i < NSTARS; i++) {
            const Star& s = stars_[i];
            glColor4f(s.r * br, s.g * br, s.b * br, s.bright);
            glVertex2f(s.x, s.y);
            if (s.cross) {
                glVertex2f(s.x + 1, s.y);
                glVertex2f(s.x - 1, s.y);
                glVertex2f(s.x, s.y + 1);
                glVertex2f(s.x, s.y - 1);
            }
        }
        glEnd();
        if (shooter_) {
            glBegin(GL_LINES);
            glColor4f(br, br, 0.8f * br, 0.8f);
            glVertex2f(sx_, sy_);
            glColor4f(br, br, 0.8f * br, 0.0f);
            glVertex2f(sx_ - tail_, sy_ - tail_);
            glEnd();
            glBegin(GL_POINTS);
            glColor4f(br, br, br, 1.0f);
            glVertex2f(sx_ + 1, sy_);
            glVertex2f(sx_ - 1, sy_);
            glVertex2f(sx_, sy_ + 1);
            glVertex2f(sx_, sy_ - 1);
            glEnd();
        }
    }

private:
    enum { NSTARS = 128 };
    struct Star {
        float x, y, speed, bright, r, g, b;
        bool cross;
    };
    Star stars_[NSTARS];
    bool shooter_;
    float sx_, sy_, shoot_speed_, tail_;
};

/* ---- sunset: a synthwave sky and a perspective grid ---- */

class Sunset : public Background {
public:
    Sunset() : offset_(0) {}

    void update(float dt)
    {
        offset_ += 0.5f * dt;
        if (offset_ > 1.0f)
            offset_ -= 1.0f;
    }

    void draw()
    {
        int w = w_, h = h_;
        float b = 0.25f * br_, br = br_;
        glDisable(GL_BLEND);
        glShadeModel(GL_SMOOTH);
        glBegin(GL_QUAD_STRIP);
        glColor3f(0, 0, 0);
        glVertex2i(0, 0);
        glVertex2i(w, 0);
        glColor3f(0, 0, 0.5f * b);
        glVertex2f(0, h * 0.4f);
        glVertex2f((float)w, h * 0.4f);
        glColor3f(0.8f * b, 0, 0.5f * b);
        glVertex2f(0, h * 0.65f);
        glVertex2f((float)w, h * 0.65f);
        glColor3f(0.9f * b, 0.4f * b, 0);
        glVertex2f(0, h * 0.80f);
        glVertex2f((float)w, h * 0.80f);
        glColor3f(0, 0, 0);
        glVertex2i(0, h);
        glVertex2i(w, h);
        glEnd();
        glEnable(GL_BLEND);
        glBegin(GL_LINES);
        float cx = w * 0.5f, horizon = h * 0.80f, spacing = w / 8.0f;
        for (int i = -10; i <= 10; i++) {
            glColor4f(br, br, br, 0.0f);
            glVertex2f(cx, horizon);
            glColor4f(br, br, br, 0.1f);
            glVertex2f(cx + i * spacing, (float)h);
        }
        float ground = h - horizon;
        for (float z = 1.0f - offset_; z < 20.0f; z += 1.0f) {
            if (z < 0.1f)
                continue;
            float y = horizon + ground / z;
            glColor4f(br, br, br, 0.2f / z);
            glVertex2f(0, y);
            glVertex2f((float)w, y);
        }
        glEnd();
    }

private:
    float offset_;
};

/* ---- ocean: waves, rising bubbles, passing fish ---- */

class Ocean : public Background {
public:
    Ocean() : spawn_(0), fish_spawn_(5.0f + (float)rnd(10))
    {
        memset(bubbles_, 0, sizeof(bubbles_));
        memset(fish_, 0, sizeof(fish_));
        for (int i = 0; i <= ROWS; i++) {
            lines_[i].speed = (float)(rnd(100) - 50);
            lines_[i].period = 1.0f + (float)rnd(100) / 100.0f;
            lines_[i].offset = (float)rnd(1000);
        }
    }

    void update(float dt)
    {
        for (int i = 0; i <= ROWS; i++)
            lines_[i].offset += lines_[i].speed * dt;
        spawn_ -= dt;
        if (spawn_ <= 0) {
            int count = 3 + rnd(5);
            float gx = (float)rnd(w_);
            spawn_ = 10.0f + (float)rnd(500) / 100.0f;
            for (int i = 0; i < NBUBBLES && count > 0; i++) {
                Bubble& b = bubbles_[i];
                if (b.active)
                    continue;
                b.active = true;
                b.base_x = gx + (float)(rnd(60) - 30);
                b.y = h_ + AMP + (float)rnd(50);
                b.speed = 8.0f + (float)rnd(50) / 10.0f;
                b.phase = (float)rnd(628) / 100.0f;
                b.amp = 2.0f + (float)rnd(30) / 10.0f;
                b.size = rnd(3);
                count--;
            }
        }
        for (int i = 0; i < NBUBBLES; i++)
            if (bubbles_[i].active) {
                bubbles_[i].y -= bubbles_[i].speed * dt;
                if (bubbles_[i].y < -20.0f)
                    bubbles_[i].active = false;
            }
        fish_spawn_ -= dt;
        if (fish_spawn_ <= 0) {
            bool school = rnd(2) == 1;
            bool left = rnd(2) == 1;
            float start_y = h_ * 0.4f + (float)rnd((int)(h_ * 0.5f));
            float speed = 30.0f + (float)rnd(40);
            if (left)
                speed = -speed;
            int count = school ? 3 + rnd(4) : 1;
            float size = school ? 10.0f + (float)rnd(10) : 30.0f + (float)rnd(30);
            fish_spawn_ = 15.0f + (float)rnd(20);
            for (int i = 0; i < NFISH && count > 0; i++) {
                Fish& f = fish_[i];
                if (f.active)
                    continue;
                f.active = true;
                f.size = size * (0.8f + (float)rnd(40) / 100.0f);
                f.speed = speed * (0.9f + (float)rnd(20) / 100.0f);
                float xo = (float)rnd(100);
                f.x = left ? w_ + 100.0f + xo : -100.0f - xo;
                f.y = start_y + (float)(rnd(60) - 30);
                count--;
            }
        }
        for (int i = 0; i < NFISH; i++) {
            Fish& f = fish_[i];
            if (!f.active)
                continue;
            f.x += f.speed * dt;
            if ((f.speed > 0 && f.x > w_ + 150.0f) || (f.speed < 0 && f.x < -150.0f))
                f.active = false;
        }
    }

    void draw()
    {
        float br = br_;
        float top = -AMP, total = h_ + 2 * AMP;
        float row_h = total / ROWS, col_w = (float)w_ / COLS;
        glShadeModel(GL_SMOOTH);
        for (int r = 0; r < ROWS; r++) {
            glBegin(GL_QUAD_STRIP);
            for (int c = 0; c <= COLS; c++) {
                float x = c * col_w;
                for (int k = 0; k < 2; k++) {
                    int line = r + k;
                    float y = top + line * row_h +
                              AMP * fsin(2.0f * k_pi * lines_[line].period *
                                         (x + lines_[line].offset) / (float)w_);
                    float col[3];
                    color((float)line / ROWS, col);
                    glColor3f(col[0], col[1], col[2]);
                    glVertex2f(x, y);
                }
            }
            glEnd();
        }
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < NFISH; i++) {
            const Fish& f = fish_[i];
            if (!f.active)
                continue;
            float x = f.x, y = f.y, s = f.size, d = f.speed > 0 ? 1.0f : -1.0f;
            glColor4f(0.15f * br, 0.15f * br, 0.2f * br, 0.8f);
            glVertex2f(x + s * 0.5f * d, y);
            glVertex2f(x - s * 0.5f * d, y);
            glVertex2f(x, y - s * 0.3f);
            glColor4f(0.25f * br, 0.25f * br, 0.3f * br, 0.8f);
            glVertex2f(x + s * 0.5f * d, y);
            glVertex2f(x - s * 0.5f * d, y);
            glVertex2f(x, y + s * 0.3f);
            glColor4f(0.2f * br, 0.2f * br, 0.25f * br, 0.8f);
            glVertex2f(x - s * 0.5f * d, y);
            glVertex2f(x - s * 0.8f * d, y - s * 0.25f);
            glVertex2f(x - s * 0.8f * d, y + s * 0.25f);
        }
        glEnd();
        static const float sizes[3] = { 2.5f, 4.0f, 5.5f };
        for (int i = 0; i < NBUBBLES; i++) {
            const Bubble& b = bubbles_[i];
            if (!b.active)
                continue;
            float x = b.base_x + fsin(b.y * 0.05f + b.phase) * b.amp, y = b.y;
            float rad = sizes[b.size];
            glBegin(GL_TRIANGLE_FAN);
            glColor4f(br, br, br, 0.05f);
            glVertex2f(x, y);
            glColor4f(br, br, br, 0.20f);
            for (int j = 0; j <= 8; j++) {
                float a = j * 2.0f * k_pi / 8.0f;
                glVertex2f(x + fcos(a) * rad, y + fsin(a) * rad);
            }
            glEnd();
        }
    }

private:
    enum { COLS = 8, ROWS = 4, NBUBBLES = 16, NFISH = 16 };
    static const float AMP;
    struct Line {
        float speed, period, offset;
    };
    struct Bubble {
        float base_x, y, speed, phase, amp;
        int size;
        bool active;
    };
    struct Fish {
        float x, y, speed, size;
        bool active;
    };

    /* t from 0 (top) to 1 (bottom): grey-blue, deep blue, black. */
    void color(float t, float* out) const
    {
        static const float c[3][3] = { { 0.1f, 0.1f, 0.3f }, { 0, 0, 0.4f }, { 0, 0, 0 } };
        int a = t < 0.5f ? 0 : 1;
        float f = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
        for (int i = 0; i < 3; i++)
            out[i] = (c[a][i] * (1.0f - f) + c[a + 1][i] * f) * br_;
    }

    Line lines_[ROWS + 1];
    Bubble bubbles_[NBUBBLES];
    Fish fish_[NFISH];
    float spawn_, fish_spawn_;
};

const float Ocean::AMP = 16.0f;

/* ---- aurora: three translucent waving curtains ---- */

class Aurora : public Background {
public:
    void resize(int w, int h, int cols, int rows, int cw, int ch, int mx, int my)
    {
        Background::resize(w, h, cols, rows, cw, ch, mx, my);
        static const float rgb[3][3] = { { 0, 0.8f, 0.6f }, { 0.4f, 0, 0.8f }, { 0, 0.2f, 0.9f } };
        static const float base[3] = { 0.2f, 0.3f, 0.4f }, height[3] = { 0.4f, 0.35f, 0.3f };
        static const float period[3] = { 1.0f, 1.5f, 2.0f }, speed[3] = { 10.0f, -8.0f, 5.0f };
        for (int i = 0; i < 3; i++) {
            Layer& l = layers_[i];
            l.r = rgb[i][0];
            l.g = rgb[i][1];
            l.b = rgb[i][2];
            l.y_base = h * base[i];
            l.height = h * height[i];
            l.period = period[i];
            l.speed = speed[i];
            if (!started_)
                l.offset = (float)rnd(100);
        }
        started_ = true;
    }

    Aurora() : started_(false) {}

    void update(float dt)
    {
        for (int i = 0; i < 3; i++)
            layers_[i].offset += layers_[i].speed * dt;
    }

    void draw()
    {
        float br = br_;
        glShadeModel(GL_SMOOTH);
        for (int i = 0; i < 3; i++) {
            const Layer& l = layers_[i];
            glBegin(GL_QUAD_STRIP);
            for (int j = 0; j <= 40; j++) {
                float x = (float)j * w_ / 40;
                float y = l.y_base + fsin(x * 0.01f * l.period + l.offset * 0.05f) * 30.0f;
                glColor4f(l.r * br, l.g * br, l.b * br, 0.0f);
                glVertex2f(x, y);
                glColor4f(l.r * br, l.g * br, l.b * br, 0.15f);
                glVertex2f(x, y + l.height);
            }
            glEnd();
        }
    }

private:
    struct Layer {
        float speed, offset, height, y_base, period, r, g, b;
    };
    Layer layers_[3];
    bool started_;
};

/* ---- embers: rising, drifting sparks ---- */

class Embers : public Background {
public:
    Embers() : started_(false) {}

    void resize(int w, int h, int cols, int rows, int cw, int ch, int mx, int my)
    {
        Background::resize(w, h, cols, rows, cw, ch, mx, my);
        if (!started_)
            for (int i = 0; i < N; i++)
                spawn(e_[i], true);
        started_ = true;
    }

    void update(float dt)
    {
        for (int i = 0; i < N; i++) {
            Ember& e = e_[i];
            e.y -= e.speed_y * dt;
            e.life -= e.decay * dt;
            e.phase += e.drift_speed * dt;
            if (e.y < -10.0f || e.life <= 0)
                spawn(e, false);
        }
    }

    void draw()
    {
        float br = br_;
        glShadeModel(GL_SMOOTH);
        glBegin(GL_LINES);
        for (int i = 0; i < N; i++) {
            const Ember& e = e_[i];
            float x = e.x + fsin(e.phase) * e.drift_amp;
            float vx = e.drift_amp * e.drift_speed * fcos(e.phase), vy = -e.speed_y;
            glColor4f(e.r * br, e.g * br, e.b * br, e.life);
            glVertex2f(x, e.y);
            glColor4f(0, 0, 0, 0);
            glVertex2f(x - vx * 0.15f, e.y - vy * 0.15f);
        }
        glEnd();
    }

private:
    enum { N = 150 };
    struct Ember {
        float x, y, speed_y, drift_amp, drift_speed, phase, life, decay, r, g, b;
    };

    void spawn(Ember& e, bool initial)
    {
        e.x = (float)rnd(w_);
        if (initial) {
            e.y = (float)rnd(h_);
            e.life = (float)rnd(100) / 100.0f;
        } else {
            e.y = (float)h_ + (float)rnd(20);
            e.life = 1.0f;
        }
        e.speed_y = 20.0f + (float)rnd(40);
        e.drift_amp = 5.0f + (float)rnd(15);
        e.drift_speed = 1.0f + (float)rnd(20) / 10.0f;
        e.phase = (float)rnd(628) / 100.0f;
        e.decay = 0.1f + (float)rnd(30) / 100.0f;
        e.r = 1.0f;
        e.g = 0.2f + (float)rnd(60) / 100.0f;
        e.b = (float)rnd(20) / 100.0f;
    }

    Ember e_[N];
    bool started_;
};

/* ---- static: faint TV noise, a random noise tile per cell ---- */

class Static : public Background {
public:
    explicit Static(bool impact) : tex_(0), impact_(impact) {}
    ~Static()
    {
        if (tex_)
            glDeleteTextures(1, &tex_);
    }

    void update(float) {}

    void draw()
    {
        if (!tex_)
            make_texture();
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glColor4f(1.0f, 1.0f, 1.0f, 0.05f * br_);
        const float tile = 1.0f / 4;
        glBegin(GL_QUADS);
        for (int r = 0; r < rows_; r++)
            for (int c = 0; c < cols_; c++) {
                int t = rnd(16);
                float u0 = (t % 4) * tile, v0 = (t / 4) * tile, u1 = u0 + tile, v1 = v0 + tile;
                if (rnd(2)) {
                    float x = u0;
                    u0 = u1;
                    u1 = x;
                }
                if (rnd(2)) {
                    float x = v0;
                    v0 = v1;
                    v1 = x;
                }
                float x = (float)(mx_ + c * cw_), y = (float)(my_ + r * ch_);
                glTexCoord2f(u0, v0);
                glVertex2f(x, y);
                glTexCoord2f(u1, v0);
                glVertex2f(x + cw_, y);
                glTexCoord2f(u1, v1);
                glVertex2f(x + cw_, y + ch_);
                glTexCoord2f(u0, v1);
                glVertex2f(x, y + ch_);
            }
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }

private:
    void make_texture()
    {
        const int size = 64;
        int bpp = impact_ ? 2 : 1;
        GLenum format = impact_ ? GL_LUMINANCE_ALPHA : GL_ALPHA;
        unsigned char* data = (unsigned char*)malloc((size_t)(size * size * bpp));
        if (!data)
            return;
        for (int i = 0; i < size * size; i++) {
            if (impact_) {
                data[i * 2] = 0xff;
                data[i * 2 + 1] = (unsigned char)rnd(256);
            } else {
                data[i] = (unsigned char)rnd(256);
            }
        }
        glGenTextures(1, &tex_);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)format, size, size, 0, format, GL_UNSIGNED_BYTE,
                     data);
        free(data);
    }

    GLuint tex_;
    bool impact_;
};

const char* const k_names[] = { "night", "sunset", "ocean", "aurora", "embers", "static", 0 };

} /* namespace */

const char* const* background_names()
{
    return k_names;
}

Background* create_background(const char* name, int w, int h, bool impact, float brightness)
{
    if (!name)
        return 0;
    Background* b = 0;
    if (strcmp(name, "night") == 0)
        b = new Night();
    else if (strcmp(name, "sunset") == 0)
        b = new Sunset();
    else if (strcmp(name, "ocean") == 0)
        b = new Ocean();
    else if (strcmp(name, "aurora") == 0)
        b = new Aurora();
    else if (strcmp(name, "embers") == 0)
        b = new Embers();
    else if (strcmp(name, "static") == 0)
        b = new Static(impact);
    if (b) {
        b->br_ = brightness;
        b->resize(w, h, 1, 1, 8, 16, 0, 0);
    }
    return b;
}

} /* namespace tw */

#else

#include "backgrounds.h"

namespace tw {

const char* const* background_names()
{
    static const char* const none[] = { 0 };
    return none;
}

Background* create_background(const char*, int, int, bool, float)
{
    return 0;
}

} /* namespace tw */

#endif
