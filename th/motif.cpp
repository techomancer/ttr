#include "motif.h"

#include <stdio.h>
#include <X11/IntrinsicP.h>
#include <X11/CoreP.h>
#include <Xm/List.h>
#include <Xm/PushB.h>
#include <Xm/PushBG.h>
#include <Xm/ScrollBar.h>
#include <Xm/Text.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>
#include <Xm/ToggleBG.h>

namespace th {

namespace {

void put_color(XrmDatabase* db, const char* spec, Color c)
{
    char hex[8];
    format_hex(c, hex);
    XrmPutStringResource(db, spec, hex);
}

void recolor_one(Widget w, const Theme& t)
{
    if (XmIsGadget(w))
        return;         /* gadgets draw with their parent's colours */
    Role role = ROLE_BACKGROUND;
    if (XmIsList(w))
        role = ROLE_LIST;
    else if (XmIsTextField(w) || XmIsText(w))
        role = ROLE_TEXT_FIELD;
    else if (XmIsPushButton(w))
        role = ROLE_BUTTON;
    else if (XmIsScrollBar(w))
        role = ROLE_SCROLL_BAR;
    if (XmIsPrimitive(w) || XmIsManager(w)) {
        XmChangeColor(w, pixel(w, t.get(role)));
    } else {
        XtVaSetValues(w, XmNbackground, pixel(w, t.get(role)), (char*)0);
        return;
    }
    if (t.set[ROLE_FOREGROUND])
        XtVaSetValues(w, XmNforeground, pixel(w, t.get(ROLE_FOREGROUND)), (char*)0);
    if (XmIsScrollBar(w))
        XtVaSetValues(w, XmNtroughColor, pixel(w, t.get(ROLE_TROUGH)), (char*)0);
    if (XmIsToggleButton(w))
        XtVaSetValues(w, XmNselectColor, pixel(w, t.get(ROLE_SELECT_FILL)), (char*)0);
}

void walk(Widget w, const Theme& t, WidgetClass skip)
{
    if (skip && XtIsSubclass(w, skip))
        return;
    recolor_one(w, t);
    /* Gadgets' colours come from here too. */
    if (XtIsComposite(w)) {
        WidgetList kids = 0;
        Cardinal n = 0;
        XtVaGetValues(w, XmNchildren, &kids, XmNnumChildren, &n, (char*)0);
        for (Cardinal i = 0; i < n; i++)
            walk(kids[i], t, skip);
    }
    /* Only widgets have popups: a gadget's record ends before them. */
    if (!XtIsWidget(w))
        return;
    for (Cardinal i = 0; i < w->core.num_popups; i++)
        walk(w->core.popup_list[i], t, skip);
}

} /* namespace */

void apply_theme(XrmDatabase* db, const Theme& t)
{
    put_color(db, "*background", t.get(ROLE_BACKGROUND));
    if (t.set[ROLE_FOREGROUND])
        put_color(db, "*foreground", t.color[ROLE_FOREGROUND]);
    if (t.set[ROLE_BUTTON])
        put_color(db, "*XmPushButton.background", t.color[ROLE_BUTTON]);
    if (t.set[ROLE_TEXT_FIELD]) {
        put_color(db, "*XmTextField.background", t.color[ROLE_TEXT_FIELD]);
        put_color(db, "*XmText.background", t.color[ROLE_TEXT_FIELD]);
    }
    if (t.set[ROLE_LIST])
        put_color(db, "*XmList.background", t.color[ROLE_LIST]);
    if (t.set[ROLE_TROUGH])
        put_color(db, "*XmScrollBar.troughColor", t.color[ROLE_TROUGH]);
    if (t.set[ROLE_SCROLL_BAR])
        put_color(db, "*XmScrollBar.background", t.color[ROLE_SCROLL_BAR]);
    if (t.set[ROLE_SELECT_FILL]) {
        put_color(db, "*XmToggleButton.selectColor", t.color[ROLE_SELECT_FILL]);
        put_color(db, "*XmToggleButtonGadget.selectColor", t.color[ROLE_SELECT_FILL]);
    }
}

void recolor(Widget root, const Theme& t, WidgetClass skip)
{
    walk(root, t, skip);
}

Pixel pixel(Widget w, Color c)
{
    Display* dpy = XtDisplay(w);
    Colormap cmap = 0;
    XtVaGetValues(w, XmNcolormap, &cmap, (char*)0);
    if (!cmap)
        cmap = DefaultColormap(dpy, DefaultScreen(dpy));
    XColor xc;
    xc.red = (unsigned short)(((c >> 16) & 0xff) * 257);
    xc.green = (unsigned short)(((c >> 8) & 0xff) * 257);
    xc.blue = (unsigned short)((c & 0xff) * 257);
    xc.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(dpy, cmap, &xc))
        return (c >> 16) + ((c >> 8) & 0xff) + (c & 0xff) > 384 ? WhitePixel(dpy, DefaultScreen(dpy))
                                                               : BlackPixel(dpy, DefaultScreen(dpy));
    return xc.pixel;
}

} /* namespace th */
