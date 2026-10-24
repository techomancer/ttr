/*
 * sdf_lab: how a line of text looks drawn four ways, for choosing how to
 * draw glyphs from signed distance fields (tw/DESIGN.md section 4.6).
 *
 *   sdf_lab "-*-dejavu sans mono-medium-r-normal--%d-*-*-*-m-*-iso10646-1" 13 out.pgm
 *
 * The XLFD has a %d for the pixel size; the font must be scalable. Rows of
 * the output, each the text at that size:
 *   1. the font at the size, 1 bit (what tw draws now);
 *   2. the font at 4x, box-filtered down (dash's way);
 *   3. a distance field from the 4x rendering, sampled at 1x, edge
 *      smoothstep 0.5 px wide either side;
 *   4. the same, 0.75 px;
 *   5. a distance field from the 1x rendering scaled 4x by EPX (Scale2x
 *      twice): the hinted shape, diagonals smoothed; edge 0.5 px;
 *   6. the same, 0.35 px.
 * Grey, white on black. Needs a display (for the X server's fonts).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace {

const int K = 4;            /* supersampling */
const int SPREAD = 4;       /* distance field range, in 1x pixels */

/* text rendered 1 bit at the font's size: w x h, 0/1 */
unsigned char* render(Display* dpy, const char* xlfd, int px, const char* text, int* w, int* h,
                      int* ascent)
{
    char name[400];
    snprintf(name, sizeof(name), xlfd, px);
    XFontStruct* fs = XLoadQueryFont(dpy, name);
    if (!fs) {
        fprintf(stderr, "sdf_lab: no font %s\n", name);
        exit(1);
    }
    int n = (int)strlen(text);
    *w = XTextWidth(fs, text, n) + 2;
    *h = fs->ascent + fs->descent;
    *ascent = fs->ascent;
    Window root = DefaultRootWindow(dpy);
    Pixmap pm = XCreatePixmap(dpy, root, (unsigned)*w, (unsigned)*h, 1);
    XGCValues v;
    v.foreground = 0;
    v.font = fs->fid;
    GC gc = XCreateGC(dpy, pm, GCForeground | GCFont, &v);
    XFillRectangle(dpy, pm, gc, 0, 0, (unsigned)*w, (unsigned)*h);
    XSetForeground(dpy, gc, 1);
    XDrawString(dpy, pm, gc, 1, fs->ascent, text, n);
    XImage* im = XGetImage(dpy, pm, 0, 0, (unsigned)*w, (unsigned)*h, 1, XYPixmap);
    unsigned char* out = (unsigned char*)malloc((size_t)*w * (size_t)*h);
    for (int y = 0; y < *h; y++)
        for (int x = 0; x < *w; x++)
            out[y * *w + x] = XGetPixel(im, x, y) ? 1 : 0;
    XDestroyImage(im);
    XFreeGC(dpy, gc);
    XFreePixmap(dpy, pm);
    XFreeFont(dpy, fs);
    return out;
}

/* EPX / Scale2x: w x h 0/1 into 2w x 2h. */
unsigned char* scale2x(const unsigned char* in, int w, int h)
{
    unsigned char* o = (unsigned char*)malloc((size_t)w * (size_t)h * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int P = in[y * w + x];
            int A = y > 0 ? in[(y - 1) * w + x] : 0;
            int B = x < w - 1 ? in[y * w + x + 1] : 0;
            int C = x > 0 ? in[y * w + x - 1] : 0;
            int D = y < h - 1 ? in[(y + 1) * w + x] : 0;
            int e0 = P, e1 = P, e2 = P, e3 = P;
            if (C == A && C != D && A != B) e0 = A;
            if (A == B && A != C && B != D) e1 = B;
            if (D == C && D != B && C != A) e2 = C;
            if (B == D && B != A && D != C) e3 = D;
            o[(2 * y) * 2 * w + 2 * x] = (unsigned char)e0;
            o[(2 * y) * 2 * w + 2 * x + 1] = (unsigned char)e1;
            o[(2 * y + 1) * 2 * w + 2 * x] = (unsigned char)e2;
            o[(2 * y + 1) * 2 * w + 2 * x + 1] = (unsigned char)e3;
        }
    return o;
}

/* Signed distance in 1x pixels from the centre of 1x pixel (x, y) to the
 * edge of a K-times mask (positive inside); dy aligns the rows. */
double sdist(const unsigned char* bk, int wk, int hk, int x, int y, int dy)
{
    double cx = (x + 0.5) * K, cy = (y + 0.5) * K + dy;
    int icx = (int)cx, icy = (int)cy;
    bool inside = icx >= 0 && icx < wk && icy >= 0 && icy < hk && bk[icy * wk + icx];
    double best = SPREAD * K;
    int r = SPREAD * K;
    for (int sy = icy - r; sy <= icy + r; sy++)
        for (int sx = icx - r; sx <= icx + r; sx++) {
            bool in = sx >= 0 && sx < wk && sy >= 0 && sy < hk && bk[sy * wk + sx];
            if (in == inside)
                continue;
            double d = sqrt((sx + 0.5 - cx) * (sx + 0.5 - cx) + (sy + 0.5 - cy) * (sy + 0.5 - cy));
            if (d < best)
                best = d;
        }
    return (best - 0.5) / K * (inside ? 1 : -1);
}

double smoothstep(double a, double b, double x)
{
    double t = (x - a) / (b - a);
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    return t * t * (3 - 2 * t);
}

} /* namespace */

int main(int argc, char** argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: sdf_lab XLFD-with-%%d px out.pgm [text]\n");
        return 2;
    }
    const char* xlfd = argv[1];
    int px = atoi(argv[2]);
    const char* text = argc > 4 ? argv[4] : "The quick brown fox: 0123 {}[]() @#&% gjpqy";
    Display* dpy = XOpenDisplay(0);
    if (!dpy)
        return 1;
    int w1, h1, a1, wk, hk, ak;
    unsigned char* b1 = render(dpy, xlfd, px, text, &w1, &h1, &a1);
    unsigned char* bk = render(dpy, xlfd, px * K, text, &wk, &hk, &ak);
    /* The 1x grid the others are drawn on: the 1x rendering's. Map 1x
     * pixel (x, y) to the 4x pixels [x*K, x*K+K) after aligning the
     * baselines. */
    int W = w1, H = h1;
    int dy = ak - a1 * K;
    const int ROWS = 6;
    unsigned char* out = (unsigned char*)calloc((size_t)W * (size_t)H * ROWS, 1);
    unsigned char* e2 = scale2x(b1, w1, h1);
    unsigned char* e4 = scale2x(e2, w1 * 2, h1 * 2);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            /* 1. bitmap */
            out[(0 * H + y) * W + x] = b1[y * w1 + x] ? 255 : 0;
            /* 2. box filter of the 4x */
            int sum = 0;
            for (int j = 0; j < K; j++)
                for (int i = 0; i < K; i++) {
                    int sx = x * K + i, sy = y * K + j + dy;
                    if (sx >= 0 && sx < wk && sy >= 0 && sy < hk)
                        sum += bk[sy * wk + sx];
                }
            out[(1 * H + y) * W + x] = (unsigned char)(sum * 255 / (K * K));
            /* 3, 4: signed distance (in 1x px) at the pixel's centre to
             * the 4x edge, positive inside. */
            double cx = (x + 0.5) * K, cy = (y + 0.5) * K + dy;
            int icx = (int)cx, icy = (int)cy;
            bool inside = icx >= 0 && icx < wk && icy >= 0 && icy < hk && bk[icy * wk + icx];
            double best = SPREAD * K;
            int r = SPREAD * K;
            for (int sy = icy - r; sy <= icy + r; sy++)
                for (int sx = icx - r; sx <= icx + r; sx++) {
                    bool in = sx >= 0 && sx < wk && sy >= 0 && sy < hk && bk[sy * wk + sx];
                    if (in == inside)
                        continue;
                    double d = sqrt((sx + 0.5 - cx) * (sx + 0.5 - cx) + (sy + 0.5 - cy) * (sy + 0.5 - cy));
                    if (d < best)
                        best = d;
                }
            /* The edge lies half a 4x pixel from the nearest opposite pixel. */
            double dist = (best - 0.5) / K * (inside ? 1 : -1);
            out[(2 * H + y) * W + x] = (unsigned char)(255 * smoothstep(-0.5, 0.5, dist));
            out[(3 * H + y) * W + x] = (unsigned char)(255 * smoothstep(-0.75, 0.75, dist));
            double de = sdist(e4, w1 * K, h1 * K, x, y, 0);
            out[(4 * H + y) * W + x] = (unsigned char)(255 * smoothstep(-0.5, 0.5, de));
            out[(5 * H + y) * W + x] = (unsigned char)(255 * smoothstep(-0.35, 0.35, de));
        }
    FILE* f = fopen(argv[3], "wb");
    fprintf(f, "P5\n%d %d\n255\n", W, H * ROWS);
    fwrite(out, 1, (size_t)W * (size_t)H * ROWS, f);
    fclose(f);
    printf("%dx%d per row (4x source %dx%d)\n", W, H, wk, hk);
    return 0;
}
