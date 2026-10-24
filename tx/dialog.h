/*
 * tx/dialog.h - dialogs laid out once: the window manager offers no
 * resizing or maximizing (_MOTIF_WM_HINTS, which mwm, 4Dwm and ttr read),
 * while the dialog still grows by itself when its fonts or scale change.
 */
#ifndef TX_DIALOG_H
#define TX_DIALOG_H

#include <Xm/Xm.h>
#include <Xm/MwmUtil.h>

namespace tx {

/* 'dialog' is the dialog's child of its shell (an XmCreate*Dialog). */
inline void fixed_size(Widget dialog)
{
    Widget shell = XtParent(dialog);
    XtVaSetValues(shell, XmNmwmFunctions, (int)(MWM_FUNC_ALL | MWM_FUNC_RESIZE | MWM_FUNC_MAXIMIZE),
                  XmNmwmDecorations,
                  (int)(MWM_DECOR_ALL | MWM_DECOR_RESIZEH | MWM_DECOR_MAXIMIZE), (char*)0);
}

} /* namespace tx */

#endif
