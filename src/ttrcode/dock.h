/*
 * ttrcode's dock areas (ttrcode/DESIGN.md section 4.1): tool panes in
 * groups of tabs, the groups tiled in three areas around the documents
 * (left, bottom, right), sashes between them all, a right-click menu on
 * each tab to move or close its pane, and the layout as a line of text.
 *
 * Xt cannot give a widget a new parent, so the docks never move a
 * pane's widgets: they ask the PaneBuilder to make them in a group, and
 * to let them go when the pane is closed.
 */
#ifndef TTRCODE_DOCK_H
#define TTRCODE_DOCK_H

#include "core/str.h"
#include "core/vector.h"

#include <Xm/Xm.h>

namespace ttrcode {

enum Side { SIDE_LEFT, SIDE_BOTTOM, SIDE_RIGHT, SIDE_COUNT };

class Pane;

class PaneBuilder {
public:
    virtual ~PaneBuilder() {}
    /* The pane's widgets in parent (an XmForm), made or made again there:
     * one managed child, which the docks attach. */
    virtual Widget build_pane(Pane* p, Widget parent) = 0;
    /* The pane was closed: let its widgets go (or keep them hidden). */
    virtual void drop_pane(Pane* p) = 0;
    /* Its tab was chosen. */
    virtual void pane_shown(Pane* p) { (void)p; }
    /* The layout changed (to save it). */
    virtual void layout_changed() {}
};

/* A tool window: the workspace, the terminal, Find Results, the
 * debugger's. */
class Pane {
public:
    Pane(const char* id, const char* title, Side home)
        : widget(0), tab(0), group(0), last_side(home), last_group(-1), id_(id), title_(title),
          home_(home)
    {
    }
    const char* id() const { return id_.c_str(); }
    const char* title() const { return title_.c_str(); }
    Side home() const { return home_; }

    /* The docks' bookkeeping. */
    Widget widget;          /* while in a group */
    Widget tab;
    struct Group* group;    /* 0: closed */
    int last_side, last_group;

private:
    ttr::Str id_, title_;
    Side home_;             /* where it goes the first time */
};

struct Area;

struct Group {
    Area* area;
    Widget form, tabs, sash;    /* sash: after it, toward the next group */
    ttr::Vector<Pane*> panes;
    int current;
    int size;                   /* along the area; the last group takes the rest */
};

struct Area {
    Side side;
    Widget form, sash;          /* sash: on its inner edge */
    ttr::Vector<Group*> groups;
    int size;                   /* across: width, or height for the bottom */
    bool hidden;                /* Alt+2 */
};

class Docks {
public:
    /* body: an XmForm to fill. center(): where the documents go. */
    Docks(Widget body, PaneBuilder* builder, Pixel tab_fill);
    ~Docks();

    Widget center() const { return center_; }
    /* An unmanaged form for the widgets of closed panes that keep them
     * (the terminal). */
    Widget parking() const { return parking_; }
    /* After the window is realized: the sashes' cursors. */
    void realized();
    void add(Pane* p);
    Pane* find(const char* id) const;
    int pane_count() const { return panes_.size(); }
    Pane* pane(int i) const { return panes_[i]; }

    /* "left:220=workspace;bottom:220=terminal,results|variables@300;right:240="
     * (DESIGN.md 4.1). Panes it does not name are closed. */
    void apply(const char* layout);
    ttr::Str layout() const;

    /* Open a pane where it was last (or at home) and bring its tab to
     * the front; its area is shown. */
    void show(Pane* p);
    void close(Pane* p);
    /* Its widgets made again where it is (the terminal after its shell
     * was restarted). */
    void rebuild(Pane* p);
    bool open(const Pane* p) const { return p->group != 0; }
    /* side, and a group there (-1: a new one at the end). */
    void move(Pane* p, Side side, int group);

    /* The selected tabs' colour, after a theme change. */
    void set_tab_fill(Pixel p);
    void set_hidden(Side s, bool on);
    bool hidden(Side s) const { return areas_[s].hidden; }
    bool empty(Side s) const { return areas_[s].groups.empty(); }
    /* Full screen: every area away, without forgetting. */
    void set_all_hidden(bool on);

    /* Callbacks (public for the C trampolines). */
    void sash_event(Widget w, XEvent* ev);
    void tab_chosen(Widget tab);
    void tab_menu(Widget tab, XEvent* ev);
    void menu_chosen(int action);

private:
    Group* new_group(Area* a, int at);
    void drop_group(Group* g);
    void add_to(Pane* p, Group* g);
    void remove_from(Pane* p);
    void choose(Group* g, int i);
    void relayout();
    void layout_area(Area* a);
    void changed();
    Widget make_sash(Widget parent, bool vertical);

    Widget body_, column_, center_, parking_, menu_;
    PaneBuilder* builder_;
    Pixel tab_fill_;
    Area areas_[SIDE_COUNT];
    ttr::Vector<Pane*> panes_;
    bool all_hidden_;
    bool in_apply_;
    /* A sash being dragged. */
    int drag_start_, drag_size_;
    /* The tab the menu is for, and its items' meanings. */
    Pane* menu_pane_;
    ttr::Vector<int> menu_actions_;
};

} /* namespace ttrcode */

#endif
