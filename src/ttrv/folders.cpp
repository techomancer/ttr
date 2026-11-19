/* ttrv's folder tree (folders.h). */
#include "folders.h"

#include "../../tf/vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <Xm/List.h>

namespace ttrv {

namespace {

void browse_cb(Widget, XtPointer closure, XtPointer call)
{
    XmListCallbackStruct* cb = (XmListCallbackStruct*)call;
    FolderTree* t = (FolderTree*)closure;
    if (cb->event && (cb->event->type == ButtonPress || cb->event->type == ButtonRelease))
        t->activated(-cb->item_position);   /* a click: at once */
    else
        t->selected(cb->item_position);
}

void default_cb(Widget, XtPointer closure, XtPointer call)
{
    ((FolderTree*)closure)->activated(((XmListCallbackStruct*)call)->item_position);
}

void key_cb(Widget, XtPointer closure, XEvent* ev, Boolean*)
{
    if (ev->type == KeyPress)
        ((FolderTree*)closure)->key(&ev->xkey);
}

void timer_cb(XtPointer closure, XtIntervalId*)
{
    ((FolderTree*)closure)->timer();
}

int name_cmp(const void* a, const void* b)
{
    return strcasecmp((*(const ttr::Str* const*)a)->c_str(), (*(const ttr::Str* const*)b)->c_str());
}

} /* namespace */

ttr::Str child_path(const char* dir, const char* name, bool archive)
{
    size_t dl = strlen(dir);
    bool slash = dl > 0 && dir[dl - 1] == '/';
    char p[4300];
    if (archive)
        snprintf(p, sizeof(p), "%s%s%s/%s://", dir, slash ? "" : "/", name,
                 tf::archive_scheme(name));
    else
        snprintf(p, sizeof(p), "%s%s%s", dir, slash ? "" : "/", name);
    return ttr::Str(p);
}

FolderTree::FolderTree(FolderTreeHost* host)
    : host_(host), list_(0), root_(new Node), current_(0), pending_(0), timer_(0),
      hidden_(false), filling_(false)
{
    root_->path = "/";
    root_->name = "/";
    root_->open = true;
}

FolderTree::~FolderTree()
{
    if (timer_)
        XtRemoveTimeOut(timer_);
    delete root_;
}

Widget FolderTree::build(Widget parent)
{
    Arg a[4];
    int n = 0;
    XtSetArg(a[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
    XtSetArg(a[n], XmNlistSizePolicy, XmCONSTANT); n++;
    list_ = XmCreateScrolledList(parent, (char*)"folders", a, n);
    XtAddCallback(list_, XmNbrowseSelectionCallback, browse_cb, this);
    XtAddCallback(list_, XmNdefaultActionCallback, default_cb, this);
    XtAddEventHandler(list_, KeyPressMask, False, key_cb, this);
    XtManageChild(list_);
    refill();
    return XtParent(list_);
}

FolderTree::Node* FolderTree::find(Node* n, const char* path) const
{
    if (strcmp(n->path.c_str(), path) == 0)
        return n;
    for (int i = 0; i < n->kids.size(); i++) {
        Node* f = find(n->kids[i], path);
        if (f)
            return f;
    }
    return 0;
}

void FolderTree::flatten(Node* n, ttr::Vector<Node*>* out) const
{
    out->push_back(n);
    if (!n->open)
        return;
    for (int i = 0; i < n->kids.size(); i++)
        flatten(n->kids[i], out);
}

void FolderTree::refill()
{
    rows_.clear();
    flatten(root_, &rows_);
    if (!list_)
        return;
    filling_ = true;
    XmString* items = (XmString*)XtMalloc(sizeof(XmString) * (size_t)(rows_.size() + 1));
    int sel = 0;
    for (int i = 0; i < rows_.size(); i++) {
        Node* nd = rows_[i];
        char t[600];
        const char* mark = nd->open ? "- " : (!nd->listed || nd->kids.size()) ? "+ " : "  ";
        snprintf(t, sizeof(t), "%*s%s%s%s", nd->depth * 2, "", mark, nd->name.c_str(),
                 nd->archive ? " [archive]" : "");
        items[i] = XmStringCreateLocalized(t);
        if (nd == current_)
            sel = i + 1;
    }
    XmListDeleteAllItems(list_);
    XmListAddItems(list_, items, rows_.size(), 0);
    for (int i = 0; i < rows_.size(); i++)
        XmStringFree(items[i]);
    XtFree((char*)items);
    if (sel) {
        XmListSelectPos(list_, sel, False);
        int top = 0, visible = 0;
        XtVaGetValues(list_, XmNtopItemPosition, &top, XmNvisibleItemCount, &visible, (char*)0);
        if (sel < top || sel >= top + visible)
            XmListSetPos(list_, sel - visible / 2 > 1 ? sel - visible / 2 : 1);
    }
    filling_ = false;
}

/* Open down to path: each folder on the way, made if it is not known
 * yet (its parent's listing fills in the rest). */
void FolderTree::show_path(const char* path)
{
    Node* n = root_;
    const char* s = path;
    size_t len = strlen(s);
    size_t i = 1;
    while (i < len) {
        const char* slash = strchr(s + i, '/');
        size_t j = slash ? (size_t)(slash - s) : len;
        ttr::Str name;
        {
            char b[1024];
            size_t k = j - i < sizeof(b) - 1 ? j - i : sizeof(b) - 1;
            memcpy(b, s + i, k);
            b[k] = 0;
            name = b;
        }
        /* "name/scheme://": an archive's root. */
        bool archive = false;
        size_t next = j + 1;
        if (j < len) {
            const char* sep = strstr(s + j + 1, "://");
            const char* sl = strchr(s + j + 1, '/');
            if (sep && sl == sep + 1) {
                archive = true;
                next = (size_t)(sep - s) + 3;
            }
        }
        char p[4300];
        size_t plen = archive ? next : j;
        if (plen >= sizeof(p))
            break;
        memcpy(p, s, plen);
        p[plen] = 0;
        if (name.empty()) {
            i = next;
            continue;
        }
        Node* kid = 0;
        for (int k = 0; k < n->kids.size(); k++)
            if (strcmp(n->kids[k]->path.c_str(), p) == 0)
                kid = n->kids[k];
        if (!kid) {
            kid = new Node;
            kid->path = p;
            kid->name = name;
            kid->archive = archive;
            kid->depth = n->depth + 1;
            kid->parent = n;
            n->kids.push_back(kid);
        }
        n->open = true;
        open(n);
        n = kid;
        i = next;
    }
    current_ = n;
    open(n);
    refill();
}

/* Open a node, asking for its entries the first time. */
void FolderTree::open(Node* n)
{
    n->open = true;
    if (!n->listed && !n->asked && host_) {
        n->asked = true;
        host_->tree_list(n->path.c_str());
    }
}

void FolderTree::listed(const Result* r)
{
    Node* n = find(root_, r->path.c_str());
    if (!n)
        return;
    n->listed = true;
    n->asked = false;
    if (r->err)
        return;
    /* The folders (and archives) among the entries, by name. */
    ttr::Vector<ttr::Str*> names;
    ttr::Vector<bool> arch;
    for (int i = 0; i < r->entries.size(); i++) {
        const Entry* e = r->entries[i];
        if (!hidden_ && e->name.c_str()[0] == '.')
            continue;
        if (e->st.type != tf::FT_DIR && !e->archive)
            continue;
        names.push_back(new ttr::Str(e->name));
    }
    int cnt = names.size();
    ttr::Str** sorted = (ttr::Str**)malloc(sizeof(ttr::Str*) * (size_t)(cnt + 1));
    for (int i = 0; i < cnt; i++)
        sorted[i] = names[i];
    if (cnt > 1)
        qsort(sorted, (size_t)cnt, sizeof(ttr::Str*), name_cmp);
    ttr::Vector<Node*> kids;
    for (int i = 0; i < cnt; i++) {
        bool archive = false;
        for (int k = 0; k < r->entries.size(); k++)
            if (strcmp(r->entries[k]->name.c_str(), sorted[i]->c_str()) == 0)
                archive = r->entries[k]->archive;
        ttr::Str p = child_path(n->path.c_str(), sorted[i]->c_str(), archive);
        Node* kid = 0;
        for (int k = 0; k < n->kids.size(); k++)
            if (n->kids[k] && strcmp(n->kids[k]->path.c_str(), p.c_str()) == 0) {
                kid = n->kids[k];
                n->kids[k] = 0;
            }
        if (!kid) {
            kid = new Node;
            kid->path = p;
            kid->name = *sorted[i];
            kid->archive = archive;
            kid->depth = n->depth + 1;
            kid->parent = n;
        }
        kids.push_back(kid);
    }
    /* Nodes the listing no longer has go, unless the current folder is
     * in them (a hidden one typed in the location bar). */
    for (int k = 0; k < n->kids.size(); k++) {
        Node* old = n->kids[k];
        if (!old)
            continue;
        bool keep = false;
        for (Node* c = current_; c && !keep; c = c->parent)
            keep = c == old;
        if (keep)
            kids.push_back(old);
        else
            delete old;
    }
    n->kids.clear();
    for (int i = 0; i < kids.size(); i++)
        n->kids.push_back(kids[i]);
    for (int i = 0; i < cnt; i++)
        delete sorted[i];
    free(sorted);
    refill();
}

void FolderTree::select_node(Node* n)
{
    current_ = n;
    if (host_)
        host_->tree_chosen(n->path.c_str());
}

void FolderTree::selected(int pos)
{
    if (filling_ || pos < 1 || pos > rows_.size())
        return;
    pending_ = rows_[pos - 1];
    if (timer_)
        XtRemoveTimeOut(timer_);
    timer_ = XtAppAddTimeOut(XtWidgetToApplicationContext(list_), 350, timer_cb, this);
}

void FolderTree::timer()
{
    timer_ = 0;
    if (pending_)
        select_node(pending_);
    pending_ = 0;
}

void FolderTree::activated(int pos)
{
    bool click = pos < 0;
    if (click)
        pos = -pos;
    if (filling_ || pos < 1 || pos > rows_.size())
        return;
    Node* n = rows_[pos - 1];
    if (timer_) {
        XtRemoveTimeOut(timer_);
        timer_ = 0;
    }
    pending_ = 0;
    if (!click) {
        /* Enter, or a double click: open or close it. */
        if (n->open && n != current_)
            n->open = false;
        else
            open(n);
        refill();
    }
    if (n != current_)
        select_node(n);
}

void FolderTree::key(XKeyEvent* ev)
{
    KeySym ks = XLookupKeysym(ev, 0);
    int* pos = 0;
    int npos = 0;
    XtVaGetValues(list_, XmNselectedPositions, &pos, XmNselectedPositionCount, &npos, (char*)0);
    if (!pos || npos < 1)
        return;
    int p = pos[0];
    if (p < 1 || p > rows_.size())
        return;
    Node* n = rows_[p - 1];
    if (ks == XK_Right || ks == XK_KP_Add || ks == XK_plus) {
        if (!n->open) {
            open(n);
            refill();
        }
    } else if (ks == XK_Left || ks == XK_KP_Subtract || ks == XK_minus) {
        if (n->open && n->kids.size() && n != root_) {
            n->open = false;
            refill();
        } else if (n->parent) {
            select_node(n->parent);
            refill();
        }
    }
}

} /* namespace ttrv */
