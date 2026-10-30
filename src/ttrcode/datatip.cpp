/*
 * ttrcode's DataTips (td/DESIGN.md section 8.2): while the program is
 * stopped, resting the mouse on a variable in the editor shows its value
 * in a small pale yellow window, as Visual C++ 6 did: "count = 42",
 * "p = 0x0804a000 {x=1 y=2}". The expression is the word under the mouse
 * with the members before it ("p->next->val" over "val"), or the
 * selection when the mouse is on it; it is evaluated in the frame shown.
 * Moving off it, a key, a click or the program running hides it.
 */
#include "app.h"

#include "../../th/motif.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Shell.h>
#include <Xm/Label.h>

namespace ttrcode {

namespace {

/* How long the mouse rests before the tip shows. */
const unsigned long k_tip_ms = 500;

const char* const k_keywords[] = {
    "auto", "bool", "break", "case", "catch", "char", "class", "const", "continue",
    "default", "delete", "do", "double", "else", "enum", "extern", "false", "float", "for",
    "goto", "if", "inline", "int", "long", "namespace", "new", "operator", "private",
    "protected", "public", "register", "return", "short", "signed", "sizeof", "static",
    "struct", "switch", "template", "this", "throw", "true", "try", "typedef", "typename",
    "union", "unsigned", "using", "virtual", "void", "volatile", "while", 0
};

bool ident(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

bool keyword(const char* s, size_t n)
{
    int i;
    for (i = 0; k_keywords[i]; i++)
        if (strlen(k_keywords[i]) == n && !strncmp(k_keywords[i], s, n))
            return true;
    return false;
}

void tip_leave(Widget, XtPointer data, XEvent*, Boolean*)
{
    ((App*)data)->tip_hide();
}

} /* namespace */

/* The expression under the editor's cell (col, row): false if there is
 * none worth evaluating (blank, a number, a keyword, a function called). */
bool App::tip_expression(int col, int row, ttr::Str* out)
{
    Doc* d = doc();
    if (!d || col < src_.gutter())
        return false;
    te::Pos p = pos_at(col, row);
    if (p.line < 0 || p.line >= d->text.lines())
        return false;
    /* On a selection within the line: the selection. */
    if (d->view.has_selection()) {
        te::Pos a, b;
        d->view.selection(&a, &b);
        if (a.line == b.line && a.line == p.line && p.col >= a.col && p.col < b.col) {
            const char* line = d->text.line(p.line);
            size_t n = (size_t)(b.col - a.col);
            if (n == 0 || n > 200)
                return false;
            char buf[256];
            memcpy(buf, line + a.col, n);
            buf[n] = 0;
            *out = buf;
            return true;
        }
    }
    const char* line = d->text.line(p.line);
    size_t len = d->text.length(p.line);
    size_t c = (size_t)p.col;
    if (c >= len || !ident(line[c]))
        return false;
    size_t a = c, b = c;
    while (a > 0 && ident(line[a - 1]))
        a--;
    while (b < len && ident(line[b]))
        b++;
    if (isdigit((unsigned char)line[a]) || keyword(line + a, b - a))
        return false;
    /* A function called: not a value. */
    size_t k = b;
    while (k < len && (line[k] == ' ' || line[k] == '\t'))
        k++;
    if (k < len && line[k] == '(')
        return false;
    /* The members it is reached through: "p->next->val", "a[i].x". */
    for (;;) {
        size_t q = a;
        if (q >= 1 && line[q - 1] == '.')
            q -= 1;
        else if (q >= 2 && line[q - 2] == '-' && line[q - 1] == '>')
            q -= 2;
        else
            break;
        size_t r = q;
        while (r > 0 && line[r - 1] == ']') {
            int depth = 0;
            size_t t = r;
            do {
                t--;
                if (line[t] == ']')
                    depth++;
                else if (line[t] == '[')
                    depth--;
            } while (t > 0 && depth > 0);
            if (depth)
                break;
            r = t;
        }
        size_t s = r;
        while (s > 0 && ident(line[s - 1]))
            s--;
        if (s == r || isdigit((unsigned char)line[s]))
            break;
        a = s;
    }
    if (b - a > 200)
        return false;
    char buf[256];
    memcpy(buf, line + a, b - a);
    buf[b - a] = 0;
    *out = buf;
    return true;
}

/* The mouse moved over the editor, no button down. */
void App::tip_motion(TwGridCallbackStruct* cb)
{
    ttr::Str expr;
    bool have = dbg_ && !dbg_->busy() && dbg_->running() && frame() &&
                tip_expression(cb->col, cb->row, &expr);
    if (have && tip_up_ && !strcmp(expr.c_str(), tip_expr_.c_str()))
        return;     /* still on it */
    tip_hide();
    if (!have)
        return;
    tip_expr_ = expr;
    if (cb->event && cb->event->type == MotionNotify) {
        tip_x_ = cb->event->xmotion.x_root;
        tip_y_ = cb->event->xmotion.y_root;
    }
    if (!tip_watch_) {
        XtAddEventHandler(grid_, LeaveWindowMask, False, tip_leave, (XtPointer)this);
        tip_watch_ = true;
    }
    tip_timer_ = XtAppAddTimeOut(XtWidgetToApplicationContext(grid_), k_tip_ms, tip_timeout,
                                 (XtPointer)this);
}

void App::tip_timeout(XtPointer data, XtIntervalId*)
{
    App* a = (App*)data;
    a->tip_timer_ = 0;
    a->tip_show();
}

void App::tip_show()
{
    const td::Frame* f = frame();
    if (!dbg_ || dbg_->busy() || !f || tip_expr_.empty())
        return;
    td::Value v = dbg_->evaluate(*f, tip_expr_.c_str());
    if (!v.ok())
        return;     /* not a variable here: no tip, as VC6 */
    ttr::Str val = dbg_->format(v);
    char text[400];
    snprintf(text, sizeof(text), "%s = %.300s%s", tip_expr_.c_str(), val.c_str(),
             strlen(val.c_str()) > 300 ? "..." : "");
    if (!tip_shell_) {
        tip_shell_ = XtVaCreatePopupShell("dataTip", overrideShellWidgetClass, top_,
                                          XmNborderWidth, 1, XmNborderColor,
                                          BlackPixelOfScreen(XtScreen(top_)), (char*)0);
        Pixel bg = th::pixel(top_, 0xffffe1);
        tip_label_ = XtVaCreateManagedWidget("text", xmLabelWidgetClass, tip_shell_,
                                             XmNbackground, bg, XmNforeground,
                                             BlackPixelOfScreen(XtScreen(top_)),
                                             XmNmarginWidth, scaled(3), XmNmarginHeight, scaled(1),
                                             XmNalignment, XmALIGNMENT_BEGINNING, (char*)0);
    }
    XmString s = XmStringCreateLocalized(text);
    XtVaSetValues(tip_label_, XmNlabelString, s, (char*)0);
    XmStringFree(s);
    XtRealizeWidget(tip_shell_);
    /* The label's size for this text (the shell keeps the last one's). */
    XtWidgetGeometry pref;
    memset(&pref, 0, sizeof(pref));
    XtQueryGeometry(tip_label_, 0, &pref);
    Dimension w = pref.width, h = pref.height;
    if (!w || !h)
        XtVaGetValues(tip_label_, XmNwidth, &w, XmNheight, &h, (char*)0);
    /* Below the line the mouse is on, kept on the screen. */
    int cw = 8, ch = 16;
    TwGridGetCellSize(grid_, &cw, &ch);
    int x = tip_x_ + 2, y = tip_y_ + ch;
    int sw = WidthOfScreen(XtScreen(top_)), sh = HeightOfScreen(XtScreen(top_));
    if (x + (int)w + 2 > sw)
        x = sw - (int)w - 2;
    if (x < 0)
        x = 0;
    if (y + (int)h + 2 > sh)
        y = tip_y_ - ch - (int)h;
    XtVaSetValues(tip_shell_, XmNx, x, XmNy, y, XmNwidth, w, XmNheight, h, (char*)0);
    XtVaSetValues(tip_label_, XmNwidth, w, XmNheight, h, (char*)0);
    XtPopup(tip_shell_, XtGrabNone);
    tip_up_ = true;
}

void App::tip_hide()
{
    if (tip_timer_) {
        XtRemoveTimeOut(tip_timer_);
        tip_timer_ = 0;
    }
    if (tip_up_ && tip_shell_)
        XtPopdown(tip_shell_);
    tip_up_ = false;
}

} /* namespace ttrcode */
