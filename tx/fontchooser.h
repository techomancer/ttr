/*
 * tx/fontchooser.h - choosing an X font: a family and a size from the
 * fonts the X server has, a Bold toggle where the program wants one, and
 * a preview. The same chooser in ttrt, ttrcode and ttr's settings
 * (tx/DESIGN.md).
 *
 * Families are listed once, by name; scalable ones (Type1, TrueType,
 * pixel size 0 in their names) are marked with a * and offer the usual
 * sizes, bitmap ones the sizes they come in ("13", or "13 (6x13)" where a
 * height comes in several widths). Among fonts of one family and size the
 * chooser prefers Unicode, then medium weight.
 */
#ifndef TX_FONTCHOOSER_H
#define TX_FONTCHOOSER_H

#include "core/str.h"
#include "core/vector.h"

#include <X11/Intrinsic.h>

namespace tx {

class FontChooser {
public:
    enum {
        FIXED = 1,          /* fixed-width fonts only (terminals, editors) */
        UNICODE = 2,        /* ISO 10646 fonts too, not only Latin-1 */
        AUTOMATIC = 4,      /* an "Automatic" entry: get() returns "" */
        BOLD = 8,           /* a Bold toggle */
        NO_PREVIEW = 16
    };
    typedef void (*Changed)(FontChooser* c, void* data);
    /* What the preview shows for a font name (ttr scales it to its UI). */
    typedef ttr::Str (*PreviewName)(const char* xlfd, void* data);

    /* The chooser's widgets go in parent, which should lay them out in a
     * row or column (a RowColumn); widget() is their own column. */
    FontChooser(Widget parent, const char* name, unsigned flags);
    ~FontChooser();
    Widget widget() const { return box_; }

    /* Show a font: an XLFD (wildcards allowed), an alias (fixed, 9x15)
     * or "" for Automatic. One the lists do not have gets its own entry
     * ("As set: ..."), so get() returns it unchanged. */
    void set(const char* font);
    /* The chosen font: an XLFD the server has, the name set() was given
     * when nothing was changed, or "" for Automatic. */
    ttr::Str get() const;
    /* The font Automatic stands for, for the preview, and its label
     * ("Automatic", "Same as the editor"). */
    void set_automatic_preview(const char* font);
    void set_automatic_label(const char* label);
    void set_preview_colors(Pixel fg, Pixel bg);
    void set_preview_text(const char* text);
    void set_preview_name(PreviewName fn, void* data);
    void on_changed(Changed fn, void* data);

private:
    struct Size {
        int px;
        int width;          /* average width, pixels; 0 for scalable */
        int rank;           /* lower is preferred among same-size fonts */
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

    void load();
    void add_font(const char* xlfd);
    Family* family_named(const char* name, bool create);
    void finish_family(Family* f);
    void fill_sizes(int fi, int px, int width);
    ttr::Str chosen() const;
    ttr::Str bolden(const char* xlfd) const;
    void update_preview();
    void changed();
    static void family_cb(Widget w, XtPointer closure, XtPointer call);
    static void size_cb(Widget w, XtPointer closure, XtPointer call);

    Display* dpy_;
    unsigned flags_;
    Widget box_, family_w_, size_w_, bold_w_, preview_;
    ttr::Vector<Family*> families_;
    int family_;
    ttr::Str set_value_;        /* what set() was given */
    ttr::Str set_shown_;        /* what it showed for it */
    bool set_bold_;
    ttr::Str automatic_;
    Changed changed_fn_;
    void* changed_data_;
    PreviewName preview_fn_;
    void* preview_data_;

    FontChooser(const FontChooser&);
    FontChooser& operator=(const FontChooser&);
};

} /* namespace tx */

#endif
