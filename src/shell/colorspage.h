/*
 * Settings > Colors: the theme (core/theme.h), shown in a preview drawn
 * the way ttr draws frames and the taskbar, and the user's own colors for
 * the background, text and window frames over it.
 */
#ifndef TTR_SHELL_COLORSPAGE_H
#define TTR_SHELL_COLORSPAGE_H

#include "core/theme.h"
#include "core/vector.h"
#include "x11/config.h"

#include <X11/Intrinsic.h>

namespace ttr {

class ColorPicker;
class SettingsDialog;
class SettingsFile;
class XPainter;

class ColorsPage {
public:
    ColorsPage(SettingsDialog* dialog, Widget page, const Config& cfg);
    ~ColorsPage();

    /* Put the theme and colors into the settings file. */
    void save(SettingsFile* file);

private:
    ColorsPage(const ColorsPage&);
    ColorsPage& operator=(const ColorsPage&);

    struct Slot {
        ColorsPage* page;
        int index;              /* into k_color_keys */
    };

    Theme effective() const;    /* the theme with the user's colors */
    void select_theme(int i);
    void update_swatches();
    void paint_preview();

    static void list_cb(Widget w, XtPointer closure, XtPointer call);
    static void change_cb(Widget w, XtPointer closure, XtPointer call);
    static void reset_cb(Widget w, XtPointer closure, XtPointer call);
    static void picked(void* closure, const Rgb& c);
    static void expose_cb(Widget w, XtPointer closure, XtPointer call);

    Widget top_;
    Vector<ThemeInfo*> themes_;     /* themes_[i] is list row i + 2 */
    int current_;                   /* -1: Classic */
    Theme base_;                    /* the selected theme */
    bool mine_[k_ncolor_keys];
    Rgb color_[k_ncolor_keys];
    Slot slots_[k_ncolor_keys];
    int picking_;

    Widget list_;
    Widget preview_;
    Widget swatch_[k_ncolor_keys];
    Widget reset_[k_ncolor_keys];
    Pixel pixel_[k_ncolor_keys];
    bool have_pixel_[k_ncolor_keys];
    XPainter* painter_;
    ColorPicker* picker_;
};

}

#endif
