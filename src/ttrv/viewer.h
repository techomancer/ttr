/*
 * ttrv's viewer (ttrv/DESIGN.md section 3): one image at a time, fitted
 * or zoomed, in the main window in place of the browser or full screen
 * in a shell of its own. Space and Backspace step through the folder in
 * the browser's order; the next images are decoded ahead, so stepping
 * shows them at once. It never shows an empty window while it decodes:
 * any size of the image the cache has is drawn, scaled, until the right
 * one comes.
 */
#ifndef TTRV_VIEWER_H
#define TTRV_VIEWER_H

#include "filelist.h"
#include "imageview.h"
#include "loader.h"

#include <Xm/Xm.h>

namespace ttrv {

class ViewerHost {
public:
    virtual ~ViewerHost() {}
    virtual int viewer_count() = 0;
    virtual const Row* viewer_row(int i) = 0;
    virtual ttr::Str viewer_path(int i) = 0;
    /* The cached decode of row i fitted in w x h (0 x 0: full size), or
     * 0. */
    virtual const Result* viewer_image(int i, int w, int h) = 0;
    /* The largest cached decode of row i, or 0. */
    virtual const Result* viewer_best(int i) = 0;
    /* What the viewer wants loaded changed. */
    virtual void viewer_changed() = 0;
    /* Back to the browser, on row i. */
    virtual void viewer_closed(int i) = 0;
    /* Ctrl+Down / Ctrl+Up: the next or previous folder. */
    virtual void viewer_folder(int step) = 0;
    /* A line for the status bar (the viewer in the window). */
    virtual void viewer_status(const char* text) = 0;
};

class Viewer : public ImageViewHost {
public:
    /* box: where the viewer goes in the main window (managed by the
     * host); top: the application's shell (for the full-screen one). */
    Viewer(Widget box, Widget top, ViewerHost* host);
    ~Viewer();

    bool active() const { return active_; }
    bool fullscreen() const { return full_; }
    int index() const { return index_; }
    /* Show row i (an image), in the window or full screen. */
    void open(int i, bool fullscreen);
    /* The rows changed (another folder): show row i, or "no images". */
    void reset(int i);
    void close();
    void set_fullscreen(bool on);
    /* How the viewer draws ("soft", the GL board), 0 before it has. */
    const char* renderer() const { return view()->renderer(); }

    /* The wants, most important first (appended). */
    void wants(ttr::Vector<Want*>* out);
    /* A decode arrived: redraw if it is the one shown. */
    void loaded(const Result* r);
    /* The path shown, or "": the cache keeps it. */
    ttr::Str current_path();

    /* ImageViewHost */
    void view_resized(ImageView* v);

    /* Callbacks (public for the C trampolines). */
    void event(XEvent* ev);

private:
    Viewer(const Viewer&);
    Viewer& operator=(const Viewer&);

    ImageView* view() const { return full_ ? full_view_ : win_view_; }
    void fit_box(int* bw, int* bh) const;
    bool dims(int* rw, int* rh) const;
    double fit_zoom() const;
    void step(int d);
    void go_to(int i);
    void zoom_to(double z, int px, int py);
    void zoom_step(int d, int px, int py);
    void clamp();
    void pan(double dx, double dy);
    void redraw();
    const ti::Image* rotated(const ti::Image* im);
    bool key(XKeyEvent* ev);
    void make_fullscreen();

    Widget box_, top_;
    ViewerHost* host_;
    ImageView* win_view_;
    Widget fs_shell_;
    ImageView* full_view_;
    bool active_, full_;
    int index_;
    int dir_;                   /* the last step: 1 forward, -1 back */
    double zoom_;               /* window pixels per image pixel; 0: fit */
    double cx_, cy_;            /* the image point in the window's centre */
    int rot_;                   /* quarter turns clockwise */
    bool overlay_;
    unsigned long bg_;
    const ti::Image* rot_src_;  /* rot_img_ is rot_src_ turned rot_q_ */
    int rot_q_;
    ti::Image rot_img_;
    bool dragging_;
    int drag_x_, drag_y_;
    double drag_cx_, drag_cy_;
    Time click_at_;
};

} /* namespace ttrv */

#endif
