/*
 * ttrv's folder tree (ttrv/DESIGN.md section 2.1): an XmList of indented
 * names, as ttrcode's workspace tree. It opens down to the current
 * folder; Right or Enter opens a folder, Left closes it or goes to the
 * parent; moving the selection goes to that folder after a moment (so
 * stepping through the tree does not list every folder passed).
 * Archives are folders, with mc's paths ("x.zip/uzip://").
 *
 * The tree's data outlives its widget: the docks build a pane's widgets
 * again when it moves (build()).
 */
#ifndef TTRV_FOLDERS_H
#define TTRV_FOLDERS_H

#include "loader.h"

#include <Xm/Xm.h>

namespace ttrv {

class FolderTreeHost {
public:
    virtual ~FolderTreeHost() {}
    /* Needs this folder's entries: answer with FolderTree::listed(). */
    virtual void tree_list(const char* path) = 0;
    /* The user chose a folder. */
    virtual void tree_chosen(const char* path) = 0;
};

class FolderTree {
public:
    explicit FolderTree(FolderTreeHost* host);
    ~FolderTree();

    /* The list in parent; returns the widget for the docks to place. */
    Widget build(Widget parent);
    void drop() { list_ = 0; }
    Widget list() const { return list_; }

    /* Open down to path and select it (no tree_chosen). */
    void show_path(const char* path);
    /* A JOB_LIST result for one of its folders (not taken). */
    void listed(const Result* r);
    void set_show_hidden(bool on) { hidden_ = on; }

    /* Callbacks (public for the C trampolines). */
    void selected(int pos);
    void activated(int pos);
    void key(XKeyEvent* ev);
    void timer();

private:
    FolderTree(const FolderTree&);
    FolderTree& operator=(const FolderTree&);

    struct Node {
        ttr::Str path, name;
        int depth;
        bool open, listed, archive, asked;
        Node* parent;
        ttr::Vector<Node*> kids;
        Node() : depth(0), open(false), listed(false), archive(false), asked(false), parent(0) {}
        ~Node()
        {
            for (int i = 0; i < kids.size(); i++)
                delete kids[i];
        }
    };

    Node* find(Node* n, const char* path) const;
    void flatten(Node* n, ttr::Vector<Node*>* out) const;
    void refill();
    void open(Node* n);
    void select_node(Node* n);

    FolderTreeHost* host_;
    Widget list_;
    Node* root_;
    ttr::Vector<Node*> rows_;       /* the visible nodes, in list order */
    Node* current_;
    Node* pending_;                 /* chosen by moving, after the timer */
    XtIntervalId timer_;
    bool hidden_, filling_;
};

/* An entry's path as a folder: the archive's root for an archive. */
ttr::Str child_path(const char* dir, const char* name, bool archive);

} /* namespace ttrv */

#endif
