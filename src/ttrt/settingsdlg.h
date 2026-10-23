/*
 * ttrt's Settings dialog (Terminal > Settings...): font, colours,
 * scrollback, shell, menu bar, Backspace and bell. OK and Apply change
 * the window it was opened from and save what changed in ttrt's settings
 * file (settings.h), for new windows.
 */
#ifndef TTRT_SETTINGSDLG_H
#define TTRT_SETTINGSDLG_H

#include "settings.h"

#include "core/color.h"
#include "../../th/scheme.h"
#include "core/str.h"
#include "core/vector.h"

#include <X11/Intrinsic.h>

namespace ttr {
class ColorPicker;
}

namespace ttrt {

/* The window the dialog works on. */
class SettingsHost {
public:
    virtual ~SettingsHost() {}
    /* The window's value of a setting; "" where ttrt decides (the font it
     * picked itself, a cursor in the text colour). */
    virtual const char* current(int setting) const = 0;
    /* values[i] is the new value of setting i where changed[i]. */
    virtual void apply(const ttr::Str* values, const bool* changed) = 0;
};

class SettingsDialog {
public:
    SettingsDialog(Widget parent, SettingsHost* host);
    ~SettingsDialog();
    /* Built anew each time, from the window's current values. */
    void show();

private:
    SettingsDialog(const SettingsDialog&);
    SettingsDialog& operator=(const SettingsDialog&);

    /* One size of a fixed-width family: a server font, or a size of a
     * scalable one. */
    struct Size {
        int px;
        int width;      /* average width, pixels; 0 for scalable */
        int rank;       /* lower is preferred among same-size fonts */
        ttr::Str label;
        ttr::Str xlfd;
    };
    struct Family {
        ttr::Str name;
        ttr::Str label;
        bool scalable;
        ttr::Str scalable_xlfd;     /* with 0 sizes */
        int scalable_rank;
        ttr::Vector<Size*> sizes;   /* by px, then width */
    };
    enum { C_FG, C_BG, C_CURSOR, C_COUNT };
    struct Slot {
        SettingsDialog* dialog;
        int index;
    };

    void build();
    void destroy();
    Widget frame(Widget parent, const char* title);
    Widget row(Widget parent, const char* label);
    Widget drop_down(Widget parent, const char* name, const char* const* items,
                     int count, int visible, int columns);
    Widget radio(Widget parent, const char* a, const char* b, Widget* first);

    void free_fonts();
    void load_fonts();
    void add_font(const char* xlfd);
    Family* family_named(const char* name, bool create);
    void finish_family(Family* f);
    void show_current_font(const char* value);
    void fill_sizes(int family, int px, int width);
    ttr::Str chosen_font() const;
    ttr::Str preview_font() const;
    void update_preview();
    void update_swatches();
    ttr::Rgb color(int i) const;
    void collect(ttr::Str* values) const;
    bool commit();
    void error(const char* msg);

    static void family_cb(Widget w, XtPointer closure, XtPointer call);
    static void size_cb(Widget w, XtPointer closure, XtPointer call);
    static void scheme_cb(Widget w, XtPointer closure, XtPointer call);
    /* The colour scheme chosen in the dialog, or false for none. */
    bool chosen_scheme(th::Scheme* out) const;
    static void change_cb(Widget w, XtPointer closure, XtPointer call);
    static void reset_cb(Widget w, XtPointer closure, XtPointer call);
    static void picked(void* closure, const ttr::Rgb& c);
    static void ok_cb(Widget w, XtPointer closure, XtPointer call);
    static void apply_cb(Widget w, XtPointer closure, XtPointer call);
    static void cancel_cb(Widget w, XtPointer closure, XtPointer call);

    Widget parent_;
    SettingsHost* host_;
    Display* dpy_;
    Widget dialog_;
    ttr::Str orig_[S_COUNT];    /* the window's values when shown or applied */

    ttr::Vector<Family*> families_;     /* [0] is Automatic */
    int family_;
    Widget family_w_;
    Widget size_w_;
    Widget preview_;
    ttr::Str font_shown_;       /* the font as first selected, to see a change */

    bool mine_[C_COUNT];        /* a colour of its own, else the default */
    ttr::Rgb color_[C_COUNT];
    ttr::Str as_set_[C_COUNT];  /* the colour's text, kept unless changed */
    Slot slots_[C_COUNT];
    Widget swatch_[C_COUNT];
    Widget reset_[C_COUNT];
    Pixel pixel_[C_COUNT];
    bool have_pixel_[C_COUNT];
    Pixel preview_fg_, preview_bg_;
    bool have_preview_fg_, have_preview_bg_;
    ttr::ColorPicker* picker_;
    int picking_;

    Widget scheme_w_;           /* "None", then th's schemes */
    th::List schemes_;
    Widget bold_bright_;
    Widget save_lines_;
    Widget login_;
    Widget menu_bar_;
    Widget backspace_del_;
    Widget bell_;
};

}

#endif
