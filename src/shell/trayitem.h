/*
 * A built-in notification area icon (network, volume): drawn by the tray
 * in a 16 pixel slot left of the docked XEmbed icons.
 */
#ifndef TTR_SHELL_TRAYITEM_H
#define TTR_SHELL_TRAYITEM_H

#include "core/geom.h"

#include <X11/Xlib.h>

namespace ttr {

class Painter;

class TrayItem {
public:
    virtual ~TrayItem() {}
    /* False hides the slot (e.g. no sound server found). */
    virtual bool visible() const = 0;
    virtual void paint(Painter& p, const Rect& r) = 0;
    /* A button press or release inside the slot. */
    virtual void button(XButtonEvent* ev) = 0;
};

}

#endif
