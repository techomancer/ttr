#include "dock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/cursorfont.h>
#include <Xm/CascadeB.h>
#include <Xm/Form.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>
#include <Xm/ToggleB.h>

namespace ttrcode {

namespace {

const char* const k_side_names[SIDE_COUNT] = { "left", "bottom", "right" };
const char* const k_side_labels[SIDE_COUNT] = { "Left", "Bottom", "Right" };
const int k_default_size[SIDE_COUNT] = { 220, 220, 240 };
const int k_min = 40;           /* no area or group smaller */
const int k_sash = 7;           /* a sash's thickness */

/* A sash's XmNuserData: side * 1000, plus the group before it + 1 for
 * those between groups (0 for an area's edge). */
long sash_code(int side, int group) { return (long)side * 1000 + group; }

void sash_cb(Widget w, XtPointer data, XEvent* ev, Boolean*)
{
    ((Docks*)data)->sash_event(w, ev);
}

void tab_cb(Widget w, XtPointer data, XtPointer)
{
    ((Docks*)data)->tab_chosen(w);
}

void tab_press(Widget w, XtPointer data, XEvent* ev, Boolean*)
{
    if (ev->type == ButtonPress && ev->xbutton.button == 3)
        ((Docks*)data)->tab_menu(w, ev);
}

Docks* g_menu_docks;

void menu_cb(Widget, XtPointer data, XtPointer)
{
    if (g_menu_docks)
        g_menu_docks->menu_chosen((int)(long)data);
}

/* The next field of *p up to sep (which is overwritten), or 0 at the
 * end. strtok_r is not everywhere. */
char* next_field(char** p, char sep)
{
    char* s = *p;
    if (!s)
        return 0;
    char* e = strchr(s, sep);
    if (e) {
        *e = 0;
        *p = e + 1;
    } else {
        *p = 0;
    }
    return s;
}

bool vertical_side(int side)
{
    return side != SIDE_BOTTOM;     /* left, right: groups one over another */
}

} /* namespace */

Docks::Docks(Widget body, PaneBuilder* builder, Pixel tab_fill)
    : body_(body), builder_(builder), tab_fill_(tab_fill), all_hidden_(false), in_apply_(false),
      drag_start_(-1), drag_size_(0), menu_pane_(0)
{
    column_ = XtVaCreateManagedWidget("column", xmFormWidgetClass, body_, (char*)0);
    center_ = XtVaCreateManagedWidget("center", xmFormWidgetClass, column_, (char*)0);
    parking_ = XtVaCreateWidget("parking", xmFormWidgetClass, body_, (char*)0);
    int s;
    for (s = 0; s < SIDE_COUNT; s++) {
        Area& a = areas_[s];
        a.side = (Side)s;
        a.size = k_default_size[s];
        a.hidden = false;
        Widget parent = s == SIDE_BOTTOM ? column_ : body_;
        char name[32];
        sprintf(name, "%sArea", k_side_names[s]);
        a.form = XtVaCreateWidget(name, xmFormWidgetClass, parent, XmNresizePolicy,
                                  XmRESIZE_NONE, (char*)0);
        /* The edge between an area and the documents: a line across the
         * direction it moves in. */
        a.sash = make_sash(parent, s != SIDE_BOTTOM);
        XtVaSetValues(a.sash, XmNuserData, (XtPointer)sash_code(s, 0), (char*)0);
    }
    /* Posted by tab_menu from the tabs only: Motif 2's automatic posting
     * would grab button 3 everywhere in the body. */
    Arg ma[1];
    XtSetArg(ma[0], XmNpopupEnabled, False);
    menu_ = XmCreatePopupMenu(body_, (char*)"tabMenu", ma, 1);
    relayout();
}

Docks::~Docks()
{
    int s, i;
    for (s = 0; s < SIDE_COUNT; s++)
        for (i = 0; i < areas_[s].groups.size(); i++)
            delete areas_[s].groups[i];
    if (g_menu_docks == this)
        g_menu_docks = 0;
}

Widget Docks::make_sash(Widget parent, bool vertical)
{
    Widget w = XtVaCreateWidget("sash", xmSeparatorWidgetClass, parent,
                                XmNorientation, vertical ? XmVERTICAL : XmHORIZONTAL,
                                XmNseparatorType, XmSHADOW_ETCHED_OUT,
                                vertical ? XmNwidth : XmNheight, k_sash,
                                XmNmargin, 0, (char*)0);
    XtAddEventHandler(w, ButtonPressMask | ButtonReleaseMask | Button1MotionMask, False, sash_cb,
                      (XtPointer)this);
    return w;
}

void Docks::realized()
{
    relayout();
}

void Docks::add(Pane* p)
{
    panes_.push_back(p);
}

Pane* Docks::find(const char* id) const
{
    int i;
    for (i = 0; i < panes_.size(); i++)
        if (!strcmp(panes_[i]->id(), id))
            return panes_[i];
    return 0;
}

/* ------------------------------------------------------------------ */
/* Groups and panes                                                    */

Group* Docks::new_group(Area* a, int at)
{
    Group* g = new Group;
    g->area = a;
    g->current = -1;
    g->size = 200;
    g->form = XtVaCreateWidget("group", xmFormWidgetClass, a->form, XmNresizePolicy,
                               XmRESIZE_NONE, (char*)0);
    g->tabs = XtVaCreateManagedWidget("tabs", xmRowColumnWidgetClass, g->form,
                                      XmNorientation, XmHORIZONTAL, XmNpacking, XmPACK_TIGHT,
                                      XmNspacing, 0, XmNmarginHeight, 0, XmNmarginWidth, 0,
                                      XmNtopAttachment, XmATTACH_FORM,
                                      XmNleftAttachment, XmATTACH_FORM,
                                      XmNrightAttachment, XmATTACH_FORM, (char*)0);
    /* Between this group and the next: a line across the tiling. */
    g->sash = make_sash(a->form, !vertical_side(a->side));
    if (at < 0 || at > a->groups.size())
        at = a->groups.size();
    a->groups.push_back(g);
    int i;
    for (i = a->groups.size() - 1; i > at; i--)
        a->groups[i] = a->groups[i - 1];
    a->groups[at] = g;
    XtManageChild(g->form);
    return g;
}

void Docks::drop_group(Group* g)
{
    Area* a = g->area;
    int i = a->groups.find(g);
    if (i >= 0)
        a->groups.remove_at(i);
    XtDestroyWidget(g->sash);
    XtDestroyWidget(g->form);
    delete g;
}

void Docks::add_to(Pane* p, Group* g)
{
    p->group = g;
    g->panes.push_back(p);
    XmString l = XmStringCreateLocalized((char*)p->title());
    p->tab = XtVaCreateManagedWidget("tab", xmToggleButtonWidgetClass, g->tabs,
                                     XmNlabelString, l, XmNindicatorOn, False,
                                     XmNshadowThickness, 2, XmNfillOnSelect, True,
                                     XmNselectColor, tab_fill_, XmNtraversalOn, False,
                                     XmNmarginWidth, 8, XmNuserData, (XtPointer)p,
                                     XmNset, False, (char*)0);
    XmStringFree(l);
    XtAddCallback(p->tab, XmNvalueChangedCallback, tab_cb, (XtPointer)this);
    XtAddEventHandler(p->tab, ButtonPressMask, False, tab_press, (XtPointer)this);
    p->widget = builder_->build_pane(p, g->form);
    if (p->widget)
        XtVaSetValues(p->widget, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, g->tabs,
                      XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                      XmNbottomAttachment, XmATTACH_FORM, (char*)0);
    choose(g, g->panes.size() - 1);
}

/* Out of its group (its widgets stay where they are for the builder to
 * deal with); the group itself stays even if it is now empty. */
void Docks::remove_from(Pane* p)
{
    Group* g = p->group;
    if (!g)
        return;
    int i = g->panes.find(p);
    if (i >= 0)
        g->panes.remove_at(i);
    XtDestroyWidget(p->tab);
    p->tab = 0;
    p->last_side = g->area->side;
    p->last_group = g->area->groups.find(g);
    p->group = 0;
    if (!g->panes.empty())
        choose(g, g->current >= g->panes.size() ? g->panes.size() - 1 : g->current);
}

void Docks::choose(Group* g, int i)
{
    if (i < 0 || i >= g->panes.size())
        return;
    g->current = i;
    int k;
    for (k = 0; k < g->panes.size(); k++) {
        Pane* p = g->panes[k];
        XmToggleButtonSetState(p->tab, k == i, False);
        if (!p->widget)
            continue;
        if (k == i)
            XtManageChild(p->widget);
        else
            XtUnmanageChild(p->widget);
    }
    builder_->pane_shown(g->panes[i]);
}

void Docks::show(Pane* p)
{
    if (!p->group) {
        Area* a = &areas_[p->last_side];
        Group* g;
        if (p->last_group >= 0 && p->last_group < a->groups.size())
            g = a->groups[p->last_group];
        else if (p->last_group < 0 && !a->groups.empty())
            g = a->groups[0];           /* first time: with what is there */
        else
            g = new_group(a, -1);
        add_to(p, g);
        changed();
    } else {
        choose(p->group, p->group->panes.find(p));
    }
    p->group->area->hidden = false;
    relayout();
}

void Docks::rebuild(Pane* p)
{
    Group* g = p->group;
    if (!g)
        return;
    p->widget = builder_->build_pane(p, g->form);
    if (p->widget)
        XtVaSetValues(p->widget, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, g->tabs,
                      XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                      XmNbottomAttachment, XmATTACH_FORM, (char*)0);
    choose(g, g->panes.find(p));
}

void Docks::close(Pane* p)
{
    Group* g = p->group;
    if (!g)
        return;
    remove_from(p);
    builder_->drop_pane(p);
    p->widget = 0;
    if (g->panes.empty())
        drop_group(g);
    relayout();
    changed();
}

void Docks::move(Pane* p, Side side, int group)
{
    Area* a = &areas_[side];
    Group* old = p->group;
    Group* g = group >= 0 && group < a->groups.size() ? a->groups[group] : 0;
    if (g && g == old)
        return;
    if (!g)
        g = new_group(a, -1);
    if (old)
        remove_from(p);
    add_to(p, g);           /* the builder makes its widgets in g */
    if (old && old->panes.empty())
        drop_group(old);
    a->hidden = false;
    relayout();
    changed();
}

void Docks::set_tab_fill(Pixel p)
{
    tab_fill_ = p;
    int i;
    for (i = 0; i < panes_.size(); i++)
        if (panes_[i]->tab)
            XtVaSetValues(panes_[i]->tab, XmNselectColor, p, (char*)0);
}

void Docks::set_hidden(Side s, bool on)
{
    areas_[s].hidden = on;
    relayout();
}

void Docks::set_all_hidden(bool on)
{
    all_hidden_ = on;
    relayout();
}

void Docks::changed()
{
    if (!in_apply_)
        builder_->layout_changed();
}

/* ------------------------------------------------------------------ */
/* Attachments                                                         */

void Docks::layout_area(Area* a)
{
    bool vert = vertical_side(a->side);
    int n = a->groups.size();
    int i;
    for (i = 0; i < n; i++) {
        Group* g = a->groups[i];
        bool last = i == n - 1;
        if (vert) {
            XtVaSetValues(g->form, XmNleftAttachment, XmATTACH_FORM,
                          XmNrightAttachment, XmATTACH_FORM,
                          XmNtopAttachment, i ? XmATTACH_WIDGET : XmATTACH_FORM,
                          XmNtopWidget, i ? a->groups[i - 1]->sash : (Widget)0,
                          XmNbottomAttachment, last ? XmATTACH_FORM : XmATTACH_NONE, (char*)0);
            if (!last)
                XtVaSetValues(g->form, XmNheight, (Dimension)g->size, (char*)0);
        } else {
            XtVaSetValues(g->form, XmNtopAttachment, XmATTACH_FORM,
                          XmNbottomAttachment, XmATTACH_FORM,
                          XmNleftAttachment, i ? XmATTACH_WIDGET : XmATTACH_FORM,
                          XmNleftWidget, i ? a->groups[i - 1]->sash : (Widget)0,
                          XmNrightAttachment, last ? XmATTACH_FORM : XmATTACH_NONE, (char*)0);
            if (!last)
                XtVaSetValues(g->form, XmNwidth, (Dimension)g->size, (char*)0);
        }
        XtVaSetValues(g->sash, XmNuserData, (XtPointer)sash_code(a->side, i + 1), (char*)0);
        if (last) {
            XtUnmanageChild(g->sash);
            continue;
        }
        if (vert)
            XtVaSetValues(g->sash, XmNleftAttachment, XmATTACH_FORM,
                          XmNrightAttachment, XmATTACH_FORM,
                          XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, g->form,
                          XmNbottomAttachment, XmATTACH_NONE, (char*)0);
        else
            XtVaSetValues(g->sash, XmNtopAttachment, XmATTACH_FORM,
                          XmNbottomAttachment, XmATTACH_FORM,
                          XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, g->form,
                          XmNrightAttachment, XmATTACH_NONE, (char*)0);
        XtManageChild(g->sash);
    }
}

void Docks::relayout()
{
    bool vis[SIDE_COUNT];
    int s;
    for (s = 0; s < SIDE_COUNT; s++)
        vis[s] = !all_hidden_ && !areas_[s].hidden && !areas_[s].groups.empty();

    Area& l = areas_[SIDE_LEFT];
    Area& r = areas_[SIDE_RIGHT];
    Area& b = areas_[SIDE_BOTTOM];
    /* XmForm keeps a child's present size when it can and squeezes the
     * one that came last when it cannot, so the documents' column and
     * center get the size that is left before anything new is managed. */
    Dimension bw = 0, bh = 0;
    XtVaGetValues(body_, XmNwidth, &bw, XmNheight, &bh, (char*)0);
    int cw = (int)bw - (vis[SIDE_LEFT] ? l.size + k_sash : 0) -
             (vis[SIDE_RIGHT] ? r.size + k_sash : 0);
    int ch = (int)bh - (vis[SIDE_BOTTOM] ? b.size + k_sash : 0);
    if (bw > 1 && cw > 1)
        XtVaSetValues(column_, XmNwidth, (Dimension)cw, (char*)0);
    if (bh > 1 && ch > 1) {
        XtVaSetValues(center_, XmNheight, (Dimension)ch, (char*)0);
        XtVaSetValues(l.form, XmNheight, bh, (char*)0);
        XtVaSetValues(r.form, XmNheight, bh, (char*)0);
    }
    if (bw > 1 && cw > 1)
        XtVaSetValues(b.form, XmNwidth, (Dimension)cw, (char*)0);
    if (vis[SIDE_LEFT]) {
        XtVaSetValues(l.form, XmNwidth, (Dimension)l.size, XmNtopAttachment, XmATTACH_FORM,
                      XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                      XmNrightAttachment, XmATTACH_NONE, (char*)0);
        XtVaSetValues(l.sash, XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment,
                      XmATTACH_FORM, XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, l.form,
                      XmNrightAttachment, XmATTACH_NONE, (char*)0);
    }
    /* The right and bottom areas hang from the far edge: an unattached
     * side would keep the widget's old position instead. */
    if (vis[SIDE_RIGHT]) {
        XtVaSetValues(r.form, XmNwidth, (Dimension)r.size, XmNtopAttachment, XmATTACH_FORM,
                      XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                      XmNleftAttachment, XmATTACH_OPPOSITE_FORM, XmNleftOffset, -r.size,
                      (char*)0);
        XtVaSetValues(r.sash, XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment,
                      XmATTACH_FORM, XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, r.form,
                      XmNleftAttachment, XmATTACH_OPPOSITE_WIDGET, XmNleftWidget, r.form,
                      XmNleftOffset, -k_sash, (char*)0);
    }
    XtVaSetValues(column_, XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM,
                  XmNleftAttachment, vis[SIDE_LEFT] ? XmATTACH_WIDGET : XmATTACH_FORM,
                  XmNleftWidget, vis[SIDE_LEFT] ? l.sash : (Widget)0,
                  XmNrightAttachment, vis[SIDE_RIGHT] ? XmATTACH_WIDGET : XmATTACH_FORM,
                  XmNrightWidget, vis[SIDE_RIGHT] ? r.sash : (Widget)0, (char*)0);
    if (vis[SIDE_BOTTOM]) {
        XtVaSetValues(b.form, XmNheight, (Dimension)b.size, XmNleftAttachment, XmATTACH_FORM,
                      XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM,
                      XmNtopAttachment, XmATTACH_OPPOSITE_FORM, XmNtopOffset, -b.size,
                      (char*)0);
        XtVaSetValues(b.sash, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment,
                      XmATTACH_FORM, XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget,
                      b.form, XmNtopAttachment, XmATTACH_OPPOSITE_WIDGET, XmNtopWidget, b.form,
                      XmNtopOffset, -k_sash, (char*)0);
    }
    XtVaSetValues(center_, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM,
                  XmNbottomAttachment, vis[SIDE_BOTTOM] ? XmATTACH_WIDGET : XmATTACH_FORM,
                  XmNbottomWidget, vis[SIDE_BOTTOM] ? b.sash : (Widget)0, (char*)0);

    for (s = 0; s < SIDE_COUNT; s++) {
        Area& a = areas_[s];
        if (vis[s]) {
            layout_area(&a);
            XtManageChild(a.form);
            XtManageChild(a.sash);
        } else {
            XtUnmanageChild(a.sash);
            XtUnmanageChild(a.form);
        }
    }

    /* Resize cursors on the sashes now on screen. */
    if (!XtIsRealized(body_))
        return;
    Display* dpy = XtDisplay(body_);
    Cursor h = XCreateFontCursor(dpy, XC_sb_h_double_arrow);
    Cursor v = XCreateFontCursor(dpy, XC_sb_v_double_arrow);
    for (s = 0; s < SIDE_COUNT; s++) {
        Area& a = areas_[s];
        if (XtIsRealized(a.sash))
            XDefineCursor(dpy, XtWindow(a.sash), s == SIDE_BOTTOM ? v : h);
        int i;
        for (i = 0; i < a.groups.size(); i++)
            if (XtIsRealized(a.groups[i]->sash))
                XDefineCursor(dpy, XtWindow(a.groups[i]->sash), vertical_side(s) ? v : h);
    }
}

/* ------------------------------------------------------------------ */
/* Sashes                                                              */

void Docks::sash_event(Widget w, XEvent* ev)
{
    XtPointer ud = 0;
    XtVaGetValues(w, XmNuserData, &ud, (char*)0);
    long code = (long)ud;
    int side = (int)(code / 1000), gi = (int)(code % 1000) - 1;
    if (side < 0 || side >= SIDE_COUNT)
        return;
    Area* a = &areas_[side];
    Group* g = gi >= 0 && gi < a->groups.size() ? a->groups[gi] : 0;
    if (gi >= 0 && !g)
        return;
    /* Which way it moves: an edge across the area, a group's sash along. */
    bool horiz = g ? !vertical_side(side) : side != SIDE_BOTTOM;
    int pos = horiz ? (ev->type == MotionNotify ? ev->xmotion.x_root : ev->xbutton.x_root)
                    : (ev->type == MotionNotify ? ev->xmotion.y_root : ev->xbutton.y_root);
    if (ev->type == ButtonPress && ev->xbutton.button == 1) {
        drag_start_ = pos;
        drag_size_ = g ? g->size : a->size;
        return;
    }
    if ((ev->type != MotionNotify && ev->type != ButtonRelease) || drag_start_ < 0)
        return;
    int delta = pos - drag_start_;
    int size, limit;
    Dimension pw = 0, ph = 0;
    if (g) {
        XtVaGetValues(a->form, XmNwidth, &pw, XmNheight, &ph, (char*)0);
        size = drag_size_ + delta;
        limit = (horiz ? (int)pw : (int)ph) - k_min;
    } else {
        Widget parent = side == SIDE_BOTTOM ? column_ : body_;
        XtVaGetValues(parent, XmNwidth, &pw, XmNheight, &ph, (char*)0);
        size = side == SIDE_LEFT ? drag_size_ + delta : drag_size_ - delta;
        limit = (horiz ? (int)pw : (int)ph) - 80;
    }
    if (size > limit)
        size = limit;
    if (size < k_min)
        size = k_min;
    if (g) {
        g->size = size;
        XtVaSetValues(g->form, horiz ? XmNwidth : XmNheight, (Dimension)size, (char*)0);
    } else {
        a->size = size;
        if (side == SIDE_RIGHT)
            XtVaSetValues(a->form, XmNwidth, (Dimension)size, XmNleftOffset, -size, (char*)0);
        else if (side == SIDE_BOTTOM)
            XtVaSetValues(a->form, XmNheight, (Dimension)size, XmNtopOffset, -size, (char*)0);
        else
            XtVaSetValues(a->form, XmNwidth, (Dimension)size, (char*)0);
    }
    if (ev->type == ButtonRelease) {
        drag_start_ = -1;
        changed();
    }
}

/* ------------------------------------------------------------------ */
/* Tabs and their menu                                                 */

void Docks::tab_chosen(Widget tab)
{
    XtPointer ud = 0;
    XtVaGetValues(tab, XmNuserData, &ud, (char*)0);
    Pane* p = (Pane*)ud;
    if (p && p->group)
        choose(p->group, p->group->panes.find(p));
}

void Docks::tab_menu(Widget tab, XEvent* ev)
{
    XtPointer ud = 0;
    XtVaGetValues(tab, XmNuserData, &ud, (char*)0);
    menu_pane_ = (Pane*)ud;
    if (!menu_pane_ || !menu_pane_->group)
        return;
    g_menu_docks = this;
    /* The items again: they name the groups there are now. */
    WidgetList kids = 0;
    Cardinal nkids = 0;
    XtVaGetValues(menu_, XmNchildren, &kids, XmNnumChildren, &nkids, (char*)0);
    ttr::Vector<Widget> old;
    Cardinal k;
    for (k = 0; k < nkids; k++)
        old.push_back(kids[k]);
    int i;
    for (i = 0; i < old.size(); i++)
        XtDestroyWidget(old[i]);
    menu_actions_.clear();

    int s;
    for (s = 0; s < SIDE_COUNT; s++) {
        Area& a = areas_[s];
        char name[32];
        sprintf(name, "move%s", k_side_names[s]);
        Widget sub = XmCreatePulldownMenu(menu_, name, 0, 0);
        char label[300];
        sprintf(label, "Move to %s", k_side_labels[s]);
        XmString l = XmStringCreateLocalized(label);
        XtVaCreateManagedWidget(name, xmCascadeButtonWidgetClass, menu_, XmNlabelString, l,
                                XmNsubMenuId, sub, (char*)0);
        XmStringFree(l);
        int g;
        for (g = 0; g < a.groups.size(); g++) {
            Group* gr = a.groups[g];
            if (gr == menu_pane_->group)
                continue;
            /* "With Terminal, Find Results" */
            ttr::Str names;
            int j;
            for (j = 0; j < gr->panes.size(); j++) {
                char part[160];
                sprintf(part, "%s%.100s", j ? ", " : "With ", gr->panes[j]->title());
                ttr::Str t(names.c_str());
                char buf[1200];
                snprintf(buf, sizeof(buf), "%s%s", t.c_str(), part);
                names = buf;
            }
            l = XmStringCreateLocalized((char*)names.c_str());
            Widget b = XtVaCreateManagedWidget("group", xmPushButtonWidgetClass, sub,
                                               XmNlabelString, l, (char*)0);
            XmStringFree(l);
            menu_actions_.push_back(s * 1000 + g + 1);
            XtAddCallback(b, XmNactivateCallback, menu_cb,
                          (XtPointer)(long)(menu_actions_.size() - 1));
        }
        l = XmStringCreateLocalized((char*)"In a New Group");
        Widget b = XtVaCreateManagedWidget("newGroup", xmPushButtonWidgetClass, sub,
                                           XmNlabelString, l, (char*)0);
        XmStringFree(l);
        menu_actions_.push_back(s * 1000);
        XtAddCallback(b, XmNactivateCallback, menu_cb, (XtPointer)(long)(menu_actions_.size() - 1));
    }
    XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, menu_, (char*)0);
    XmString l = XmStringCreateLocalized((char*)"Close");
    Widget b = XtVaCreateManagedWidget("close", xmPushButtonWidgetClass, menu_,
                                       XmNlabelString, l, (char*)0);
    XmStringFree(l);
    menu_actions_.push_back(-1);
    XtAddCallback(b, XmNactivateCallback, menu_cb, (XtPointer)(long)(menu_actions_.size() - 1));

    XmMenuPosition(menu_, &ev->xbutton);
    XtManageChild(menu_);
}

void Docks::menu_chosen(int item)
{
    if (!menu_pane_ || item < 0 || item >= menu_actions_.size())
        return;
    int action = menu_actions_[item];
    Pane* p = menu_pane_;
    menu_pane_ = 0;
    if (action < 0) {
        close(p);
        return;
    }
    move(p, (Side)(action / 1000), action % 1000 - 1);
}

/* ------------------------------------------------------------------ */
/* The layout as text                                                  */

ttr::Str Docks::layout() const
{
    char out[4096];
    size_t o = 0;
    out[0] = 0;
    int s;
    for (s = 0; s < SIDE_COUNT; s++) {
        const Area& a = areas_[s];
        o += (size_t)snprintf(out + o, sizeof(out) - o, "%s%s:%d=", s ? ";" : "",
                              k_side_names[s], a.size);
        int g;
        for (g = 0; g < a.groups.size() && o < sizeof(out); g++) {
            const Group* gr = a.groups[g];
            int j;
            if (g)
                o += (size_t)snprintf(out + o, sizeof(out) - o, "|");
            for (j = 0; j < gr->panes.size() && o < sizeof(out); j++)
                o += (size_t)snprintf(out + o, sizeof(out) - o, "%s%s", j ? "," : "",
                                      gr->panes[j]->id());
            if (g < a.groups.size() - 1 && o < sizeof(out))
                o += (size_t)snprintf(out + o, sizeof(out) - o, "@%d", gr->size);
        }
        if (o >= sizeof(out))
            break;
    }
    return ttr::Str(out);
}

void Docks::apply(const char* text)
{
    in_apply_ = true;
    /* Everything closed first. */
    int i, s;
    for (i = 0; i < panes_.size(); i++)
        if (panes_[i]->group)
            close(panes_[i]);
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", text ? text : "");
    char* rest = buf;
    char* part;
    while ((part = next_field(&rest, ';')) != 0) {
        char* colon = strchr(part, ':');
        char* eq = strchr(part, '=');
        if (!eq)
            continue;
        *eq = 0;
        if (colon && colon < eq)
            *colon = 0;
        int side = -1;
        for (s = 0; s < SIDE_COUNT; s++)
            if (!strcmp(part, k_side_names[s]))
                side = s;
        if (side < 0)
            continue;
        Area* a = &areas_[side];
        if (colon && colon < eq && atoi(colon + 1) >= k_min)
            a->size = atoi(colon + 1);
        /* Groups: "a,b@300|c". */
        char* grest = eq + 1;
        char* gtext;
        while ((gtext = next_field(&grest, '|')) != 0) {
            int size = 0;
            char* at = strchr(gtext, '@');
            if (at) {
                *at = 0;
                size = atoi(at + 1);
            }
            Group* g = 0;
            char* prest = gtext;
            char* id;
            while ((id = next_field(&prest, ',')) != 0) {
                Pane* p = find(id);
                if (!p || p->group)
                    continue;
                if (!g)
                    g = new_group(a, -1);
                add_to(p, g);
            }
            if (g && size >= k_min)
                g->size = size;
            if (g)
                choose(g, 0);
        }
    }
    relayout();
    in_apply_ = false;
}

} /* namespace ttrcode */
