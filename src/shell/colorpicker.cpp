#include "shell/colorpicker.h"
#include "core/scale.h"
#include "core/theme.h"     /* parse_hex_color() */

#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Scale.h>
#include <Xm/Separator.h>
#include <Xm/TextF.h>

#include <stdio.h>
#include <string.h>

namespace ttr {

/* The 16 VGA colors, then ttr's own green and blue. */
static const unsigned long k_basic[] = {
    0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080,
    0xc0c0c0, 0x808080, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff,
    0x00ffff, 0xffffff, 0x11a66d, 0x2344d9
};
static const int k_nbasic = (int)(sizeof(k_basic) / sizeof(k_basic[0]));

static XmString xmstr(const char* s)
{
    return XmStringCreateLocalized((char*)s);
}

Pixel alloc_color(Display* dpy, const Rgb& c, Pixel* old, bool* have)
{
    Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
    if (*have)
        XFreeColors(dpy, cmap, old, 1, 0);
    XColor xc;
    xc.red = c.r;
    xc.green = c.g;
    xc.blue = c.b;
    xc.flags = DoRed | DoGreen | DoBlue;
    if (XAllocColor(dpy, cmap, &xc)) {
        *old = xc.pixel;
        *have = true;
        return xc.pixel;
    }
    *have = false;
    return BlackPixel(dpy, DefaultScreen(dpy));
}

ColorPicker::ColorPicker(Chosen chosen, void* closure)
    : chosen_(chosen), closure_(closure), dialog_(0), swatch_(0), hex_(0),
      updating_(false), pixel_(0), have_pixel_(false), nbasic_(0)
{
}

ColorPicker::~ColorPicker()
{
    if (dialog_) {
        XtRemoveCallback(dialog_, XmNdestroyCallback, destroyed_cb,
                         (XtPointer)this);
        free_colors();
    }
}

void ColorPicker::free_colors()
{
    if (!dialog_)
        return;
    Display* dpy = XtDisplay(dialog_);
    Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
    if (have_pixel_)
        XFreeColors(dpy, cmap, &pixel_, 1, 0);
    have_pixel_ = false;
    if (nbasic_ > 0)
        XFreeColors(dpy, cmap, basic_, nbasic_, 0);
    nbasic_ = 0;
}

static Widget hrow(Widget parent)
{
    Arg args[3];
    XtSetArg(args[0], XmNorientation, XmHORIZONTAL);
    XtSetArg(args[1], XmNmarginWidth, 0);
    XtSetArg(args[2], XmNmarginHeight, 0);
    Widget rc = XmCreateRowColumn(parent, (char*)"row", args, 3);
    XtManageChild(rc);
    return rc;
}

static Widget label(Widget parent, const char* text, int width)
{
    XmString l = xmstr(text);
    Arg args[4];
    int n = 0;
    XtSetArg(args[n], XmNlabelString, l); n++;
    if (width > 0) {
        XtSetArg(args[n], XmNwidth, width); n++;
        XtSetArg(args[n], XmNrecomputeSize, False); n++;
        XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
    }
    Widget w = XmCreateLabel(parent, (char*)"label", args, n);
    XmStringFree(l);
    XtManageChild(w);
    return w;
}

void ColorPicker::build(Widget parent)
{
    Arg args[8];
    int n = 0;
    XtSetArg(args[n], XmNautoUnmanage, False); n++;
    XtSetArg(args[n], XmNmarginWidth, scaled(10)); n++;
    XtSetArg(args[n], XmNmarginHeight, scaled(10)); n++;
    dialog_ = XmCreateFormDialog(parent, (char*)"colorPicker", args, n);
    XtAddCallback(dialog_, XmNdestroyCallback, destroyed_cb, (XtPointer)this);

    n = 0;
    XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
    XtSetArg(args[n], XmNspacing, scaled(6)); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget col = XmCreateRowColumn(dialog_, (char*)"column", args, n);
    XtManageChild(col);

    /* The swatch: a drawing area in a sunken frame. */
    n = 0;
    XtSetArg(args[n], XmNshadowType, XmSHADOW_IN); n++;
    Widget frame = XmCreateFrame(col, (char*)"swatchFrame", args, n);
    XtManageChild(frame);
    n = 0;
    XtSetArg(args[n], XmNwidth, scaled(300)); n++;
    XtSetArg(args[n], XmNheight, scaled(56)); n++;
    XtSetArg(args[n], XmNresizePolicy, XmRESIZE_NONE); n++;
    XtSetArg(args[n], XmNtraversalOn, False); n++;
    swatch_ = XmCreateDrawingArea(frame, (char*)"swatch", args, n);
    XtManageChild(swatch_);

    static const char* const names[3] = { "Red:", "Green:", "Blue:" };
    int i;
    for (i = 0; i < 3; i++) {
        Widget r = hrow(col);
        label(r, names[i], scaled(70));
        n = 0;
        XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
        XtSetArg(args[n], XmNminimum, 0); n++;
        XtSetArg(args[n], XmNmaximum, 255); n++;
        XtSetArg(args[n], XmNshowValue, True); n++;
        XtSetArg(args[n], XmNscaleWidth, scaled(230)); n++;
        scales_[i] = XmCreateScale(r, (char*)"channel", args, n);
        XtAddCallback(scales_[i], XmNvalueChangedCallback, scale_cb,
                      (XtPointer)this);
        XtAddCallback(scales_[i], XmNdragCallback, scale_cb, (XtPointer)this);
        XtManageChild(scales_[i]);
    }

    Widget r = hrow(col);
    label(r, "Hex:", scaled(70));
    n = 0;
    XtSetArg(args[n], XmNcolumns, 8); n++;
    XtSetArg(args[n], XmNmaxLength, 7); n++;
    hex_ = XmCreateTextField(r, (char*)"hex", args, n);
    XtAddCallback(hex_, XmNvalueChangedCallback, hex_cb, (XtPointer)this);
    XtManageChild(hex_);

    /* Basic colors, in two rows. */
    Display* dpy = XtDisplay(parent);
    Widget rows[2];
    rows[0] = hrow(col);
    rows[1] = hrow(col);
    for (i = 0; i < k_nbasic; i++) {
        Pixel p;
        bool have = false;
        p = alloc_color(dpy, Rgb::hex(k_basic[i]), &p, &have);
        if (have)
            basic_[nbasic_++] = p;
        XmString empty = xmstr(" ");
        n = 0;
        XtSetArg(args[n], XmNlabelString, empty); n++;
        XtSetArg(args[n], XmNwidth, scaled(26)); n++;
        XtSetArg(args[n], XmNheight, scaled(22)); n++;
        XtSetArg(args[n], XmNrecomputeSize, False); n++;
        XtSetArg(args[n], XmNuserData, (XtPointer)(long)i); n++;
        Widget b = XmCreatePushButton(rows[i < k_nbasic / 2 ? 0 : 1],
                                      (char*)"basic", args, n);
        XmStringFree(empty);
        XmChangeColor(b, p);
        XtAddCallback(b, XmNactivateCallback, basic_cb, (XtPointer)this);
        XtManageChild(b);
    }

    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, col); n++;
    XtSetArg(args[n], XmNtopOffset, scaled(10)); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget sep = XmCreateSeparator(dialog_, (char*)"sep", args, n);
    XtManageChild(sep);

    XmString ok = xmstr("OK"), cancel = xmstr("Cancel");
    n = 0;
    XtSetArg(args[n], XmNlabelString, cancel); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, sep); n++;
    XtSetArg(args[n], XmNtopOffset, scaled(6)); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget cancel_b = XmCreatePushButton(dialog_, (char*)"cancel", args, n);
    n = 0;
    XtSetArg(args[n], XmNlabelString, ok); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, sep); n++;
    XtSetArg(args[n], XmNtopOffset, scaled(6)); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNrightWidget, cancel_b); n++;
    Widget ok_b = XmCreatePushButton(dialog_, (char*)"ok", args, n);
    XmStringFree(ok);
    XmStringFree(cancel);
    XtAddCallback(ok_b, XmNactivateCallback, ok_cb, (XtPointer)this);
    XtAddCallback(cancel_b, XmNactivateCallback, cancel_cb, (XtPointer)this);
    XtManageChild(ok_b);
    XtManageChild(cancel_b);
    XtSetArg(args[0], XmNdefaultButton, ok_b);
    XtSetArg(args[1], XmNcancelButton, cancel_b);
    XtSetValues(dialog_, args, 2);
}

void ColorPicker::show(Widget parent, const char* title, const Rgb& start)
{
    if (!dialog_)
        build(parent);
    XmString t = xmstr(title);
    Arg args[1];
    XtSetArg(args[0], XmNdialogTitle, t);
    XtSetValues(dialog_, args, 1);
    XmStringFree(t);
    set(start, false);
    XtManageChild(dialog_);
}

/* Show 'c' in the swatch, sliders and (unless it came from there) the hex
 * field. */
void ColorPicker::set(const Rgb& c, bool from_text)
{
    color_ = c;
    updating_ = true;
    XmScaleSetValue(scales_[0], c.r >> 8);
    XmScaleSetValue(scales_[1], c.g >> 8);
    XmScaleSetValue(scales_[2], c.b >> 8);
    if (!from_text) {
        char hex[8];
        format_hex_color(c, hex);
        XmTextFieldSetString(hex_, hex);
    }
    updating_ = false;

    Pixel p = alloc_color(XtDisplay(dialog_), c, &pixel_, &have_pixel_);
    Arg args[1];
    XtSetArg(args[0], XmNbackground, p);
    XtSetValues(swatch_, args, 1);
    if (XtIsRealized(swatch_))
        XClearWindow(XtDisplay(swatch_), XtWindow(swatch_));
}

void ColorPicker::scale_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ColorPicker* self = (ColorPicker*)closure;
    if (self->updating_)
        return;
    int v[3], i;
    for (i = 0; i < 3; i++)
        XmScaleGetValue(self->scales_[i], &v[i]);
    self->set(Rgb((unsigned short)(v[0] * 257), (unsigned short)(v[1] * 257),
                  (unsigned short)(v[2] * 257)), false);
}

void ColorPicker::hex_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)call;
    ColorPicker* self = (ColorPicker*)closure;
    if (self->updating_)
        return;
    char* s = XmTextFieldGetString(w);
    /* Only whole colors, so typing does not jump around; the # may be
     * left out. */
    char buf[16];
    if (s && strlen(s) == 6)
        sprintf(buf, "#%s", s);
    else
        sprintf(buf, "%.8s", s ? s : "");
    Rgb c;
    if (strlen(buf) == 7 && parse_hex_color(buf, &c))
        self->set(c, true);
    XtFree(s);
}

void ColorPicker::basic_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)call;
    XtPointer data = 0;
    Arg args[1];
    XtSetArg(args[0], XmNuserData, &data);
    XtGetValues(w, args, 1);
    int i = (int)(long)data;
    if (i >= 0 && i < k_nbasic)
        ((ColorPicker*)closure)->set(Rgb::hex(k_basic[i]), false);
}

void ColorPicker::ok_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ColorPicker* self = (ColorPicker*)closure;
    XtUnmanageChild(self->dialog_);
    self->chosen_(self->closure_, self->color_);
}

void ColorPicker::cancel_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    XtUnmanageChild(((ColorPicker*)closure)->dialog_);
}

void ColorPicker::destroyed_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ColorPicker* self = (ColorPicker*)closure;
    self->free_colors();
    self->dialog_ = 0;
}

}
