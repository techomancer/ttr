/*
 * Settings > Terminal: ttrt's settings (core/termsettings.h), saved as
 * "ttrt.<name>" lines in the settings file. ttrt reads them when a window
 * opens, so they apply to new terminal windows.
 */
#ifndef TTR_SHELL_TERMINALPAGE_H
#define TTR_SHELL_TERMINALPAGE_H

#include "core/color.h"
#include "core/str.h"
#include "core/termsettings.h"
#include "core/vector.h"

#include <X11/Intrinsic.h>

namespace ttr {

class ColorPicker;
class SettingsDialog;
class SettingsFile;

class TerminalPage {
public:
    TerminalPage(SettingsDialog* dialog, Widget page);
    ~TerminalPage();

    void save(SettingsFile* file);

private:
    TerminalPage(const TerminalPage&);
    TerminalPage& operator=(const TerminalPage&);

    /* One size of a fixed-width family: a server font, or a size of a
     * scalable one. */
    struct Size {
        int px;
        int width;      /* average width, pixels; 0 for scalable */
        int rank;       /* lower is preferred among same-size fonts */
        Str label;
        Str xlfd;
    };
    struct Family {
        Str name;
        Str label;
        bool scalable;
        Str scalable_xlfd;      /* with 0 sizes */
        int scalable_rank;
        Vector<Size*> sizes;    /* by px, then width */
    };
    enum { C_FG, C_BG, C_CURSOR, C_COUNT };
    struct Slot {
        TerminalPage* page;
        int index;
    };

    void load_fonts();
    void add_font(const char* xlfd);
    Family* family_named(const char* name, bool create);
    void finish_family(Family* f);
    void show_current_font(const char* value);
    void fill_sizes(int family, int px, int width);
    Str chosen_font() const;
    Str preview_font() const;
    void update_preview();
    void update_swatches();
    Rgb color(int i) const;

    static void family_cb(Widget w, XtPointer closure, XtPointer call);
    static void size_cb(Widget w, XtPointer closure, XtPointer call);
    static void change_cb(Widget w, XtPointer closure, XtPointer call);
    static void reset_cb(Widget w, XtPointer closure, XtPointer call);
    static void picked(void* closure, const Rgb& c);

    Widget top_;
    Display* dpy_;
    Vector<Family*> families_;  /* [0] is Automatic */
    int family_;                /* selected */
    Widget family_w_;
    Widget size_w_;
    Widget preview_;
    Str font_as_set_;           /* the file's font name ("9x15") */
    Str font_shown_;            /* what it was shown as, to see a change */

    bool mine_[C_COUNT];        /* set by the user, else the default */
    Rgb color_[C_COUNT];
    Str as_set_[C_COUNT];       /* the file's text, kept unless changed */
    Slot slots_[C_COUNT];
    Widget swatch_[C_COUNT];
    Widget reset_[C_COUNT];
    Pixel pixel_[C_COUNT];
    bool have_pixel_[C_COUNT];
    Pixel preview_fg_, preview_bg_;
    bool have_preview_fg_, have_preview_bg_;
    ColorPicker* picker_;
    int picking_;

    Widget bold_bright_;
    Widget save_lines_;
    Widget login_;
    Widget menu_bar_;
    Widget backspace_del_;
    Widget bell_;
};

}

#endif
