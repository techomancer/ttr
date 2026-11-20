/* ttrv's GL renderer (glpaint.h). */
#include "glpaint.h"

#include "../../tw/renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(TTR_HAVE_GL)

#include <GL/gl.h>
#include <GL/glx.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif

namespace ttrv {

namespace {

/* Each tile repeats this many of its neighbours' pixels on every side:
 * enough for the 4 x 4 taps of the bicubic filter. */
const int BORDER = 2;

int pow2_at_least(int n)
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

/* Pixels at..at+n of a side, in a texture 'tex' texels long. */
struct Span {
    int at, n, tex;
};

int max_spans(int n, int max_tex)
{
    return n / (max_tex - 2 * BORDER) + 16;
}

/* A side of n pixels cut into tiles: whole ones of the largest size
 * there is room for, then the rest in one that is at most a quarter
 * padding (else another whole one, smaller). A side of 1024 is a 1024
 * and an 8, not a 2048. */
int split(int n, int max_tex, Span* out)
{
    int k = 0;
    for (int at = 0; at < n;) {
        int rest = n - at, need = rest + 2 * BORDER;
        int t = pow2_at_least(need);
        if (t <= max_tex && (need <= 64 || (t - need) * 4 <= t)) {
            out[k].at = at;
            out[k].n = rest;
            out[k].tex = t;
            return k + 1;
        }
        t = max_tex;
        while (t - 2 * BORDER > rest)
            t /= 2;
        out[k].at = at;
        out[k].n = t - 2 * BORDER;
        out[k].tex = t;
        at += out[k].n;
        k++;
    }
    return k;
}

long side_texels(int n, int max_tex)
{
    Span* s = (Span*)malloc(sizeof(Span) * (size_t)max_spans(n, max_tex));
    if (!s)
        return 0x7fffffffL;
    int k = split(n, max_tex, s);
    long sum = 0;
    for (int i = 0; i < k; i++)
        sum += s[i].tex;
    free(s);
    return sum;
}

/* What an image of w x h takes in textures, mipmaps included. */
long texture_bytes(int w, int h, int max_tex)
{
    double b = (double)side_texels(w, max_tex) * side_texels(h, max_tex) * 4 * 4 / 3;
    return b > 2e9 ? 2000000000L : (long)b;
}

/* A mipmap level from the one above (w x h, 'stride' bytes a row) into
 * out, packed: each pixel the average of the 2 x 2 (or 2 x 1) it covers.
 * Power-of-two sizes, so it divides evenly. Alpha is not kept (the
 * textures are RGB). */
void halve(const unsigned char* in, size_t stride, int w, int h, unsigned char* out)
{
    int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
    if (w > 1 && h > 1) {
        for (int y = 0; y < nh; y++) {
            const unsigned char* a = in + (size_t)(2 * y) * stride;
            const unsigned char* b = a + stride;
            unsigned char* o = out + (size_t)y * (size_t)nw * 4;
            for (int x = 0; x < nw; x++, a += 8, b += 8, o += 4) {
                o[0] = (unsigned char)((a[0] + a[4] + b[0] + b[4] + 2) >> 2);
                o[1] = (unsigned char)((a[1] + a[5] + b[1] + b[5] + 2) >> 2);
                o[2] = (unsigned char)((a[2] + a[6] + b[2] + b[6] + 2) >> 2);
                o[3] = 255;
            }
        }
        return;
    }
    /* One pixel wide or high: pairs along the other side. */
    size_t step = w > 1 ? 4 : stride;
    for (int i = 0; i < nw * nh; i++) {
        const unsigned char* p = in + (size_t)(2 * i) * step;
        for (int k = 0; k < 3; k++)
            out[i * 4 + k] = (unsigned char)((p[k] + p[k + step] + 1) >> 1);
        out[i * 4 + 3] = 255;
    }
}

/* No pixel of the image is transparent at all. */
bool opaque(const ti::Image& im)
{
    for (int y = 0; y < im.h; y++) {
        const unsigned char* p = im.row(y) + 3;
        for (int x = 0; x < im.w; x++, p += 4)
            if (*p != 255)
                return false;
    }
    return true;
}

#if !defined(__sgi)
/* The Catmull-Rom filter for enlarging: 4 x 4 texels around each pixel,
 * read at their centres (the magnification filter is GL_NEAREST). */
const char* const k_bicubic =
    "uniform sampler2D tex;\n"
    "uniform vec2 size;\n"
    "vec4 weights(float t) {\n"
    "    float t2 = t * t, t3 = t2 * t;\n"
    "    return vec4(-0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1.0,\n"
    "                -1.5 * t3 + 2.0 * t2 + 0.5 * t, 0.5 * t3 - 0.5 * t2);\n"
    "}\n"
    "vec4 row(vec2 b, float dy, vec4 w) {\n"
    "    return texture2D(tex, (b + vec2(-1.0, dy)) / size) * w.x +\n"
    "           texture2D(tex, (b + vec2(0.0, dy)) / size) * w.y +\n"
    "           texture2D(tex, (b + vec2(1.0, dy)) / size) * w.z +\n"
    "           texture2D(tex, (b + vec2(2.0, dy)) / size) * w.w;\n"
    "}\n"
    "void main() {\n"
    "    vec2 p = gl_TexCoord[0].st * size - 0.5;\n"
    "    vec2 i = floor(p);\n"
    "    vec2 f = p - i;\n"
    "    vec4 wx = weights(f.x), wy = weights(f.y);\n"
    "    vec2 b = i + 0.5;\n"
    "    vec4 c = row(b, -1.0, wx) * wy.x + row(b, 0.0, wx) * wy.y +\n"
    "             row(b, 1.0, wx) * wy.z + row(b, 2.0, wx) * wy.w;\n"
    "    gl_FragColor = clamp(c, 0.0, 1.0);\n"
    "}\n";

/* OpenGL 2.0's shader calls, looked up by name (the headers are 1.1's). */
typedef unsigned int (*PfnCreateShader)(unsigned int);
typedef void (*PfnShaderSource)(unsigned int, int, const char* const*, const int*);
typedef void (*PfnCompileShader)(unsigned int);
typedef void (*PfnGetShaderiv)(unsigned int, unsigned int, int*);
typedef unsigned int (*PfnCreateProgram)(void);
typedef void (*PfnAttachShader)(unsigned int, unsigned int);
typedef void (*PfnLinkProgram)(unsigned int);
typedef void (*PfnGetProgramiv)(unsigned int, unsigned int, int*);
typedef void (*PfnUseProgram)(unsigned int);
typedef int (*PfnGetUniformLocation)(unsigned int, const char*);
typedef void (*PfnUniform2f)(int, float, float);
typedef void (*PfnUniform1i)(int, int);
typedef void (*PfnDeleteProgram)(unsigned int);

void* proc(const char* name)
{
    return (void*)glXGetProcAddressARB((const GLubyte*)name);
}
#endif

} /* namespace */

#if !defined(__sgi)
struct GLPaint::Api {
    PfnCreateShader create_shader;
    PfnShaderSource shader_source;
    PfnCompileShader compile_shader;
    PfnGetShaderiv get_shaderiv;
    PfnCreateProgram create_program;
    PfnAttachShader attach_shader;
    PfnLinkProgram link_program;
    PfnGetProgramiv get_programiv;
    PfnUseProgram use_program;
    PfnGetUniformLocation get_uniform_location;
    PfnUniform2f uniform2f;
    PfnUniform1i uniform1i;
    PfnDeleteProgram delete_program;
};
#else
struct GLPaint::Api {
    int unused;
};
#endif

GLPaint::GLPaint()
    : dpy_(0), win_(0), ctx_(0), dbl_(false), accelerated_(false), edge_clamp_(false),
      gen_mipmap_(false), max_tex_(256), w_(0), h_(0), budget_(0), bg_(0), font_lists_(0),
      font_(0), clock_(0), prog_(0), u_size_(-1), api_(0)
{
}

GLPaint::~GLPaint()
{
    if (ctx_) {
        glXMakeCurrent(dpy_, win_, (GLXContext)ctx_);
        forget();
        if (font_lists_)
            glDeleteLists(font_lists_, 256);
#if !defined(__sgi)
        if (prog_ && api_ && api_->delete_program)
            api_->delete_program(prog_);
#endif
        glXMakeCurrent(dpy_, None, 0);
        glXDestroyContext(dpy_, (GLXContext)ctx_);
    }
    delete api_;
}

bool GLPaint::choose_visual(Display* dpy, int screen, XVisualInfo* out)
{
    return tw::choose_visual("gl", dpy, screen, out);
}

bool GLPaint::init(Display* dpy, Window win, const XVisualInfo& vi, char* why, size_t n)
{
    dpy_ = dpy;
    win_ = win;
    XVisualInfo v = vi;
    int use_gl = 0, rgba = 0, dbl = 0;
    glXGetConfig(dpy, &v, GLX_USE_GL, &use_gl);
    glXGetConfig(dpy, &v, GLX_RGBA, &rgba);
    glXGetConfig(dpy, &v, GLX_DOUBLEBUFFER, &dbl);
    if (!use_gl || !rgba) {
        snprintf(why, n, "the visual has no RGBA GL");
        return false;
    }
    GLXContext ctx = glXCreateContext(dpy, &v, 0, True);
    if (!ctx) {
        snprintf(why, n, "glXCreateContext failed");
        return false;
    }
    ctx_ = ctx;
    dbl_ = dbl != 0;
    if (!glXMakeCurrent(dpy, win, ctx)) {
        snprintf(why, n, "glXMakeCurrent failed");
        return false;
    }
    const char* r = (const char*)glGetString(GL_RENDERER);
    const char* ver = (const char*)glGetString(GL_VERSION);
    const char* ext = (const char*)glGetString(GL_EXTENSIONS);
    if (!r)
        r = "";
    if (!ver)
        ver = "1.0";
    if (!ext)
        ext = "";
    int major = atoi(ver), minor = 0;
    const char* dot = strchr(ver, '.');
    if (dot)
        minor = atoi(dot + 1);
    char nm[200];
    snprintf(nm, sizeof(nm), "%.120s (GL %d.%d%s)", r, major, minor,
             glXIsDirect(dpy, ctx) ? "" : ", indirect");
    name_ = nm;
    /* Software rasterizers, and boards that texture in software (the
     * Indy's Newport, XZ, Elan): the soft renderer is faster there.
     * IMPACT textures in hardware but from 1 to 4 MB of TRAM, less than
     * a screenful. */
    static const char* const slow[] = { "llvmpipe", "softpipe", "Software Rasterizer", "swrast",
                                        "NEWPORT", "GR2", "GR3", "GU1", "XZ", "ELAN", "IMPACT",
                                        0 };
    accelerated_ = glXIsDirect(dpy, ctx) != 0;
    for (int i = 0; slow[i]; i++)
        if (strstr(r, slow[i]))
            accelerated_ = false;
    edge_clamp_ = major > 1 || minor >= 2 || strstr(ext, "GL_SGIS_texture_edge_clamp") ||
                  strstr(ext, "GL_EXT_texture_edge_clamp");
    /* TTRV_GL_CPUMIPS: mipmaps made here, as on the boards that cannot. */
    gen_mipmap_ = (major > 1 || minor >= 4 || strstr(ext, "GL_SGIS_generate_mipmap")) &&
                  !getenv("TTRV_GL_CPUMIPS");
    GLint mt = 256;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mt);
    max_tex_ = mt > 2048 ? 2048 : mt < 64 ? 64 : mt;
    /* TTRV_GL_TILE: a smaller tile, to see the seams if there are any. */
    const char* tile = getenv("TTRV_GL_TILE");
    if (tile && atoi(tile) >= 16 && atoi(tile) < max_tex_)
        max_tex_ = pow2_at_least(atoi(tile));
    set_budget(0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glDisable(GL_BLEND);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    if (!dbl_) {
        glDrawBuffer(GL_FRONT);
        glReadBuffer(GL_FRONT);
    }
    if (major >= 2 && !getenv("TTRV_GL_NOSHADER"))
        make_shader();
    return true;
}

void GLPaint::make_shader()
{
#if !defined(__sgi)
    api_ = new Api;
    memset(api_, 0, sizeof(*api_));
    api_->create_shader = (PfnCreateShader)proc("glCreateShader");
    api_->shader_source = (PfnShaderSource)proc("glShaderSource");
    api_->compile_shader = (PfnCompileShader)proc("glCompileShader");
    api_->get_shaderiv = (PfnGetShaderiv)proc("glGetShaderiv");
    api_->create_program = (PfnCreateProgram)proc("glCreateProgram");
    api_->attach_shader = (PfnAttachShader)proc("glAttachShader");
    api_->link_program = (PfnLinkProgram)proc("glLinkProgram");
    api_->get_programiv = (PfnGetProgramiv)proc("glGetProgramiv");
    api_->use_program = (PfnUseProgram)proc("glUseProgram");
    api_->get_uniform_location = (PfnGetUniformLocation)proc("glGetUniformLocation");
    api_->uniform2f = (PfnUniform2f)proc("glUniform2f");
    api_->uniform1i = (PfnUniform1i)proc("glUniform1i");
    api_->delete_program = (PfnDeleteProgram)proc("glDeleteProgram");
    Api& a = *api_;
    if (!a.create_shader || !a.shader_source || !a.compile_shader || !a.get_shaderiv ||
        !a.create_program || !a.attach_shader || !a.link_program || !a.get_programiv ||
        !a.use_program || !a.get_uniform_location || !a.uniform2f || !a.uniform1i)
        return;
    unsigned int sh = a.create_shader(0x8B30);     /* GL_FRAGMENT_SHADER */
    if (!sh)
        return;
    const char* src = k_bicubic;
    a.shader_source(sh, 1, &src, 0);
    a.compile_shader(sh);
    int ok = 0;
    a.get_shaderiv(sh, 0x8B81, &ok);                /* GL_COMPILE_STATUS */
    if (!ok)
        return;
    unsigned int p = a.create_program();
    a.attach_shader(p, sh);
    a.link_program(p);
    a.get_programiv(p, 0x8B82, &ok);                /* GL_LINK_STATUS */
    if (!ok)
        return;
    prog_ = p;
    a.use_program(p);
    a.uniform1i(a.get_uniform_location(p, "tex"), 0);
    u_size_ = a.get_uniform_location(p, "size");
    a.use_program(0);
    char nm[260];
    snprintf(nm, sizeof(nm), "%.240s, bicubic", name_.c_str());
    name_ = nm;
#endif
}

void GLPaint::set_budget(long bytes)
{
    if (bytes > 0) {
        budget_ = bytes;
        return;
    }
    /* A VPro's renderer string ends in its memory ("VPRO/B/32"). It pages
     * textures from main memory, so the zoomed-in parts of an image four
     * times its size draw as fast as resident ones (measured on a V10:
     * a 12-megapixel photograph at 1:1 at 60 frames a second, and the
     * whole of it at 3 or 4). Elsewhere the board's memory is not known;
     * PC boards since 2010 have a gigabyte. */
    const char* r = name_.c_str();
    const char* slash = strrchr(r, '/');
    if (strncmp(r, "VPRO", 4) == 0 && slash && atoi(slash + 1) > 0)
        budget_ = (long)atoi(slash + 1) * 4 * 1024 * 1024;
    else
#if defined(__sgi)
        budget_ = 8L * 1024 * 1024;
#else
        budget_ = 256L * 1024 * 1024;
#endif
}

void GLPaint::set_font(XFontStruct* font)
{
    if (!ctx_ || !font || font->fid == font_)
        return;
    glXMakeCurrent(dpy_, win_, (GLXContext)ctx_);
    if (!font_lists_)
        font_lists_ = glGenLists(256);
    if (font_lists_) {
        glXUseXFont(font->fid, 0, 256, (int)font_lists_);
        font_ = font->fid;
    }
}

void GLPaint::release(Set* s)
{
    for (int i = 0; i < s->count; i++)
        glDeleteTextures(1, &s->tiles[i].id);
    free(s->tiles);
    s->tiles = 0;
    s->count = 0;
    s->key = "";
    s->failed = false;
}

void GLPaint::forget()
{
    if (!ctx_)
        return;
    glXMakeCurrent(dpy_, win_, (GLXContext)ctx_);
    release(&named_[0]);
    release(&named_[1]);
    release(&frame_);
}

void GLPaint::begin(int w, int h, unsigned long bg)
{
    if (!ctx_)
        return;
    if (glXGetCurrentContext() != (GLXContext)ctx_ || glXGetCurrentDrawable() != win_)
        glXMakeCurrent(dpy_, win_, (GLXContext)ctx_);
    w_ = w;
    h_ = h;
    bg_ = bg;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(((bg >> 16) & 255) / 255.0f, ((bg >> 8) & 255) / 255.0f, (bg & 255) / 255.0f,
                 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

/* The image (reduced to the budget) into tiles; frames ("" key) have no
 * mipmaps, being drawn 1:1. */
bool GLPaint::upload(Set* s, const ti::Image& im, const char* key)
{
    release(s);
    bool with_mips = *key != 0;
    s->key = key;
    s->bg = bg_;
    s->failed = true;           /* until it is all up */
    const ti::Image* src = &im;
    ti::Image reduced;
    int w = im.w, h = im.h;
    if (with_mips) {
        while (texture_bytes(w, h, max_tex_) > budget_ && w > 16 && h > 16) {
            w = w * 7 / 8;
            h = h * 7 / 8;
        }
        if (w != im.w) {
            if (!ti::scale(im, w, h, &reduced))
                return false;
            src = &reduced;
        }
    }
    s->w = w;
    s->h = h;
    s->scale = (double)w / im.w;
    Span* sx = (Span*)malloc(sizeof(Span) * (size_t)max_spans(w, max_tex_));
    Span* sy = (Span*)malloc(sizeof(Span) * (size_t)max_spans(h, max_tex_));
    int nx = sx ? split(w, max_tex_, sx) : 0, ny = sy ? split(h, max_tex_, sy) : 0;
    s->tiles = (Tile*)calloc((size_t)(nx * ny > 0 ? nx * ny : 1), sizeof(Tile));
    unsigned char* buf = (unsigned char*)malloc((size_t)max_tex_ * (size_t)max_tex_ * 4);
    unsigned char* half = with_mips && !gen_mipmap_
                              ? (unsigned char*)malloc((size_t)max_tex_ * (size_t)max_tex_)
                              : 0;
    if (!sx || !sy || !s->tiles || !buf || (with_mips && !gen_mipmap_ && !half)) {
        free(sx);
        free(sy);
        free(buf);
        free(half);
        return false;
    }
    int bg[3] = { (int)((bg_ >> 16) & 255), (int)((bg_ >> 8) & 255), (int)(bg_ & 255) };
    bool solid = opaque(*src);
    unsigned int wrap = edge_clamp_ ? GL_CLAMP_TO_EDGE : GL_CLAMP;
    while (glGetError() != GL_NO_ERROR) {
    }
    bool ok = true;
    for (int ty = 0; ty < ny && ok; ty++) {
        for (int tx = 0; tx < nx && ok; tx++) {
            Tile& t = s->tiles[s->count];
            t.x = sx[tx].at;
            t.y = sy[ty].at;
            t.w = sx[tx].n;
            t.h = sy[ty].n;
            t.tw = sx[tx].tex;
            t.th = sy[ty].tex;
            /* The texels: the tile's pixels and BORDER around. Inside an
             * opaque image they are the image's own, sent from where they
             * are; else copied, the image's edge repeated past it (and into
             * the padding to a power of two), over the background where it
             * is transparent. */
            int left = t.x - BORDER, top = t.y - BORDER;
            const unsigned char* level0 = buf;
            size_t stride = (size_t)t.tw * 4;
            if (solid && left >= 0 && top >= 0 && left + t.tw <= w && top + t.th <= h) {
                level0 = src->row(top) + left * 4;
                stride = (size_t)w * 4;
            } else {
                for (int y = 0; y < t.th; y++) {
                    int sy = top + y;
                    sy = sy < 0 ? 0 : sy >= h ? h - 1 : sy;
                    const unsigned char* row = src->row(sy);
                    unsigned char* o = buf + (size_t)y * (size_t)t.tw * 4;
                    int x = 0;
                    for (; x < t.tw && left + x < 0; x++)
                        memcpy(o + x * 4, row, 4);
                    int end = w - left < t.tw ? w - left : t.tw;
                    if (end > x) {
                        memcpy(o + x * 4, row + (left + x) * 4, (size_t)(end - x) * 4);
                        x = end;
                    }
                    for (; x < t.tw; x++)
                        memcpy(o + x * 4, row + (w - 1) * 4, 4);
                    if (!solid) {
                        for (x = 0; x < t.tw; x++, o += 4) {
                            int a = o[3];
                            if (a == 255)
                                continue;
                            for (int k = 0; k < 3; k++)
                                o[k] = (unsigned char)((o[k] * a + bg[k] * (255 - a) + 127) / 255);
                            o[3] = 255;
                        }
                    }
                }
            }
            glGenTextures(1, &t.id);
            s->count++;
            glBindTexture(GL_TEXTURE_2D, t.id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wrap);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wrap);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            with_mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            if (with_mips && gen_mipmap_)
                glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(stride / 4));
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, t.tw, t.th, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         level0);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            if (with_mips && !gen_mipmap_) {
                /* Level 1 from level 0 wherever it is, then back and forth
                 * between the two buffers. */
                int lw = t.tw, lh = t.th;
                const unsigned char* a = level0;
                unsigned char* b = level0 == buf ? half : buf;
                unsigned char* other = level0 == buf ? buf : half;
                for (int level = 1; lw > 1 || lh > 1; level++) {
                    halve(a, level == 1 ? stride : (size_t)lw * 4, lw, lh, b);
                    lw = lw > 1 ? lw / 2 : 1;
                    lh = lh > 1 ? lh / 2 : 1;
                    glTexImage2D(GL_TEXTURE_2D, level, GL_RGB8, lw, lh, 0, GL_RGBA,
                                 GL_UNSIGNED_BYTE, b);
                    a = b;
                    b = other;
                    other = (unsigned char*)a;
                }
            }
            if (glGetError() != GL_NO_ERROR)
                ok = false;
        }
    }
    free(sx);
    free(sy);
    free(buf);
    free(half);
    if (!ok) {
        release(s);
        s->key = key;
        s->failed = true;
        return false;
    }
    s->failed = false;
    return true;
}

void GLPaint::draw(const Set& s, double zoom, double x0, double y0)
{
    /* Window pixels per uploaded pixel. */
    double z = zoom / s.scale;
    GLint mag = GL_LINEAR;
    if (z >= 1.9999)
        mag = GL_NEAREST;
#if !defined(__sgi)
    bool cubic = false;
    if (z > 1.0001 && z < 1.9999 && prog_) {
        mag = GL_NEAREST;
        cubic = true;
    }
#endif
#if !defined(__sgi)
    if (cubic)
        api_->use_program(prog_);
#endif
    glEnable(GL_TEXTURE_2D);
    for (int i = 0; i < s.count; i++) {
        const Tile& t = s.tiles[i];
        double wx0 = (t.x / s.scale - x0) * zoom, wy0 = (t.y / s.scale - y0) * zoom;
        double wx1 = ((t.x + t.w) / s.scale - x0) * zoom, wy1 = ((t.y + t.h) / s.scale - y0) * zoom;
        if (wx1 <= 0 || wy1 <= 0 || wx0 >= w_ || wy0 >= h_)
            continue;
        glBindTexture(GL_TEXTURE_2D, t.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
#if !defined(__sgi)
        if (cubic)
            api_->uniform2f(u_size_, (float)t.tw, (float)t.th);
#endif
        float u0 = (float)BORDER / t.tw, v0 = (float)BORDER / t.th;
        float u1 = (float)(BORDER + t.w) / t.tw, v1 = (float)(BORDER + t.h) / t.th;
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0);
        glVertex2d(wx0, wy0);
        glTexCoord2f(u1, v0);
        glVertex2d(wx1, wy0);
        glTexCoord2f(u1, v1);
        glVertex2d(wx1, wy1);
        glTexCoord2f(u0, v1);
        glVertex2d(wx0, wy1);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
#if !defined(__sgi)
    if (cubic)
        api_->use_program(0);
#endif
}

bool GLPaint::image(const ti::Image& im, const char* key, double zoom, double x0, double y0)
{
    if (!ctx_ || im.empty())
        return false;
    /* Two named images, so going between the fitted one and the full
     * one (zoomed) uploads neither again. */
    Set* s = 0;
    if (*key) {
        for (int i = 0; i < 2 && !s; i++)
            if (strcmp(named_[i].key.c_str(), key) == 0 && named_[i].bg == bg_ &&
                (named_[i].failed || named_[i].count))
                s = &named_[i];
        if (!s) {
            s = named_[0].used <= named_[1].used ? &named_[0] : &named_[1];
            if (!upload(s, im, key)) {
                s->used = ++clock_;
                return false;
            }
        }
    } else {
        s = &frame_;
        if (!upload(s, im, key))
            return false;
    }
    s->used = ++clock_;
    if (s->failed)
        return false;
    /* Reduced, and zoomed in past what the reduction kept. */
    if (s->scale < 1 && zoom > s->scale * 1.0001)
        return false;
    draw(*s, zoom, x0, y0);
    return true;
}

void GLPaint::text(int x, int y, const char* s, int len, unsigned long rgb)
{
    if (!ctx_ || !font_lists_ || len <= 0)
        return;
    glColor3ub((GLubyte)(rgb >> 16), (GLubyte)(rgb >> 8), (GLubyte)rgb);
    glRasterPos2i(x, y);
    glListBase(font_lists_);
    glCallLists(len, GL_UNSIGNED_BYTE, s);
}

bool GLPaint::read(ti::Image* out)
{
    if (!ctx_ || w_ < 1 || h_ < 1 || !out->alloc(w_, h_))
        return false;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(dbl_ ? GL_BACK : GL_FRONT);
    glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, out->rgba);
    /* GL's rows go up. */
    size_t bpl = (size_t)w_ * 4;
    unsigned char* tmp = (unsigned char*)malloc(bpl);
    if (!tmp)
        return false;
    for (int y = 0; y < h_ / 2; y++) {
        memcpy(tmp, out->row(y), bpl);
        memcpy(out->row(y), out->row(h_ - 1 - y), bpl);
        memcpy(out->row(h_ - 1 - y), tmp, bpl);
    }
    free(tmp);
    return glGetError() == GL_NO_ERROR;
}

void GLPaint::finish()
{
    if (ctx_)
        glFinish();
}

void GLPaint::end()
{
    if (!ctx_)
        return;
    if (dbl_)
        glXSwapBuffers(dpy_, win_);
    else
        glFlush();
}

} /* namespace ttrv */

#else /* no GL */

namespace ttrv {

struct GLPaint::Api {
    int unused;
};

GLPaint::GLPaint()
    : dpy_(0), win_(0), ctx_(0), dbl_(false), accelerated_(false), edge_clamp_(false),
      gen_mipmap_(false), max_tex_(0), w_(0), h_(0), budget_(0), bg_(0), font_lists_(0),
      font_(0), clock_(0), prog_(0), u_size_(-1), api_(0)
{
}

GLPaint::~GLPaint()
{
}

bool GLPaint::choose_visual(Display*, int, XVisualInfo*)
{
    return false;
}

bool GLPaint::init(Display*, Window, const XVisualInfo&, char* why, size_t n)
{
    snprintf(why, n, "built without OpenGL");
    return false;
}

void GLPaint::set_budget(long) {}
void GLPaint::set_font(XFontStruct*) {}
void GLPaint::forget() {}
void GLPaint::begin(int, int, unsigned long) {}
bool GLPaint::image(const ti::Image&, const char*, double, double, double)
{
    return false;
}
void GLPaint::text(int, int, const char*, int, unsigned long) {}
bool GLPaint::read(ti::Image*)
{
    return false;
}
void GLPaint::finish() {}
void GLPaint::end() {}
bool GLPaint::upload(Set*, const ti::Image&, const char*)
{
    return false;
}
void GLPaint::release(Set*) {}
void GLPaint::draw(const Set&, double, double, double) {}
void GLPaint::make_shader() {}

} /* namespace ttrv */

#endif
