#include "shell/colorspage.h"
#include "shell/colorpicker.h"
#include "shell/settings.h"
#include "core/frame.h"
#include "core/painter.h"
#include "core/scale.h"
#include "core/settings.h"
#include "x11/xpainter.h"

#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace ttr {

/* What the Settings page calls each of the user's colors. */
static const char* const k_color_labels[k_ncolor_keys] = {
    "Background:", "Text:", "Active title bar:", "Active title text:",
    "Inactive title bar:", "Inactive title text:"
};

static XmString xmstr(const char* s)
{
    return XmStringCreateLocalized((char*)s);
}

static int compare_nocase(const char* a, const char* b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static Widget push(Widget parent, const char* label, XtCallbackProc cb,
                   XtPointer closure)
{
    XmString l = xmstr(label);
    Arg args[1];
    XtSetArg(args[0], XmNlabelString, l);
    Widget b = XmCreatePushButton(parent, (char*)"button", args, 1);
    XmStringFree(l);
    XtAddCallback(b, XmNactivateCallback, cb, closure);
    XtManageChild(b);
    return b;
}

ColorsPage::ColorsPage(SettingsDialog* dialog, Widget page, const Config& cfg)
    : top_(dialog->widget()), current_(-1), picking_(-1)
{
    Display* dpy = XtDisplay(top_);
    painter_ = new XPainter(dpy, DefaultScreen(dpy));
    const char* fonts[3] = { cfg.title_xfont.c_str(), "fixed", 0 };
    painter_->load_font(fonts);
    picker_ = new ColorPicker(picked, this);

    list_themes(&themes_);
    base_ = Theme::classic();
    int i;
    for (i = 0; i < themes_.size(); i++)
        if (compare_nocase(themes_[i]->name.c_str(),
                           cfg.theme_name.c_str()) == 0) {
            Theme t;
            if (t.load(themes_[i]->path.c_str())) {
                current_ = i;
                base_ = t;
                base_.name = themes_[i]->name;
            }
            break;
        }
    for (i = 0; i < k_ncolor_keys; i++) {
        mine_[i] = false;
        have_pixel_[i] = false;
        slots_[i].page = this;
        slots_[i].index = i;
        XColor c;
        const char* v = cfg.colors[i].c_str();
        if (*v && XParseColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)),
                              v, &c)) {
            mine_[i] = true;
            color_[i] = Rgb(c.red, c.green, c.blue);
        }
    }

    /* The theme list and the preview, side by side. */
    Widget f = dialog->frame(page, "Theme");
    Arg args[10];
    int n = 0;
    XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
    XtSetArg(args[n], XmNspacing, scaled(10)); n++;
    XtSetArg(args[n], XmNmarginWidth, 0); n++;
    Widget side = XmCreateRowColumn(f, (char*)"side", args, n);
    XtManageChild(side);

    n = 0;
    XtSetArg(args[n], XmNvisibleItemCount, 9); n++;
    XtSetArg(args[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(args[n], XmNscrollBarDisplayPolicy, XmSTATIC); n++;
    list_ = XmCreateScrolledList(side, (char*)"themes", args, n);
    XmString s = xmstr("Classic  (ttr)");
    XmListAddItemUnselected(list_, s, 0);
    XmStringFree(s);
    for (i = 0; i < themes_.size(); i++) {
        char label[400];
        sprintf(label, "%.300s  (%.50s)", themes_[i]->name.c_str(),
                themes_[i]->origin.c_str());
        s = xmstr(label);
        XmListAddItemUnselected(list_, s, 0);
        XmStringFree(s);
    }
    XtAddCallback(list_, XmNbrowseSelectionCallback, list_cb,
                  (XtPointer)this);
    XtManageChild(list_);

    n = 0;
    XtSetArg(args[n], XmNshadowType, XmSHADOW_IN); n++;
    Widget pf = XmCreateFrame(side, (char*)"previewFrame", args, n);
    XtManageChild(pf);
    n = 0;
    XtSetArg(args[n], XmNwidth, scaled(330)); n++;
    XtSetArg(args[n], XmNheight, scaled(170)); n++;
    XtSetArg(args[n], XmNresizePolicy, XmRESIZE_NONE); n++;
    XtSetArg(args[n], XmNtraversalOn, False); n++;
    preview_ = XmCreateDrawingArea(pf, (char*)"preview", args, n);
    XtAddCallback(preview_, XmNexposeCallback, expose_cb, (XtPointer)this);
    XtManageChild(preview_);

    /* The user's colors, two to a row. */
    f = dialog->frame(page, "Your colors");
    Widget r = 0;
    for (i = 0; i < k_ncolor_keys; i++) {
        if (i % 2 == 0)
            r = dialog->row(f, k_color_labels[i]);
        else {
            XmString l = xmstr(k_color_labels[i]);
            n = 0;
            XtSetArg(args[n], XmNlabelString, l); n++;
            XtSetArg(args[n], XmNwidth, scaled(150)); n++;
            XtSetArg(args[n], XmNrecomputeSize, False); n++;
            XtSetArg(args[n], XmNalignment, XmALIGNMENT_END); n++;
            XtManageChild(XmCreateLabel(r, (char*)"label", args, n));
            XmStringFree(l);
        }
        XmString l = xmstr("#000000");
        n = 0;
        XtSetArg(args[n], XmNlabelString, l); n++;
        XtSetArg(args[n], XmNwidth, scaled(90)); n++;
        XtSetArg(args[n], XmNrecomputeSize, False); n++;
        swatch_[i] = XmCreatePushButton(r, (char*)"swatch", args, n);
        XmStringFree(l);
        XtAddCallback(swatch_[i], XmNactivateCallback, change_cb,
                      (XtPointer)&slots_[i]);
        XtManageChild(swatch_[i]);
        reset_[i] = push(r, "Theme", reset_cb, (XtPointer)&slots_[i]);
    }
    XmString note = xmstr("Click a color to choose your own; Theme goes back "
                          "to the theme's.");
    XtSetArg(args[0], XmNlabelString, note);
    XtManageChild(XmCreateLabel(page, (char*)"note", args, 1));
    XmStringFree(note);

    XmListSelectPos(list_, current_ + 2, False);
    XmListSetBottomPos(list_, current_ + 2);
    update_swatches();
}

ColorsPage::~ColorsPage()
{
    Display* dpy = XtDisplay(top_);
    Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
    int i;
    for (i = 0; i < k_ncolor_keys; i++)
        if (have_pixel_[i])
            XFreeColors(dpy, cmap, &pixel_[i], 1, 0);
    delete picker_;
    delete painter_;
    free_themes(&themes_);
}

Theme ColorsPage::effective() const
{
    Theme t = base_;
    int i;
    for (i = 0; i < k_ncolor_keys; i++)
        if (mine_[i])
            t.put(k_color_roles[i], color_[i]);
    return t;
}

void ColorsPage::select_theme(int i)
{
    current_ = -1;
    base_ = Theme::classic();
    if (i >= 0 && i < themes_.size()) {
        Theme t;
        if (t.load(themes_[i]->path.c_str())) {
            current_ = i;
            base_ = t;
            base_.name = themes_[i]->name;
        }
    }
    update_swatches();
}

/* Each swatch shows its color, the user's or the theme's, and its hex. */
void ColorsPage::update_swatches()
{
    Theme t = effective();
    Display* dpy = XtDisplay(top_);
    int i;
    for (i = 0; i < k_ncolor_keys; i++) {
        Rgb c = t.get(k_color_roles[i]);
        char hex[16];
        format_hex_color(c, hex);
        XmString l = xmstr(hex);
        Arg args[1];
        XtSetArg(args[0], XmNlabelString, l);
        XtSetValues(swatch_[i], args, 1);
        XmStringFree(l);
        XmChangeColor(swatch_[i], alloc_color(dpy, c, &pixel_[i],
                                              &have_pixel_[i]));
        XtSetSensitive(reset_[i], mine_[i]);
    }
    paint_preview();
}

/* A bit of desktop in the theme: an inactive window, the active one over
 * it, and a taskbar, drawn by the code that draws the real ones. */
void ColorsPage::paint_preview()
{
    if (!XtIsRealized(preview_))
        return;
    Dimension dw = 0, dh = 0;
    Arg args[2];
    XtSetArg(args[0], XmNwidth, &dw);
    XtSetArg(args[1], XmNheight, &dh);
    XtGetValues(preview_, args, 2);
    int w = dw, h = dh;
    Theme t = effective();
    XPainter& p = *painter_;
    p.begin(XtWindow(preview_), w, h);
    p.fill(Rect(0, 0, w, h), Rgb::hex(0x405060));   /* the desktop */

    FrameTheme ft;
    ft.fit_font(p.text_ascent() + p.text_descent());
    ft.active = motif_colors(t.get(TR_ACTIVE));
    ft.active.fg = t.get(TR_ACTIVE_TEXT);
    ft.inactive = motif_colors(t.get(TR_INACTIVE));
    ft.inactive.fg = t.get(TR_INACTIVE_TEXT);
    FrameDecor d;
    ColorSet panel = motif_colors(t.get(TR_BACKGROUND));
    panel.fg = t.get(TR_FOREGROUND);

    int bar_h = p.text_ascent() + p.text_descent() + scaled(10);
    int fw = w * 6 / 10, fh = (h - bar_h) * 6 / 10;
    int i;
    for (i = 0; i < 2; i++) {
        bool active = i == 1;
        int x = active ? w - fw - scaled(8) : scaled(8);
        int y = active ? h - bar_h - fh - scaled(6) : scaled(6);
        OffsetPainter op(p, x, y);
        paint_frame(op, ft, d, fw, fh, active ? "Active" : "Inactive",
                    active, FP_NONE);
        /* The window's contents: a panel with a text field. */
        FrameExtents e = ft.extents(d);
        Rect inside(x + e.left, y + e.top, fw - e.width(), fh - e.height());
        p.fill(inside, panel.bg);
        Rect field(inside.x + scaled(6), inside.y + scaled(6),
                   inside.w - scaled(12), p.text_ascent() +
                   p.text_descent() + scaled(6));
        ColorSet fc = motif_colors(t.get(TR_TEXT_FIELD));
        p.fill(field, fc.bg);
        draw_bevel(p, field, scaled_line(2), panel.bottom, panel.top);
        const char* sample = active ? "Text" : "Sample";
        p.text(field.x + scaled(5), field.y + scaled(3) + p.text_ascent(),
               sample, (int)strlen(sample),
               t.set[TR_FOREGROUND] ? t.color[TR_FOREGROUND] : fc.fg);
    }

    /* The taskbar, with the Start button. */
    Rect bar(0, h - bar_h, w, bar_h);
    p.fill(bar, panel.bg);
    draw_bevel(p, bar, scaled_line(1), panel.top, panel.bottom);
    const char* start = "Start";
    int sw = p.text_width(start, 5) + scaled(14);
    Rect sb(scaled(3), bar.y + scaled(3), sw, bar_h - scaled(6));
    draw_bevel(p, sb, scaled_line(2), panel.top, panel.bottom);
    p.text(sb.x + scaled(7), sb.y + (sb.h - p.text_ascent() -
                                     p.text_descent()) / 2 + p.text_ascent(),
           start, 5, panel.fg);
    p.end();
}

void ColorsPage::expose_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((ColorsPage*)closure)->paint_preview();
}

void ColorsPage::list_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    XmListCallbackStruct* cbs = (XmListCallbackStruct*)call;
    ((ColorsPage*)closure)->select_theme(cbs->item_position - 2);
}

void ColorsPage::change_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    Slot* slot = (Slot*)closure;
    ColorsPage* self = slot->page;
    self->picking_ = slot->index;
    char title[64];
    sprintf(title, "%.40s", k_color_labels[slot->index]);
    int n = (int)strlen(title);
    if (n > 0 && title[n - 1] == ':')
        title[n - 1] = 0;
    self->picker_->show(self->top_, title,
                        self->effective().get(k_color_roles[slot->index]));
}

void ColorsPage::picked(void* closure, const Rgb& c)
{
    ColorsPage* self = (ColorsPage*)closure;
    int i = self->picking_;
    if (i < 0)
        return;
    self->mine_[i] = true;
    self->color_[i] = c;
    self->update_swatches();
}

void ColorsPage::reset_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    Slot* slot = (Slot*)closure;
    slot->page->mine_[slot->index] = false;
    slot->page->update_swatches();
}

void ColorsPage::save(SettingsFile* file)
{
    file->set("theme", current_ >= 0 ? themes_[current_]->name.c_str()
                                     : "Classic");
    int i;
    for (i = 0; i < k_ncolor_keys; i++) {
        if (mine_[i]) {
            char hex[16];
            format_hex_color(color_[i], hex);
            file->set(k_color_keys[i], hex);
        } else {
            file->remove(k_color_keys[i]);
        }
    }
}

}
