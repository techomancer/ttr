/*
 * tx/pages.h - a settings dialog as every ttr program has it: the pages'
 * names in a list on the left, the chosen page on the right, a note and
 * the buttons along the bottom. Pages are columns of frames, each frame
 * rows that start with a label of one width, so all the programs' dialogs
 * line up the same way. Sizes go through ttr::scaled().
 */
#ifndef TX_PAGES_H
#define TX_PAGES_H

#include <X11/Intrinsic.h>

namespace tx {

class PagedDialog {
public:
    enum { OK = 1, APPLY = 2, CANCEL = 4 };
    enum { MAX_PAGES = 12 };
    /* Called with OK, APPLY or CANCEL when one is pressed; the dialog
     * stays up until the program unmanages it. */
    typedef void (*Pressed)(void* data, int which);

    PagedDialog();

    /* The dialog (unmanaged) and one empty page per name. The page shown
     * last is kept when the dialog is made again. */
    void create(Widget parent, const char* name, const char* title,
                const char* const* pages, int count);
    Widget dialog() const { return dialog_; }
    Widget page(int i) const { return pages_[i]; }
    /* With the pages filled: the page area as big as the biggest page,
     * then the note (or none) and the buttons. */
    void finish(const char* note, int buttons, Pressed pressed, void* data);
    void show_page(int i);
    int shown() const { return shown_; }
    /* Destroy the dialog's widgets (to build it again). */
    void destroy();

private:
    PagedDialog(const PagedDialog&);
    PagedDialog& operator=(const PagedDialog&);
    static void page_cb(Widget w, XtPointer closure, XtPointer call);
    static void button_cb(Widget w, XtPointer closure, XtPointer call);

    struct Hook {
        PagedDialog* dialog;
        int which;
    };
    Widget dialog_;
    Widget list_;
    Widget content_;
    Widget pages_[MAX_PAGES];
    int count_;
    int shown_;
    Pressed pressed_;
    void* data_;
    Hook hooks_[3];
};

/* The label width rows start with, at 100%. */
enum { LABEL_WIDTH = 140 };

/* A titled frame in a page; returns the column its rows go in. */
Widget frame(Widget parent, const char* title);
/* A horizontal row starting with a label LABEL_WIDTH wide. */
Widget row(Widget parent, const char* label);
/* Two radio buttons named "a" and "b"; returns the box, *first is "a". */
Widget radio(Widget parent, const char* a, const char* b, Widget* first);
/* A drop-down list of items (managed). */
Widget drop_down(Widget parent, const char* name, const char* const* items, int count,
                 int visible, int columns);
/* A plain label after the other widgets of a row. */
Widget note(Widget parent, const char* text);
/* A drop-down list's item i (from 0) selected, and which one is (-1:
 * none). */
void choose(Widget drop_down, int i);
int chosen(Widget drop_down);

} /* namespace tx */

#endif
