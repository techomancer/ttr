/*
 * tw/grid_widget.h - TwGrid, the Motif widget around a tw::Surface
 * (DESIGN.md section 7). An XmPrimitive subclass: it takes keyboard focus
 * like any Motif widget and reports keys, pointer, resize and focus
 * through callbacks; it knows nothing about what the model is.
 */
#ifndef TW_GRID_WIDGET_H
#define TW_GRID_WIDGET_H

#include <Xm/Xm.h>

namespace tw {
class Source;
class Surface;
}

extern WidgetClass twGridWidgetClass;

/* Resources. */
#define TwNsource ((char*)"source")
#define TwNfont ((char*)"font")
#define TwNboldFont ((char*)"boldFont")
#define TwNcolumns ((char*)"columns")
#define TwNrows ((char*)"rows")
#define TwNframeDelay ((char*)"frameDelay")
#define TwNblinkInterval ((char*)"blinkInterval")
#define TwNinputCallback ((char*)"inputCallback")
#define TwNpointerCallback ((char*)"pointerCallback")
#define TwNresizeCallback ((char*)"resizeCallback")
#define TwNfocusCallback ((char*)"focusCallback")
#define TwCSource ((char*)"Source")
#define TwCFont ((char*)"Font")
#define TwCColumns ((char*)"Columns")
#define TwCRows ((char*)"Rows")
#define TwCFrameDelay ((char*)"FrameDelay")
#define TwCBlinkInterval ((char*)"BlinkInterval")

enum {
    TW_CR_KEY = 1,
    TW_CR_BUTTON_PRESS,
    TW_CR_BUTTON_RELEASE,
    TW_CR_MOTION,
    TW_CR_RESIZE,
    TW_CR_FOCUS_IN,
    TW_CR_FOCUS_OUT
};

struct TwGridCallbackStruct {
    int reason;
    XEvent* event;
    /* TW_CR_KEY */
    KeySym keysym;
    unsigned int state;         /* X modifier state */
    char text[64];              /* what the key types, UTF-8 where the locale is */
    int text_len;
    /* pointer: cell and pixel position, button (1-5) */
    int col, row;
    int x, y;
    int button;
    /* TW_CR_RESIZE: the new size in cells */
    int cols, rows;
};

Widget TwCreateGrid(Widget parent, const char* name, ArgList args, Cardinal n);
/* The model changed: schedule a frame. */
void TwGridDamage(Widget w);
void TwGridSetSource(Widget w, tw::Source* src);
void TwGridGetCellSize(Widget w, int* width, int* height);
/* Size the widget (and ask its parent) for cols x rows cells. */
void TwGridSetSize(Widget w, int cols, int rows);
tw::Surface* TwGridSurface(Widget w);

#endif
