/*
 * tx/pages.cpp - the settings dialog every ttr program has (tx/pages.h).
 */
#include "pages.h"
#include "dialog.h"

#include "core/scale.h"

#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/ScrolledW.h>
#include <Xm/Separator.h>
#include <Xm/ComboBox.h>
#include <Xm/ToggleB.h>

using ttr::scaled;

namespace tx {

namespace {

XmString xmstr(const char* s)
{
    return XmStringCreateLocalized((char*)s);
}

} /* namespace */

PagedDialog::PagedDialog()
    : dialog_(0), list_(0), scroll_(0), content_(0), count_(0), shown_(0), pressed_(0), data_(0)
{
    for (int i = 0; i < MAX_PAGES; i++)
        pages_[i] = 0;
}

void PagedDialog::create(Widget parent, const char* name, const char* title,
                         const char* const* pages, int count)
{
    if (count > MAX_PAGES)
        count = MAX_PAGES;
    count_ = count;
    XmString t = xmstr(title);
    Arg args[6];
    int n = 0;
    XtSetArg(args[n], XmNdialogTitle, t); n++;
    XtSetArg(args[n], XmNautoUnmanage, False); n++;
    XtSetArg(args[n], XmNmarginWidth, scaled(10)); n++;
    XtSetArg(args[n], XmNmarginHeight, scaled(10)); n++;
    dialog_ = XmCreateFormDialog(parent, (char*)name, args, n);
    fixed_size(dialog_);
    XmStringFree(t);

    /* Categories on the left, the chosen page on the right. */
    XmString items[MAX_PAGES];
    int i;
    for (i = 0; i < count; i++)
        items[i] = xmstr(pages[i]);
    n = 0;
    XtSetArg(args[n], XmNitems, items); n++;
    XtSetArg(args[n], XmNitemCount, count); n++;
    XtSetArg(args[n], XmNvisibleItemCount, count); n++;
    XtSetArg(args[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(args[n], XmNlistMarginWidth, scaled(4)); n++;
    list_ = XmCreateScrolledList(dialog_, (char*)"categories", args, n);
    for (i = 0; i < count; i++)
        XmStringFree(items[i]);
    XtAddCallback(list_, XmNbrowseSelectionCallback, page_cb, (XtPointer)this);
    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetValues(XtParent(list_), args, n);
    XtManageChild(list_);

    /* The page area: in a window that scrolls when the screen is too
     * small for it (a large UI scale), which looks the same as the pages
     * alone when they fit. A window manager would otherwise squeeze the
     * dialog to the screen, and rows lose what does not fit. */
    Arg sargs[10];
    n = 0;
    XtSetArg(sargs[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(sargs[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(sargs[n], XmNleftWidget, XtParent(list_)); n++;
    XtSetArg(sargs[n], XmNleftOffset, scaled(10)); n++;
    XtSetArg(sargs[n], XmNrightAttachment, XmATTACH_FORM); n++;
    XtSetArg(sargs[n], XmNscrollingPolicy, XmAUTOMATIC); n++;
    XtSetArg(sargs[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
    XtSetArg(sargs[n], XmNshadowThickness, 0); n++;
    XtSetArg(sargs[n], XmNspacing, 0); n++;
    scroll_ = XmCreateScrolledWindow(dialog_, (char*)"pageArea", sargs, n);
    content_ = XmCreateForm(scroll_, (char*)"pages", 0, 0);

    /* One page: a column of frames in the page area, shown alone. */
    for (i = 0; i < count; i++) {
        n = 0;
        XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
        XtSetArg(args[n], XmNspacing, scaled(6)); n++;
        XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
        XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
        XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
        pages_[i] = XmCreateRowColumn(content_, (char*)"page", args, n);
    }
}

void PagedDialog::finish(const char* note, int buttons, Pressed pressed, void* data)
{
    pressed_ = pressed;
    data_ = data;
    /* The page area is as big as the biggest page, so switching pages
     * does not resize the dialog. */
    Dimension pw = 0, ph = 0;
    int i;
    for (i = 0; i < count_; i++) {
        XtWidgetGeometry pref;
        XtQueryGeometry(pages_[i], 0, &pref);
        if (pref.width > pw)
            pw = pref.width;
        if (pref.height > ph)
            ph = pref.height;
    }
    Arg args[8];
    int n = 0;
    XtSetArg(args[n], XmNwidth, pw); n++;
    XtSetArg(args[n], XmNheight, ph); n++;
    XtSetArg(args[n], XmNresizePolicy, XmRESIZE_NONE); n++;
    XtSetValues(content_, args, n);
    XtManageChild(content_);
    show_page(shown_);
    /* At most most of the screen, beside the list and above the buttons;
     * scroll bars where the pages need more. */
    Screen* scr = XtScreen(dialog_);
    XtWidgetGeometry lp;
    XtQueryGeometry(XtParent(list_), 0, &lp);
    int max_w = WidthOfScreen(scr) * 9 / 10 - (int)lp.width - scaled(40);
    int max_h = HeightOfScreen(scr) * 9 / 10 - scaled(90);
    int w = pw, h = ph;
    if (w > max_w || h > max_h) {
        /* Room for the scroll bar across the other way. */
        int bar = scaled(17) + scaled(4);
        if (w > max_w)
            h += bar;
        if (h > max_h)
            w += bar;
    }
    if (w > max_w && max_w > scaled(200))
        w = max_w;
    if (h > max_h && max_h > scaled(150))
        h = max_h;
    n = 0;
    XtSetArg(args[n], XmNwidth, w); n++;
    XtSetArg(args[n], XmNheight, h); n++;
    XtSetValues(scroll_, args, n);
    XtManageChild(scroll_);

    /* The note and the buttons along the bottom. */
    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, scroll_); n++;
    XtSetArg(args[n], XmNtopOffset, scaled(10)); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget sep = XmCreateSeparator(dialog_, (char*)"sep", args, n);
    XtManageChild(sep);

    if (note && *note) {
        XmString l = xmstr(note);
        n = 0;
        XtSetArg(args[n], XmNlabelString, l); n++;
        XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
        XtSetArg(args[n], XmNtopWidget, sep); n++;
        XtSetArg(args[n], XmNtopOffset, scaled(10)); n++;
        XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
        Widget label = XmCreateLabel(dialog_, (char*)"note", args, n);
        XmStringFree(l);
        XtManageChild(label);
    }

    /* Right to left: Cancel, Apply, OK. */
    static const char* const names[3] = { "cancel", "apply", "ok" };
    static const char* const labels[3] = { "Cancel", "Apply", "OK" };
    static const int which[3] = { CANCEL, APPLY, OK };
    Widget right = 0, ok = 0, cancel = 0;
    for (i = 0; i < 3; i++) {
        if (!(buttons & which[i]))
            continue;
        XmString l = xmstr(labels[i]);
        n = 0;
        XtSetArg(args[n], XmNlabelString, l); n++;
        XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
        XtSetArg(args[n], XmNtopWidget, sep); n++;
        XtSetArg(args[n], XmNtopOffset, scaled(6)); n++;
        if (right) {
            XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
            XtSetArg(args[n], XmNrightWidget, right); n++;
            XtSetArg(args[n], XmNrightOffset, scaled(6)); n++;
        } else {
            XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
        }
        Widget b = XmCreatePushButton(dialog_, (char*)names[i], args, n);
        XmStringFree(l);
        hooks_[i].dialog = this;
        hooks_[i].which = which[i];
        XtAddCallback(b, XmNactivateCallback, button_cb, (XtPointer)&hooks_[i]);
        XtManageChild(b);
        right = b;
        if (which[i] == OK)
            ok = b;
        if (which[i] == CANCEL)
            cancel = b;
    }
    n = 0;
    if (ok) {
        XtSetArg(args[n], XmNdefaultButton, ok); n++;
    }
    if (cancel) {
        XtSetArg(args[n], XmNcancelButton, cancel); n++;
    }
    if (n)
        XtSetValues(dialog_, args, n);
}

void PagedDialog::show_page(int i)
{
    if (i < 0 || i >= count_)
        i = 0;
    shown_ = i;
    for (int j = 0; j < count_; j++)
        if (j != i)
            XtUnmanageChild(pages_[j]);
    XtManageChild(pages_[i]);
    XmListSelectPos(list_, i + 1, False);
}

void PagedDialog::destroy()
{
    if (dialog_)
        XtDestroyWidget(XtParent(dialog_));
    dialog_ = list_ = scroll_ = content_ = 0;
    for (int i = 0; i < MAX_PAGES; i++)
        pages_[i] = 0;
    count_ = 0;
}

void PagedDialog::page_cb(Widget, XtPointer closure, XtPointer call)
{
    XmListCallbackStruct* cbs = (XmListCallbackStruct*)call;
    ((PagedDialog*)closure)->show_page(cbs->item_position - 1);
}

void PagedDialog::button_cb(Widget, XtPointer closure, XtPointer)
{
    Hook* h = (Hook*)closure;
    if (h->dialog->pressed_)
        h->dialog->pressed_(h->dialog->data_, h->which);
}

Widget frame(Widget parent, const char* title)
{
    Widget f = XmCreateFrame(parent, (char*)"frame", 0, 0);
    XmString l = xmstr(title);
    Arg args[3];
    XtSetArg(args[0], XmNlabelString, l);
    XtSetArg(args[1], XmNchildType, XmFRAME_TITLE_CHILD);
    XtManageChild(XmCreateLabel(f, (char*)"title", args, 2));
    XmStringFree(l);

    XtSetArg(args[0], XmNorientation, XmVERTICAL);
    XtSetArg(args[1], XmNmarginWidth, scaled(8));
    XtSetArg(args[2], XmNmarginHeight, scaled(6));
    Widget rc = XmCreateRowColumn(f, (char*)"rows", args, 3);
    XtManageChild(rc);
    XtManageChild(f);
    return rc;
}

Widget row(Widget parent, const char* label)
{
    Arg args[4];
    XtSetArg(args[0], XmNorientation, XmHORIZONTAL);
    XtSetArg(args[1], XmNmarginWidth, 0);
    XtSetArg(args[2], XmNmarginHeight, 0);
    Widget rc = XmCreateRowColumn(parent, (char*)"row", args, 3);

    XmString l = xmstr(label);
    int n = 0;
    XtSetArg(args[n], XmNlabelString, l); n++;
    XtSetArg(args[n], XmNwidth, scaled(LABEL_WIDTH)); n++;
    XtSetArg(args[n], XmNrecomputeSize, False); n++;
    XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
    XtManageChild(XmCreateLabel(rc, (char*)"label", args, n));
    XmStringFree(l);
    XtManageChild(rc);
    return rc;
}

Widget radio(Widget parent, const char* a, const char* b, Widget* first)
{
    Arg args[1];
    XtSetArg(args[0], XmNorientation, XmHORIZONTAL);
    Widget box = XmCreateRadioBox(parent, (char*)"radio", args, 1);
    XmString la = xmstr(a), lb = xmstr(b);
    XtSetArg(args[0], XmNlabelString, la);
    *first = XmCreateToggleButton(box, (char*)"a", args, 1);
    XtSetArg(args[0], XmNlabelString, lb);
    Widget second = XmCreateToggleButton(box, (char*)"b", args, 1);
    XmStringFree(la);
    XmStringFree(lb);
    XtManageChild(*first);
    XtManageChild(second);
    XtManageChild(box);
    return box;
}

Widget drop_down(Widget parent, const char* name, const char* const* items, int count,
                 int visible, int columns)
{
    XmString* strs = new XmString[count > 0 ? count : 1];
    int i;
    for (i = 0; i < count; i++)
        strs[i] = xmstr(items[i]);
    Arg args[4];
    int n = 0;
    XtSetArg(args[n], XmNvisibleItemCount, visible); n++;
    XtSetArg(args[n], XmNcolumns, columns); n++;
    if (count > 0) {    /* Motif reads items[0] even when there are none */
        XtSetArg(args[n], XmNitems, strs); n++;
        XtSetArg(args[n], XmNitemCount, count); n++;
    }
    Widget cb = XmCreateDropDownList(parent, (char*)name, args, n);
    for (i = 0; i < count; i++)
        XmStringFree(strs[i]);
    delete [] strs;
    XtManageChild(cb);
    return cb;
}

Widget note(Widget parent, const char* text)
{
    XmString l = xmstr(text);
    Arg args[1];
    XtSetArg(args[0], XmNlabelString, l);
    Widget w = XmCreateLabel(parent, (char*)"note", args, 1);
    XmStringFree(l);
    XtManageChild(w);
    return w;
}

void choose(Widget drop_down, int i)
{
    Widget list = XtNameToWidget(drop_down, "*List");
    XmStringTable items = 0;
    int count = 0;
    if (!list)
        return;
    XtVaGetValues(list, XmNitems, &items, XmNitemCount, &count, (char*)0);
    if (i >= 0 && i < count)
        XmComboBoxSelectItem(drop_down, items[i]);
}

int chosen(Widget drop_down)
{
    Widget list = XtNameToWidget(drop_down, "*List");
    int* pos = 0;
    int n = 0;
    if (!list)
        return -1;
    XtVaGetValues(list, XmNselectedPositions, &pos, XmNselectedPositionCount, &n, (char*)0);
    return pos && n > 0 ? pos[0] - 1 : -1;
}

} /* namespace tx */
