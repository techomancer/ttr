/*
 * Posting a Motif popup menu from the notification area: just above the
 * area the click was in, so it does not cover the taskbar, and moved left when it would
 * run off the right edge of the screen.
 */
#ifndef TTR_SHELL_POPUP_H
#define TTR_SHELL_POPUP_H

#include <Xm/Xm.h>
#include <Xm/RowColumn.h>

#include <string.h>

namespace ttr {

inline void post_popup_above(Widget menu, const XButtonEvent* ev)
{
    XtRealizeWidget(XtParent(menu));
    Dimension w = 0, h = 0;
    Arg args[2];
    XtSetArg(args[0], XmNwidth, &w);
    XtSetArg(args[1], XmNheight, &h);
    XtGetValues(menu, args, 2);

    Display* dpy = XtDisplay(menu);
    int sw = DisplayWidth(dpy, DefaultScreen(dpy));
    XButtonEvent bev = *ev;
    if (bev.x_root + (int)w > sw)
        bev.x_root = sw - (int)w;
    if (bev.x_root < 0)
        bev.x_root = 0;
    /* ev->y is relative to the tray area, so this is its top edge. */
    bev.y_root = ev->y_root - ev->y - (int)h - 2;
    if (bev.y_root < 0)
        bev.y_root = 0;
    XmMenuPosition(menu, &bev);
    XtManageChild(menu);
}

}

#endif
