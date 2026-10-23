/* The visual each renderer kind draws into (renderer.h). */
#include "renderer.h"

#include <string.h>

namespace tw {

bool gl_choose_visual(Display* dpy, int screen, XVisualInfo* out);

bool choose_visual(const char* kind, Display* dpy, int screen, XVisualInfo* out)
{
    if (kind && strcmp(kind, "gl") == 0)
        return gl_choose_visual(dpy, screen, out);
    if (kind && strcmp(kind, "soft") == 0) {
        /* The default visual if it is TrueColor, else the deepest one
         * that is (the Indy: 24-bit beside its 8-bit default). */
        Visual* dv = DefaultVisual(dpy, screen);
        if (dv->c_class == TrueColor && DefaultDepth(dpy, screen) >= 15) {
            XVisualInfo tmpl;
            tmpl.visualid = XVisualIDFromVisual(dv);
            int n = 0;
            XVisualInfo* vi = XGetVisualInfo(dpy, VisualIDMask, &tmpl, &n);
            if (vi) {
                *out = *vi;
                XFree(vi);
                return true;
            }
        }
        static const int depths[] = { 24, 32, 16, 15 };
        for (int i = 0; i < 4; i++)
            if (XMatchVisualInfo(dpy, screen, depths[i], TrueColor, out))
                return true;
        return false;
    }
    /* core: the default visual. */
    XVisualInfo tmpl;
    tmpl.visualid = XVisualIDFromVisual(DefaultVisual(dpy, screen));
    int n = 0;
    XVisualInfo* vi = XGetVisualInfo(dpy, VisualIDMask, &tmpl, &n);
    if (!vi)
        return false;
    *out = *vi;
    XFree(vi);
    return true;
}

} /* namespace tw */
