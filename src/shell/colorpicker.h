/*
 * A color chooser: red, green and blue sliders, a hex field, a swatch
 * and a row of basic colors. Motif 2.1 has no color selector widget
 * (XmColorSelector is Motif 2.2), so this is ours. The dialog is made on
 * first use as a child of the given parent and goes away with it.
 */
#ifndef TTR_SHELL_COLORPICKER_H
#define TTR_SHELL_COLORPICKER_H

#include "core/color.h"

#include <X11/Intrinsic.h>

namespace ttr {

/* A pixel for 'c' in the default colormap, freeing *old first if there
 * is one (*have). On 8-bit displays colors are scarce. */
Pixel alloc_color(Display* dpy, const Rgb& c, Pixel* old, bool* have);

class ColorPicker {
public:
    typedef void (*Chosen)(void* closure, const Rgb& c);

    ColorPicker(Chosen chosen, void* closure);
    ~ColorPicker();
    void show(Widget parent, const char* title, const Rgb& start);

private:
    ColorPicker(const ColorPicker&);
    ColorPicker& operator=(const ColorPicker&);

    void build(Widget parent);
    void set(const Rgb& c, bool from_text);
    void free_colors();

    static void scale_cb(Widget w, XtPointer closure, XtPointer call);
    static void hex_cb(Widget w, XtPointer closure, XtPointer call);
    static void basic_cb(Widget w, XtPointer closure, XtPointer call);
    static void ok_cb(Widget w, XtPointer closure, XtPointer call);
    static void cancel_cb(Widget w, XtPointer closure, XtPointer call);
    static void destroyed_cb(Widget w, XtPointer closure, XtPointer call);

    Chosen chosen_;
    void* closure_;
    Widget dialog_;
    Widget swatch_;
    Widget scales_[3];
    Widget hex_;
    Rgb color_;
    bool updating_;
    Pixel pixel_;
    bool have_pixel_;
    Pixel basic_[18];
    int nbasic_;
};

}

#endif
