/* ttrv's viewer (viewer.h). */
#include "viewer.h"

#include "render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Shell.h>
#include <X11/Xutil.h>
#include <Xm/Form.h>

namespace ttrv {

namespace {

/* Zoom levels, as ACDSee stepped through them. */
const double k_levels[] = { 1.0 / 16, 1.0 / 12, 1.0 / 10, 1.0 / 8, 1.0 / 6, 1.0 / 5, 1.0 / 4,
                            1.0 / 3, 1.0 / 2, 2.0 / 3, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0,
                            12.0, 16.0, 24.0, 32.0 };
const int k_nlevels = (int)(sizeof(k_levels) / sizeof(k_levels[0]));

void event_cb(Widget, XtPointer closure, XEvent* ev, Boolean*)
{
    ((Viewer*)closure)->event(ev);
}

const EventMask k_events = KeyPressMask | ButtonPressMask | ButtonReleaseMask | Button1MotionMask;

} /* namespace */

Viewer::Viewer(Widget box, Widget top, ViewerHost* host)
    : box_(box), top_(top), host_(host), win_view_(0), fs_shell_(0), full_view_(0),
      active_(false), full_(false), index_(-1), dir_(1), zoom_(0), cx_(0), cy_(0), rot_(0),
      overlay_(false), bg_(0x000000), rot_src_(0), rot_q_(0), dragging_(false), drag_x_(0),
      drag_y_(0), drag_cx_(0), drag_cy_(0), click_at_(0)
{
    win_view_ = new ImageView(box, "viewer", this, true);
    XtVaSetValues(win_view_->widget(), XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment,
                  XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment,
                  XmATTACH_FORM, XmNtraversalOn, True, (char*)0);
    win_view_->set_background(bg_);
    XtAddEventHandler(win_view_->widget(), k_events, False, event_cb, this);
}

Viewer::~Viewer()
{
    delete win_view_;
    delete full_view_;
    if (fs_shell_)
        XtDestroyWidget(fs_shell_);
}

ttr::Str Viewer::current_path()
{
    if (!active_ || index_ < 0)
        return ttr::Str("");
    return host_->viewer_path(index_);
}

/* The box the fitted decode is asked for: the window, turned with the
 * image. */
void Viewer::fit_box(int* bw, int* bh) const
{
    int w, h;
    view()->size(&w, &h);
    *bw = rot_ & 1 ? h : w;
    *bh = rot_ & 1 ? w : h;
}

/* The image's size as shown (turned), from its header or a decode. */
bool Viewer::dims(int* rw, int* rh) const
{
    const Row* r = host_->viewer_row(index_);
    int w = 0, h = 0;
    if (r && r->probed && r->info.width) {
        w = r->info.width;
        h = r->info.height;
    } else {
        const Result* b = host_->viewer_best(index_);
        if (b && b->info.width) {
            w = b->info.width;
            h = b->info.height;
        }
    }
    if (!w || !h)
        return false;
    *rw = rot_ & 1 ? h : w;
    *rh = rot_ & 1 ? w : h;
    return true;
}

double Viewer::fit_zoom() const
{
    int rw, rh, w, h;
    if (!dims(&rw, &rh))
        return 1.0;
    view()->size(&w, &h);
    double z = (double)w / rw;
    if ((double)h / rh < z)
        z = (double)h / rh;
    return z > 1.0 ? 1.0 : z;
}

void Viewer::open(int i, bool fullscreen)
{
    active_ = true;
    if (fullscreen != full_)
        set_fullscreen(fullscreen);
    go_to(i);
    XmProcessTraversal(view()->widget(), XmTRAVERSE_CURRENT);
}

void Viewer::reset(int i)
{
    go_to(i);
}

void Viewer::close()
{
    if (!active_)
        return;
    if (full_)
        set_fullscreen(false);
    active_ = false;
    rot_src_ = 0;
    rot_img_.clear();
    win_view_->show(0);
    host_->viewer_closed(index_);
}

void Viewer::make_fullscreen()
{
    if (fs_shell_)
        return;
    Display* dpy = XtDisplay(top_);
    Screen* scr = XtScreen(top_);
    fs_shell_ = XtVaAppCreateShell("ttrvFullScreen", "TTRv", topLevelShellWidgetClass, dpy,
                                   XmNx, 0, XmNy, 0, XmNwidth, WidthOfScreen(scr), XmNheight,
                                   HeightOfScreen(scr), XmNmwmDecorations, 0, XmNtitle, "ttrv",
                                   XmNdeleteResponse, XmDO_NOTHING, (char*)0);
    Widget form = XtVaCreateManagedWidget("form", xmFormWidgetClass, fs_shell_, (char*)0);
    full_view_ = new ImageView(form, "viewer", this, true);
    XtVaSetValues(full_view_->widget(), XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment,
                  XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment,
                  XmATTACH_FORM, XmNtraversalOn, True, (char*)0);
    full_view_->set_background(bg_);
    XtAddEventHandler(full_view_->widget(), k_events, False, event_cb, this);
    XtRealizeWidget(fs_shell_);
    /* EWMH full screen before it is mapped (ttr, other modern window
     * managers); the geometry and no decorations for the rest (4Dwm). */
    Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom fs = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    XChangeProperty(dpy, XtWindow(fs_shell_), state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char*)&fs, 1);
    XSizeHints hints;
    memset(&hints, 0, sizeof(hints));
    hints.flags = USPosition | USSize;
    hints.width = WidthOfScreen(scr);
    hints.height = HeightOfScreen(scr);
    XSetWMNormalHints(dpy, XtWindow(fs_shell_), &hints);
}

void Viewer::set_fullscreen(bool on)
{
    if (on == full_)
        return;
    if (on) {
        make_fullscreen();
        full_ = true;
        XtPopup(fs_shell_, XtGrabNone);
        XmProcessTraversal(full_view_->widget(), XmTRAVERSE_CURRENT);
    } else {
        full_ = false;
        full_view_->show(0);
        XtPopdown(fs_shell_);
        XmProcessTraversal(win_view_->widget(), XmTRAVERSE_CURRENT);
    }
    rot_src_ = 0;
    zoom_ = 0;
    host_->viewer_changed();
    redraw();
}

void Viewer::go_to(int i)
{
    index_ = i;
    zoom_ = 0;
    rot_ = 0;
    rot_src_ = 0;
    rot_img_.clear();
    int rw, rh;
    if (dims(&rw, &rh)) {
        cx_ = rw / 2.0;
        cy_ = rh / 2.0;
    }
    host_->viewer_changed();
    redraw();
}

/* The next (d > 0) or previous image in the browser's order. */
void Viewer::step(int d)
{
    int n = host_->viewer_count();
    for (int j = index_ + d; j >= 0 && j < n; j += d) {
        const Row* r = host_->viewer_row(j);
        if (r && r->image) {
            dir_ = d > 0 ? 1 : -1;
            go_to(j);
            return;
        }
    }
}

void Viewer::clamp()
{
    int rw, rh, w, h;
    if (!dims(&rw, &rh) || zoom_ <= 0)
        return;
    view()->size(&w, &h);
    double hw = w / (2 * zoom_), hh = h / (2 * zoom_);
    if (rw * zoom_ <= w)
        cx_ = rw / 2.0;
    else if (cx_ < hw)
        cx_ = hw;
    else if (cx_ > rw - hw)
        cx_ = rw - hw;
    if (rh * zoom_ <= h)
        cy_ = rh / 2.0;
    else if (cy_ < hh)
        cy_ = hh;
    else if (cy_ > rh - hh)
        cy_ = rh - hh;
}

/* A new zoom, keeping the image point under the window point (px, py)
 * where it is. */
void Viewer::zoom_to(double z, int px, int py)
{
    int w, h;
    view()->size(&w, &h);
    double old = zoom_ > 0 ? zoom_ : fit_zoom();
    if (zoom_ <= 0) {
        int rw, rh;
        if (dims(&rw, &rh)) {
            cx_ = rw / 2.0;
            cy_ = rh / 2.0;
        }
    }
    double ix = cx_ + (px - w / 2.0) / old, iy = cy_ + (py - h / 2.0) / old;
    zoom_ = z;
    cx_ = ix - (px - w / 2.0) / z;
    cy_ = iy - (py - h / 2.0) / z;
    clamp();
    host_->viewer_changed();
    redraw();
}

void Viewer::zoom_step(int d, int px, int py)
{
    double z = zoom_ > 0 ? zoom_ : fit_zoom();
    double next = z;
    if (d > 0) {
        for (int i = 0; i < k_nlevels; i++)
            if (k_levels[i] > z * 1.001) {
                next = k_levels[i];
                break;
            }
    } else {
        for (int i = k_nlevels - 1; i >= 0; i--)
            if (k_levels[i] < z * 0.999) {
                next = k_levels[i];
                break;
            }
    }
    zoom_to(next, px, py);
}

void Viewer::pan(double dx, double dy)
{
    cx_ += dx;
    cy_ += dy;
    clamp();
    redraw();
}

const ti::Image* Viewer::rotated(const ti::Image* im)
{
    if (!rot_ || !im)
        return im;
    if (rot_src_ != im || rot_q_ != rot_ || rot_img_.empty()) {
        if (!rotate(*im, rot_, &rot_img_))
            return im;
        rot_src_ = im;
        rot_q_ = rot_;
    }
    return &rot_img_;
}

void Viewer::redraw()
{
    if (!active_)
        return;
    ImageView* v = view();
    int w, h;
    v->size(&w, &h);
    const Row* row = host_->viewer_row(index_);
    if (!row || !row->image) {
        v->show(0);
        v->set_text("No images in this folder.");
        host_->viewer_status("No images in this folder.");
        return;
    }
    int bw, bh;
    fit_box(&bw, &bh);
    const Result* fit = host_->viewer_image(index_, bw, bh);
    const Result* full = host_->viewer_image(index_, 0, 0);
    const Result* best = host_->viewer_best(index_);
    char text[600];
    text[0] = 0;
    const char* note = "";
    int rw = 0, rh = 0;
    bool known = dims(&rw, &rh);
    double z = zoom_ > 0 ? zoom_ : fit_zoom();
    /* The image's name for GL's textures: the file, the decode's size and
     * the turn. */
    char key[1200];
    if (zoom_ <= 0 && fit && !fit->image.empty()) {
        /* Fitted: as decoded, turned. */
        snprintf(key, sizeof(key), "%.1100s|%dx%d|%d", fit->path.c_str(), fit->image.w,
                 fit->image.h, rot_);
        v->show(rotated(&fit->image), key);
    } else {
        /* Zoomed, or fitted from another size until the fitted one
         * comes: the best source there is, drawn at the zoom. */
        const Result* src = full && !full->image.empty() ? full : 0;
        if (!src && fit && !fit->image.empty())
            src = fit;
        if (!src || (best && !best->image.empty() && best->image.w > src->image.w))
            src = best && !best->image.empty() ? best : src;
        /* Zoomed out: the fitted decode has all the pixels shown, and is
         * smaller to render from (and to keep in textures). */
        if (known && zoom_ > 0 && fit && !fit->image.empty() &&
            z <= (double)(rot_ & 1 ? fit->image.h : fit->image.w) / rw * 1.0001)
            src = fit;
        if (src && known) {
            const ti::Image* im = rotated(&src->image);
            double s = (double)im->w / rw;      /* source pixels per image pixel */
            double cx = zoom_ > 0 ? cx_ : rw / 2.0, cy = zoom_ > 0 ? cy_ : rh / 2.0;
            double x0 = (cx - w / (2 * z)) * s, y0 = (cy - h / (2 * z)) * s;
            snprintf(key, sizeof(key), "%.1100s|%dx%d|%d", src->path.c_str(), src->image.w,
                     src->image.h, rot_);
            v->show_at(im, key, z / s, x0, y0);
            if (zoom_ <= 0 ? !fit : (z > s * 1.01 && src != full))
                note = "Decoding...";
        } else {
            v->show(0);
            const Result* failed = fit ? fit : full;
            if (failed && failed->image.empty())
                note = failed->err ? strerror(failed->err) : failed->msg;
            else
                note = "Decoding...";
        }
    }
    int pos = 0, count = 0;
    for (int i = 0; i < host_->viewer_count(); i++) {
        const Row* r = host_->viewer_row(i);
        if (r && r->image) {
            count++;
            if (i <= index_)
                pos = count;
        }
    }
    /* The status bar's first field (the others show the file), and the
     * overlay's line with the file too. */
    char status[200];
    if (known)
        snprintf(status, sizeof(status), "%d of %d   %d%%%s%.150s", pos, count,
                 (int)(z * 100 + 0.5), *note ? "   " : "", note);
    else
        snprintf(status, sizeof(status), "%d of %d%s%.150s", pos, count, *note ? "   " : "", note);
    char shot[100] = "";
    if (row->exif)
        ti::exif_summary(*row->exif, shot, sizeof(shot));
    if (overlay_ && known)
        snprintf(text, sizeof(text), "%.300s   %d x %d   %.200s%s%.90s", row->name.c_str(),
                 rot_ & 1 ? rh : rw, rot_ & 1 ? rw : rh, status, shot[0] ? "   " : "", shot);
    else if (overlay_)
        snprintf(text, sizeof(text), "%.300s   %.200s", row->name.c_str(), status);
    else
        snprintf(text, sizeof(text), "%s", note);
    v->set_text(text);
    host_->viewer_status(status);
}

void Viewer::wants(ttr::Vector<Want*>* out)
{
    if (!active_ || index_ < 0)
        return;
    int bw, bh;
    fit_box(&bw, &bh);
    if (bw < 8 || bh < 8)
        return;
    const Row* row = host_->viewer_row(index_);
    if (row && row->image) {
        if (!host_->viewer_image(index_, bw, bh))
            out->push_back(new Want(JOB_IMAGE, host_->viewer_path(index_).c_str(), bw, bh));
        /* Zoomed past what the fitted decode has: the whole image. */
        const Result* fit = host_->viewer_image(index_, bw, bh);
        int rw, rh;
        if (zoom_ > 0 && dims(&rw, &rh) && !host_->viewer_image(index_, 0, 0)) {
            int have = fit && !fit->image.empty() ? (rot_ & 1 ? fit->image.h : fit->image.w) : 0;
            if (zoom_ * rw > have * 1.01)
                out->push_back(new Want(JOB_IMAGE, host_->viewer_path(index_).c_str(), 0, 0));
        }
    }
    /* Ahead: two in the direction of travel, one behind. */
    int n = host_->viewer_count();
    int ahead = 0, behind = 0;
    for (int j = index_ + dir_; j >= 0 && j < n && ahead < 2; j += dir_) {
        const Row* r = host_->viewer_row(j);
        if (r && r->image) {
            ahead++;
            if (!host_->viewer_image(j, bw, bh))
                out->push_back(new Want(JOB_IMAGE, host_->viewer_path(j).c_str(), bw, bh));
        }
    }
    for (int j = index_ - dir_; j >= 0 && j < n && behind < 1; j -= dir_) {
        const Row* r = host_->viewer_row(j);
        if (r && r->image) {
            behind++;
            if (!host_->viewer_image(j, bw, bh))
                out->push_back(new Want(JOB_IMAGE, host_->viewer_path(j).c_str(), bw, bh));
        }
    }
}

void Viewer::loaded(const Result* r)
{
    if (!active_ || index_ < 0)
        return;
    if (strcmp(r->path.c_str(), host_->viewer_path(index_).c_str()) != 0)
        return;
    if (zoom_ <= 0) {
        int rw, rh;
        if (dims(&rw, &rh)) {
            cx_ = rw / 2.0;
            cy_ = rh / 2.0;
        }
    }
    redraw();
}

void Viewer::view_resized(ImageView* v)
{
    if (!active_ || v != view())
        return;
    clamp();
    host_->viewer_changed();
    redraw();
}

bool Viewer::key(XKeyEvent* ev)
{
    char buf[8];
    KeySym ks = 0;
    XLookupString(ev, buf, sizeof(buf), &ks, 0);
    bool ctrl = (ev->state & ControlMask) != 0;
    bool alt = (ev->state & Mod1Mask) != 0;
    int w, h;
    view()->size(&w, &h);
    int rw = 0, rh = 0;
    dims(&rw, &rh);
    double z = zoom_ > 0 ? zoom_ : fit_zoom();
    bool wide = zoom_ > 0 && rw * z > w + 1, tall = zoom_ > 0 && rh * z > h + 1;
    switch (ks) {
    case XK_space: case XK_Next: case XK_KP_Next:
        step(1);
        return true;
    case XK_BackSpace: case XK_Prior: case XK_KP_Prior:
        step(-1);
        return true;
    case XK_Right: case XK_KP_Right:
        if (wide)
            pan(w / 8.0 / z, 0);
        else
            step(1);
        return true;
    case XK_Left: case XK_KP_Left:
        if (wide)
            pan(-w / 8.0 / z, 0);
        else
            step(-1);
        return true;
    case XK_Down: case XK_KP_Down:
        if (ctrl)
            host_->viewer_folder(1);
        else if (tall)
            pan(0, h / 8.0 / z);
        return true;
    case XK_Up: case XK_KP_Up:
        if (ctrl)
            host_->viewer_folder(-1);
        else if (tall)
            pan(0, -h / 8.0 / z);
        return true;
    case XK_Home: case XK_KP_Home: {
        int n = host_->viewer_count();
        for (int j = 0; j < n; j++)
            if (host_->viewer_row(j) && host_->viewer_row(j)->image) {
                dir_ = 1;
                go_to(j);
                break;
            }
        return true;
    }
    case XK_End: case XK_KP_End:
        for (int j = host_->viewer_count() - 1; j >= 0; j--)
            if (host_->viewer_row(j) && host_->viewer_row(j)->image) {
                dir_ = -1;
                go_to(j);
                break;
            }
        return true;
    case XK_Return: case XK_KP_Enter:
        if (alt)
            set_fullscreen(!full_);
        else
            close();
        return true;
    case XK_Escape:
        close();
        return true;
    case XK_f: case XK_F:
        set_fullscreen(!full_);
        return true;
    case XK_plus: case XK_equal: case XK_KP_Add:
        zoom_step(1, w / 2, h / 2);
        return true;
    case XK_minus: case XK_KP_Subtract:
        zoom_step(-1, w / 2, h / 2);
        return true;
    case XK_asterisk: case XK_KP_Multiply: case XK_slash: case XK_KP_Divide:
        zoom_to(1.0, w / 2, h / 2);
        return true;
    case XK_period: case XK_z: case XK_Z: case XK_KP_Decimal:
        zoom_ = 0;
        host_->viewer_changed();
        redraw();
        return true;
    case XK_bracketright: case XK_bracketleft:
        rot_ = (rot_ + (ks == XK_bracketright ? 1 : 3)) & 3;
        rot_src_ = 0;
        if (zoom_ > 0) {
            double t = cx_;
            cx_ = cy_;
            cy_ = t;
            clamp();
        }
        host_->viewer_changed();
        redraw();
        return true;
    case XK_i: case XK_I:
        overlay_ = !overlay_;
        redraw();
        return true;
    default:
        return false;
    }
}

void Viewer::event(XEvent* ev)
{
    if (!active_)
        return;
    if (ev->type == KeyPress) {
        key(&ev->xkey);
        return;
    }
    if (ev->type == ButtonPress) {
        XButtonEvent* b = &ev->xbutton;
        XmProcessTraversal(view()->widget(), XmTRAVERSE_CURRENT);
        if (b->button == 4 || b->button == 5) {
            zoom_step(b->button == 4 ? 1 : -1, b->x, b->y);
            return;
        }
        if (b->button != 1)
            return;
        if (b->time - click_at_ < 400) {
            click_at_ = 0;
            set_fullscreen(!full_);
            return;
        }
        click_at_ = b->time;
        dragging_ = zoom_ > 0;
        drag_x_ = b->x;
        drag_y_ = b->y;
        drag_cx_ = cx_;
        drag_cy_ = cy_;
        return;
    }
    if (ev->type == MotionNotify && dragging_) {
        double z = zoom_ > 0 ? zoom_ : 1.0;
        cx_ = drag_cx_ - (ev->xmotion.x - drag_x_) / z;
        cy_ = drag_cy_ - (ev->xmotion.y - drag_y_) / z;
        clamp();
        redraw();
        return;
    }
    if (ev->type == ButtonRelease && ev->xbutton.button == 1)
        dragging_ = false;
}

} /* namespace ttrv */
