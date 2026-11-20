/*
 * ttrv's GL renderer against the soft one, and what it costs. Needs a
 * display with GLX; no input, so it runs on a machine whose screen is
 * someone else's.
 *
 *   ttrv_test_gl [-budget MB] [-size WxH] [image]
 *
 * Draws the image (a made-up one if none is given) at a series of zooms
 * in a window of its own, reads each frame back and compares it with
 * render_view's: 1:1 and the nearest-pixel zooms must match, the
 * filtered ones be close. Then times the upload, frames of panning at
 * each zoom in GL and in software, and the budget's fallback. Exit
 * status 1 if a comparison fails, 2 if there is no GL.
 */
#include "../glpaint.h"
#include "../render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace {

double now()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

/* A photograph's worth of detail: gradients, a fine checker, thin
 * diagonals and a disc with an alpha edge. */
void make_image(ti::Image* im, int w, int h)
{
    im->alloc(w, h);
    for (int y = 0; y < h; y++) {
        unsigned char* p = im->row(y);
        for (int x = 0; x < w; x++, p += 4) {
            int r = x * 255 / w, g = y * 255 / h, b = ((x / 7 + y / 5) & 1) ? 200 : 60;
            if ((x + y) % 97 == 0 || (x - y + 10000) % 89 == 0)
                r = g = b = 255;
            int dx = x - w * 3 / 4, dy = y - h / 4;
            int a = 255;
            if (dx * dx + dy * dy < (h / 6) * (h / 6)) {
                r = 250;
                g = 220;
                b = 40;
                a = (x & 8) ? 128 : 255;
            }
            p[0] = (unsigned char)r;
            p[1] = (unsigned char)g;
            p[2] = (unsigned char)b;
            p[3] = (unsigned char)a;
        }
    }
}

bool load(const char* path, ti::Image* im)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* d = (unsigned char*)malloc((size_t)(n > 0 ? n : 1));
    bool ok = d && fread(d, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    ti::Info info;
    char err[200];
    ok = ok && ti::decode(d, (size_t)n, im, &info, err, sizeof(err));
    free(d);
    return ok;
}

void save(const ti::Image& im, const char* path)
{
    unsigned char* d = 0;
    size_t n = 0;
    if (!ti::encode_png(im, 0, &d, &n))
        return;
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
    }
    free(d);
}

/* Peak signal to noise over RGB, in dB (99 for identical). */
double psnr(const ti::Image& a, const ti::Image& b)
{
    if (a.w != b.w || a.h != b.h)
        return 0;
    double se = 0;
    for (int y = 0; y < a.h; y++) {
        const unsigned char* p = a.row(y);
        const unsigned char* q = b.row(y);
        for (int x = 0; x < a.w * 4; x++) {
            if ((x & 3) == 3)
                continue;
            double d = (double)p[x] - q[x];
            se += d * d;
        }
    }
    if (se == 0)
        return 99;
    double mse = se / ((double)a.w * a.h * 3);
    double p = 10 * log10(255.0 * 255.0 / mse);
    return p > 99 ? 99 : p;
}

} /* namespace */

int main(int argc, char** argv)
{
    int W = 800, H = 600, budget_mb = 0;
    const char* path = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-budget") == 0 && i + 1 < argc)
            budget_mb = atoi(argv[++i]);
        else if (strcmp(argv[i], "-size") == 0 && i + 1 < argc)
            sscanf(argv[++i], "%dx%d", &W, &H);
        else
            path = argv[i];
    }
    ti::Image im;
    if (path ? !load(path, &im) : (make_image(&im, 2400, 1800), im.empty())) {
        fprintf(stderr, "test_gl: cannot read %s\n", path ? path : "(made-up image)");
        return 1;
    }
    Display* dpy = XOpenDisplay(0);
    if (!dpy) {
        fprintf(stderr, "test_gl: no display\n");
        return 2;
    }
    int screen = DefaultScreen(dpy);
    XVisualInfo vi;
    if (!ttrv::GLPaint::choose_visual(dpy, screen, &vi)) {
        fprintf(stderr, "test_gl: no GLX visual\n");
        return 2;
    }
    XSetWindowAttributes sa;
    sa.colormap = XCreateColormap(dpy, RootWindow(dpy, screen), vi.visual, AllocNone);
    sa.border_pixel = 0;
    sa.background_pixmap = None;
    sa.override_redirect = True;
    sa.event_mask = StructureNotifyMask;
    Window win = XCreateWindow(dpy, RootWindow(dpy, screen), 0, 0, (unsigned)W, (unsigned)H, 0,
                               vi.depth, InputOutput, vi.visual,
                               CWColormap | CWBorderPixel | CWBackPixmap | CWOverrideRedirect |
                                   CWEventMask,
                               &sa);
    XMapRaised(dpy, win);
    for (;;) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == MapNotify)
            break;
    }
    ttrv::GLPaint* glp = new ttrv::GLPaint;
    ttrv::GLPaint& gl = *glp;
    char why[200];
    if (!gl.init(dpy, win, vi, why, sizeof(why))) {
        fprintf(stderr, "test_gl: %s\n", why);
        return 2;
    }
    if (budget_mb)
        gl.set_budget((long)budget_mb * 1024 * 1024);
    printf("GL: %s, %s; visual 0x%lx depth %d; image %d x %d; window %d x %d\n", gl.name(),
           gl.accelerated() ? "accelerated" : "not accelerated", (unsigned long)vi.visualid,
           vi.depth, im.w, im.h, W, H);

    const unsigned long bg = 0x202020;
    int failures = 0;
    /* The upload, timed on its own. */
    double fit = (double)W / im.w < (double)H / im.h ? (double)W / im.w : (double)H / im.h;
    double t0 = now();
    gl.begin(W, H, bg);
    bool up = gl.image(im, "image", fit, im.w / 2.0 - W / (2 * fit), im.h / 2.0 - H / (2 * fit));
    gl.finish();
    gl.end();
    printf("upload: %.0f ms%s\n", (now() - t0) * 1000, up ? "" : " (not drawn: over the budget)");

    struct Case {
        double zoom;
        double min_psnr;
        const char* what;
    };
    Case cases[] = { { 0, 20, "fit" },        { 0.5, 20, "half" },    { 1, 60, "1:1" },
                     { 1.5, 20, "150%" },     { 2, 40, "2x" },        { 3, 40, "3x" },
                     { 0.13, 20, "13%" } };
    printf("%-6s %8s %10s %10s %10s\n", "zoom", "PSNR", "gl ms", "soft ms", "drawn by");
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        double z = cases[c].zoom > 0 ? cases[c].zoom : fit;
        /* Whole source pixels at the window's corner, so 1:1 is exact. */
        double x0 = floor(im.w / 2.0 - W / (2 * z)), y0 = floor(im.h / 2.0 - H / (2 * z));
        ti::Image got, want;
        gl.begin(W, H, bg);
        bool drawn = gl.image(im, "image", z, x0, y0);
        if (!drawn && ttrv::render_view(im, z, x0, y0, W, H, bg, &want))
            gl.image(want, "", 1, 0, 0);
        gl.read(&got);
        gl.end();
        ttrv::render_view(im, z, x0, y0, W, H, bg, &want);
        double p = psnr(got, want);
        bool ok = p >= cases[c].min_psnr;
        if (!ok) {
            failures++;
            /* TTRV_TEST_DUMP=dir: the two frames, to look at. */
            const char* dir = getenv("TTRV_TEST_DUMP");
            if (dir) {
                char fn[600];
                snprintf(fn, sizeof(fn), "%.500s/gl-%d.png", dir, (int)c);
                save(got, fn);
                snprintf(fn, sizeof(fn), "%.500s/soft-%d.png", dir, (int)c);
                save(want, fn);
            }
        }
        /* Frames of panning: three window pixels a frame. */
        const int N = 20;
        t0 = now();
        for (int i = 0; i < N; i++) {
            gl.begin(W, H, bg);
            if (!gl.image(im, "image", z, x0 + i * 3 / z, y0) &&
                ttrv::render_view(im, z, x0 + i * 3 / z, y0, W, H, bg, &want))
                gl.image(want, "", 1, 0, 0);
            gl.end();
        }
        gl.finish();
        double tg = (now() - t0) * 1000 / N;
        t0 = now();
        for (int i = 0; i < N / 4; i++)
            ttrv::render_view(im, z, x0 + i * 3 / z, y0, W, H, bg, &want);
        double ts = (now() - t0) * 1000 / (N / 4);
        printf("%-6s %8.1f %10.1f %10.1f %10s%s\n", cases[c].what, p, tg, ts,
               drawn ? "textures" : "soft", ok ? "" : "   FAILED");
    }

    /* Sizes at the tiles' edges, 1:1 and at 2x: exact. */
    static const int sizes[][2] = { { 1, 1 }, { 5, 3 }, { 1024, 300 }, { 1029, 77 }, { 60, 2047 } };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        ti::Image small, got, want;
        make_image(&small, sizes[i][0], sizes[i][1]);
        for (int z = 1; z <= 2; z++) {
            double x0 = -10, y0 = small.h > H / z ? small.h / 2 : -10;
            gl.begin(W, H, bg);
            char key[40];
            sprintf(key, "edge%d", (int)i);
            bool drawn = gl.image(small, key, z, x0, y0);
            gl.read(&got);
            gl.end();
            ttrv::render_view(small, z, x0, y0, W, H, bg, &want);
            double p = psnr(got, want);
            bool ok = drawn && p >= 60;
            if (!ok)
                failures++;
            printf("%d x %d at %dx: PSNR %.1f%s\n", small.w, small.h, z, p, ok ? "" : "   FAILED");
        }
    }

    /* A budget too small for the image: small, it still draws (reduced);
     * 1:1 asks for the soft frame. Not for an image that fits in it. */
    if (im.w * im.h < 1000000) {
        printf("ttrv gl: %d failures\n", failures);
        delete glp;
        XCloseDisplay(dpy);
        return failures ? 1 : 0;
    }
    gl.forget();
    gl.set_budget(1024 * 1024);
    gl.begin(W, H, bg);
    bool small_fit = gl.image(im, "image", 0.05, 0, 0);
    bool small_one = gl.image(im, "image", 1, 0, 0);
    gl.end();
    bool budget_ok = small_fit && !small_one;
    printf("1 MB budget: fitted %s, 1:1 %s%s\n", small_fit ? "drawn" : "not drawn",
           small_one ? "drawn" : "left to soft", budget_ok ? "" : "   FAILED");
    if (!budget_ok)
        failures++;
    printf("ttrv gl: %d failures\n", failures);
    delete glp;
    XCloseDisplay(dpy);
    return failures ? 1 : 0;
}
