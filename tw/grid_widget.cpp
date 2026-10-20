/*
 * TwGrid: an XmPrimitive subclass that owns a tw::Surface. Xt class
 * records are C structures; this file fills them in C++98.
 */
#include "grid_widget.h"
#include "surface.h"

#include <stdio.h>
#include <string.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <Xm/PrimitiveP.h>
#include <Xm/XmP.h>

namespace {

class GridScheduler;

struct TwGridPart {
    tw::Source* source;
    String font;
    String bold_font;
    int columns;
    int rows;
    int frame_delay;
    int blink_interval;
    XtCallbackList input_cb;
    XtCallbackList pointer_cb;
    XtCallbackList resize_cb;
    XtCallbackList focus_cb;

    tw::Surface* surface;
    GridScheduler* sched;
    XtIntervalId frame_timer;
    XtIntervalId blink_timer;
    int cols_now, rows_now;
};

struct TwGridRec {
    CorePart core;
    XmPrimitivePart primitive;
    TwGridPart grid;
};

struct TwGridClassPart {
    XtPointer extension;
};

struct TwGridClassRec {
    CoreClassPart core_class;
    XmPrimitiveClassPart primitive_class;
    TwGridClassPart grid_class;
};

typedef TwGridRec* TwGridWidget;

const int k_margin = 2;

void frame_timeout(XtPointer data, XtIntervalId*);
void blink_timeout(XtPointer data, XtIntervalId*);

/* Timers on the widget's application context. */
class GridScheduler : public tw::Scheduler {
public:
    explicit GridScheduler(TwGridWidget g) : g_(g) {}
    void schedule_frame(int ms)
    {
        if (g_->grid.frame_timer)
            XtRemoveTimeOut(g_->grid.frame_timer);
        g_->grid.frame_timer = XtAppAddTimeOut(XtWidgetToApplicationContext((Widget)g_),
                                               (unsigned long)(ms < 0 ? 0 : ms),
                                               frame_timeout, (XtPointer)g_);
    }
    void schedule_blink(int ms)
    {
        if (g_->grid.blink_timer)
            XtRemoveTimeOut(g_->grid.blink_timer);
        g_->grid.blink_timer = 0;
        if (ms > 0)
            g_->grid.blink_timer = XtAppAddTimeOut(XtWidgetToApplicationContext((Widget)g_),
                                                   (unsigned long)ms, blink_timeout,
                                                   (XtPointer)g_);
    }

private:
    TwGridWidget g_;
};

void frame_timeout(XtPointer data, XtIntervalId*)
{
    TwGridWidget g = (TwGridWidget)data;
    g->grid.frame_timer = 0;
    if (XtIsRealized((Widget)g))
        g->grid.surface->frame();
}

void blink_timeout(XtPointer data, XtIntervalId*)
{
    TwGridWidget g = (TwGridWidget)data;
    g->grid.blink_timer = 0;
    if (XtIsRealized((Widget)g))
        g->grid.surface->blink();
}

/* ---- resources ----------------------------------------------------------- */

#define OFF(field) XtOffsetOf(TwGridRec, grid.field)

XtResource k_resources[] = {
    { (String)TwNsource, (String)TwCSource, (String)XtRPointer, sizeof(XtPointer),
      OFF(source), (String)XtRImmediate, (XtPointer)0 },
    { (String)TwNfont, (String)TwCFont, (String)XtRString, sizeof(String),
      OFF(font), (String)XtRString, (XtPointer)"fixed" },
    { (String)TwNboldFont, (String)TwCFont, (String)XtRString, sizeof(String),
      OFF(bold_font), (String)XtRImmediate, (XtPointer)0 },
    { (String)TwNcolumns, (String)TwCColumns, (String)XtRInt, sizeof(int),
      OFF(columns), (String)XtRImmediate, (XtPointer)80 },
    { (String)TwNrows, (String)TwCRows, (String)XtRInt, sizeof(int),
      OFF(rows), (String)XtRImmediate, (XtPointer)24 },
    { (String)TwNframeDelay, (String)TwCFrameDelay, (String)XtRInt, sizeof(int),
      OFF(frame_delay), (String)XtRImmediate, (XtPointer)8 },
    { (String)TwNblinkInterval, (String)TwCBlinkInterval, (String)XtRInt, sizeof(int),
      OFF(blink_interval), (String)XtRImmediate, (XtPointer)500 },
    { (String)TwNinputCallback, (String)XtCCallback, (String)XtRCallback,
      sizeof(XtCallbackList), OFF(input_cb), (String)XtRCallback, (XtPointer)0 },
    { (String)TwNpointerCallback, (String)XtCCallback, (String)XtRCallback,
      sizeof(XtCallbackList), OFF(pointer_cb), (String)XtRCallback, (XtPointer)0 },
    { (String)TwNresizeCallback, (String)XtCCallback, (String)XtRCallback,
      sizeof(XtCallbackList), OFF(resize_cb), (String)XtRCallback, (XtPointer)0 },
    { (String)TwNfocusCallback, (String)XtCCallback, (String)XtRCallback,
      sizeof(XtCallbackList), OFF(focus_cb), (String)XtRCallback, (XtPointer)0 },
    /* No highlight border or shadow: the text goes to the edge. */
    { (String)XmNhighlightThickness, (String)XmCHighlightThickness, (String)XmRHorizontalDimension,
      sizeof(Dimension), XtOffsetOf(TwGridRec, primitive.highlight_thickness),
      (String)XtRImmediate, (XtPointer)0 },
    { (String)XmNshadowThickness, (String)XmCShadowThickness, (String)XmRHorizontalDimension,
      sizeof(Dimension), XtOffsetOf(TwGridRec, primitive.shadow_thickness),
      (String)XtRImmediate, (XtPointer)0 },
    { (String)XmNtraversalOn, (String)XmCTraversalOn, (String)XmRBoolean, sizeof(Boolean),
      XtOffsetOf(TwGridRec, primitive.traversal_on), (String)XtRImmediate, (XtPointer)True }
};

#undef OFF

/* ---- helpers -------------------------------------------------------------- */

void preferred(TwGridWidget g, Dimension* w, Dimension* h)
{
    int cw = g->grid.surface->cell_width(), ch = g->grid.surface->cell_height();
    *w = (Dimension)(g->grid.columns * cw + 2 * k_margin);
    *h = (Dimension)(g->grid.rows * ch + 2 * k_margin);
}

void load_fonts(TwGridWidget g)
{
    Display* dpy = XtDisplay((Widget)g);
    if (!g->grid.surface->set_fonts(dpy, g->grid.font, g->grid.bold_font)) {
        fprintf(stderr, "TwGrid: cannot load font \"%s\", using fixed\n",
                g->grid.font ? g->grid.font : "");
        g->grid.surface->set_fonts(dpy, "fixed", 0);
    }
}

void call(TwGridWidget g, XtCallbackList list, TwGridCallbackStruct* cb)
{
    if (list)
        XtCallCallbackList((Widget)g, list, (XtPointer)cb);
}

/* Latin-1 bytes from XLookupString to UTF-8. */
int latin1_to_utf8(const unsigned char* in, int n, char* out, int max)
{
    int o = 0;
    for (int i = 0; i < n && o + 2 < max; i++) {
        unsigned char c = in[i];
        if (c < 0x80) {
            out[o++] = (char)c;
        } else {
            out[o++] = (char)(0xc0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3f));
        }
    }
    out[o] = 0;
    return o;
}

/* ---- class methods -------------------------------------------------------- */

void initialize(Widget req, Widget nw, ArgList, Cardinal*)
{
    (void)req;
    TwGridWidget g = (TwGridWidget)nw;
    g->grid.surface = new tw::Surface();
    g->grid.sched = new GridScheduler(g);
    g->grid.frame_timer = 0;
    g->grid.blink_timer = 0;
    g->grid.cols_now = 0;
    g->grid.rows_now = 0;
    g->grid.surface->set_margins(k_margin, k_margin);
    g->grid.surface->set_frame_delay(g->grid.frame_delay);
    g->grid.surface->set_blink_interval(g->grid.blink_interval);
    load_fonts(g);
    Dimension w, h;
    preferred(g, &w, &h);
    if (g->core.width == 0)
        g->core.width = w;
    if (g->core.height == 0)
        g->core.height = h;
    if (g->grid.source)
        g->grid.surface->set_source(g->grid.source);
}

void destroy(Widget w)
{
    TwGridWidget g = (TwGridWidget)w;
    if (g->grid.frame_timer)
        XtRemoveTimeOut(g->grid.frame_timer);
    if (g->grid.blink_timer)
        XtRemoveTimeOut(g->grid.blink_timer);
    delete g->grid.surface;
    delete g->grid.sched;
    g->grid.surface = 0;
    g->grid.sched = 0;
}

void do_resize(Widget w)
{
    TwGridWidget g = (TwGridWidget)w;
    int cols, rows;
    g->grid.surface->resize(g->core.width, g->core.height, &cols, &rows);
    if (cols != g->grid.cols_now || rows != g->grid.rows_now) {
        g->grid.cols_now = cols;
        g->grid.rows_now = rows;
        TwGridCallbackStruct cb;
        memset(&cb, 0, sizeof(cb));
        cb.reason = TW_CR_RESIZE;
        cb.cols = cols;
        cb.rows = rows;
        call(g, g->grid.resize_cb, &cb);
    }
}

void realize(Widget w, XtValueMask* mask, XSetWindowAttributes* attrs)
{
    TwGridWidget g = (TwGridWidget)w;
    /* We paint every pixel: no background clears, no flicker, and keep
     * the contents where they are when the window grows. */
    *mask |= CWBitGravity | CWBackPixmap;
    *mask &= ~(XtValueMask)CWBackPixel;
    attrs->bit_gravity = NorthWestGravity;
    attrs->background_pixmap = None;
    XtCreateWindow(w, InputOutput, CopyFromParent, *mask, attrs);
    XWindowAttributes wa;
    XGetWindowAttributes(XtDisplay(w), XtWindow(w), &wa);
    g->grid.surface->attach(XtDisplay(w), XtWindow(w), wa.visual, g->core.colormap,
                            g->core.depth, g->grid.sched);
    do_resize(w);
}

void expose(Widget w, XEvent*, Region)
{
    TwGridWidget g = (TwGridWidget)w;
    g->grid.surface->expose();
}

Boolean set_values(Widget old, Widget req, Widget nw, ArgList, Cardinal*)
{
    (void)req;
    TwGridWidget o = (TwGridWidget)old;
    TwGridWidget g = (TwGridWidget)nw;
    Boolean redisplay = False;
    if (o->grid.font != g->grid.font || o->grid.bold_font != g->grid.bold_font) {
        load_fonts(g);
        do_resize(nw);
        redisplay = True;
    }
    if (o->grid.columns != g->grid.columns || o->grid.rows != g->grid.rows) {
        Dimension w, h;
        preferred(g, &w, &h);
        g->core.width = w;
        g->core.height = h;
    }
    if (o->grid.source != g->grid.source)
        g->grid.surface->set_source(g->grid.source);
    g->grid.surface->set_frame_delay(g->grid.frame_delay);
    g->grid.surface->set_blink_interval(g->grid.blink_interval);
    return redisplay;
}

XtGeometryResult query_geometry(Widget w, XtWidgetGeometry* in, XtWidgetGeometry* out)
{
    TwGridWidget g = (TwGridWidget)w;
    Dimension pw, ph;
    preferred(g, &pw, &ph);
    out->request_mode = CWWidth | CWHeight;
    out->width = pw;
    out->height = ph;
    if ((in->request_mode & CWWidth) && (in->request_mode & CWHeight) &&
        in->width == pw && in->height == ph)
        return XtGeometryYes;
    if (pw == g->core.width && ph == g->core.height)
        return XtGeometryNo;
    return XtGeometryAlmost;
}

/* ---- actions ---------------------------------------------------------------- */

void act_key(Widget w, XEvent* ev, String*, Cardinal*)
{
    TwGridWidget g = (TwGridWidget)w;
    if (ev->type != KeyPress)
        return;
    TwGridCallbackStruct cb;
    memset(&cb, 0, sizeof(cb));
    cb.reason = TW_CR_KEY;
    cb.event = ev;
    unsigned char raw[32];
    KeySym ks = NoSymbol;
    int n = XLookupString(&ev->xkey, (char*)raw, sizeof(raw), &ks, 0);
    cb.keysym = ks;
    cb.state = ev->xkey.state;
    cb.text_len = latin1_to_utf8(raw, n, cb.text, sizeof(cb.text));
    call(g, g->grid.input_cb, &cb);
}

void pointer(TwGridWidget g, XEvent* ev, int reason, int x, int y, int button, unsigned state)
{
    TwGridCallbackStruct cb;
    memset(&cb, 0, sizeof(cb));
    cb.reason = reason;
    cb.event = ev;
    cb.x = x;
    cb.y = y;
    cb.button = button;
    cb.state = state;
    g->grid.surface->cell_at(x, y, &cb.col, &cb.row);
    call(g, g->grid.pointer_cb, &cb);
}

void act_button(Widget w, XEvent* ev, String*, Cardinal*)
{
    TwGridWidget g = (TwGridWidget)w;
    if (ev->type == ButtonPress) {
        XmProcessTraversal(w, XmTRAVERSE_CURRENT);
        pointer(g, ev, TW_CR_BUTTON_PRESS, ev->xbutton.x, ev->xbutton.y,
                (int)ev->xbutton.button, ev->xbutton.state);
    } else if (ev->type == ButtonRelease) {
        pointer(g, ev, TW_CR_BUTTON_RELEASE, ev->xbutton.x, ev->xbutton.y,
                (int)ev->xbutton.button, ev->xbutton.state);
    }
}

void act_motion(Widget w, XEvent* ev, String*, Cardinal*)
{
    if (ev->type != MotionNotify)
        return;
    pointer((TwGridWidget)w, ev, TW_CR_MOTION, ev->xmotion.x, ev->xmotion.y, 0,
            ev->xmotion.state);
}

void focus(TwGridWidget g, XEvent* ev, bool in)
{
    if (ev->type == FocusIn || ev->type == FocusOut) {
        /* Ignore focus moving between our own windows. */
        if (ev->xfocus.detail == NotifyInferior || ev->xfocus.detail == NotifyPointer)
            return;
    }
    g->grid.surface->set_focus(in);
    TwGridCallbackStruct cb;
    memset(&cb, 0, sizeof(cb));
    cb.reason = in ? TW_CR_FOCUS_IN : TW_CR_FOCUS_OUT;
    cb.event = ev;
    call(g, g->grid.focus_cb, &cb);
}

void act_focus_in(Widget w, XEvent* ev, String*, Cardinal*)
{
    focus((TwGridWidget)w, ev, true);
}

void act_focus_out(Widget w, XEvent* ev, String*, Cardinal*)
{
    focus((TwGridWidget)w, ev, false);
}

XtActionsRec k_actions[] = {
    { (String)"TwKey", act_key },
    { (String)"TwButton", act_button },
    { (String)"TwMotion", act_motion },
    { (String)"TwFocusIn", act_focus_in },
    { (String)"TwFocusOut", act_focus_out }
};

/* Every key goes to the model: no Motif traversal on Tab or arrows. */
const char k_translations[] =
    "<KeyPress>: TwKey()\n"
    "<BtnDown>: TwButton()\n"
    "<BtnUp>: TwButton()\n"
    "<Motion>: TwMotion()\n"
    "<FocusIn>: TwFocusIn()\n"
    "<FocusOut>: TwFocusOut()";

} /* namespace */

static TwGridClassRec twGridClassRec = {
    {
        /* superclass */          (WidgetClass)&xmPrimitiveClassRec,
        /* class_name */          (String)"TwGrid",
        /* widget_size */         sizeof(TwGridRec),
        /* class_initialize */    0,
        /* class_part_init */     0,
        /* class_inited */        False,
        /* initialize */          initialize,
        /* initialize_hook */     0,
        /* realize */             realize,
        /* actions */             k_actions,
        /* num_actions */         XtNumber(k_actions),
        /* resources */           k_resources,
        /* num_resources */       XtNumber(k_resources),
        /* xrm_class */           NULLQUARK,
        /* compress_motion */     True,
        /* compress_exposure */   XtExposeCompressMaximal | XtExposeGraphicsExposeMerged,
        /* compress_enterleave */ True,
        /* visible_interest */    False,
        /* destroy */             destroy,
        /* resize */              do_resize,
        /* expose */              expose,
        /* set_values */          set_values,
        /* set_values_hook */     0,
        /* set_values_almost */   XtInheritSetValuesAlmost,
        /* get_values_hook */     0,
        /* accept_focus */        0,
        /* version */             XtVersion,
        /* callback_private */    0,
        /* tm_table */            (String)k_translations,
        /* query_geometry */      query_geometry,
        /* display_accelerator */ 0,
        /* extension */           0
    },
    {
        /* border_highlight */    XmInheritBorderHighlight,
        /* border_unhighlight */  XmInheritBorderUnhighlight,
        /* translations */        0,
        /* arm_and_activate */    0,
        /* syn_resources */       0,
        /* num_syn_resources */   0,
        /* extension */           0
    },
    {
        /* extension */           0
    }
};

WidgetClass twGridWidgetClass = (WidgetClass)&twGridClassRec;

Widget TwCreateGrid(Widget parent, const char* name, ArgList args, Cardinal n)
{
    return XtCreateWidget(name, twGridWidgetClass, parent, args, n);
}

void TwGridDamage(Widget w)
{
    TwGridWidget g = (TwGridWidget)w;
    if (g->grid.surface)
        g->grid.surface->damage();
}

void TwGridSetSource(Widget w, tw::Source* src)
{
    TwGridWidget g = (TwGridWidget)w;
    g->grid.source = src;
    g->grid.surface->set_source(src);
}

void TwGridGetCellSize(Widget w, int* width, int* height)
{
    TwGridWidget g = (TwGridWidget)w;
    *width = g->grid.surface->cell_width();
    *height = g->grid.surface->cell_height();
}

void TwGridSetSize(Widget w, int cols, int rows)
{
    XtVaSetValues(w, TwNcolumns, cols, TwNrows, rows, (char*)0);
}

tw::Surface* TwGridSurface(Widget w)
{
    return ((TwGridWidget)w)->grid.surface;
}
