/*
 * ttrt's Settings dialog (Terminal > Settings...), in pages as ttr's
 * (tx/pages.h): the terminal (scrollback, shell, menu bar, Backspace,
 * bell), appearance (UI scale, font, drawing) and colours (the UI
 * theme, the text colour scheme and colours). OK and Apply
 * change the window it was opened from and save what changed in ttrt's
 * settings file (settings.h), for new windows.
 */
#ifndef TTRT_SETTINGSDLG_H
#define TTRT_SETTINGSDLG_H

#include "settings.h"

#include "core/color.h"
#include "../../th/scheme.h"
#include "core/str.h"
#include "core/vector.h"
#include "../../tx/pages.h"

#include <X11/Intrinsic.h>

namespace ttr {
class ColorPicker;
}
namespace tx {
class FontChooser;
}

namespace ttrt {

/* The window the dialog works on. */
class SettingsHost {
public:
    virtual ~SettingsHost() {}
    /* The window's value of a setting; "" where ttrt decides (the font it
     * picked itself, a cursor in the text colour). */
    virtual const char* current(int setting) const = 0;
    /* The font ttrt picks when none is set, at the window's UI scale. */
    virtual const char* automatic_font() const = 0;
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

    enum { C_FG, C_BG, C_CURSOR, C_COUNT };
    struct Slot {
        SettingsDialog* dialog;
        int index;
    };

    void build();
    void destroy();

    void update_preview();
    void update_swatches();
    ttr::Rgb color(int i) const;
    void collect(ttr::Str* values) const;
    bool commit();
    void error(const char* msg);

    static void scheme_cb(Widget w, XtPointer closure, XtPointer call);
    /* The colour scheme chosen in the dialog, or false for none. */
    bool chosen_scheme(th::Scheme* out) const;
    static void change_cb(Widget w, XtPointer closure, XtPointer call);
    static void reset_cb(Widget w, XtPointer closure, XtPointer call);
    static void picked(void* closure, const ttr::Rgb& c);
    static void pressed(void* data, int which);

    Widget parent_;
    SettingsHost* host_;
    Display* dpy_;
    tx::PagedDialog pages_;
    Widget dialog_;
    ttr::Str orig_[S_COUNT];    /* the window's values when shown or applied */

    tx::FontChooser* font_;

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
    Widget renderer_w_;
    Widget dynbg_w_;
    Widget text_w_;
    Widget scale_w_;            /* "Same as ttr", then percents */
    Widget theme_w_;            /* "Same as ttr", then th's themes */
    th::List themes_;
};

}

#endif
