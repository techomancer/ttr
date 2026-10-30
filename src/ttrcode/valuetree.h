/*
 * The values Watch, Variables and QuickWatch show (td/DESIGN.md section
 * 8.2), as a tree in a CellList: a row per value, its members, elements
 * or what it points to under it. What is open, and what each value was
 * at the last stop, are kept by expression, so after a step the same
 * rows are open and the values that changed are red.
 */
#ifndef TTRCODE_VALUETREE_H
#define TTRCODE_VALUETREE_H

#include "celllist.h"

#include "../../td/session.h"

namespace ttrcode {

class ValueTree {
public:
    ValueTree();
    ~ValueTree();

    /* The top rows. stop: which stop of the program this is (the values
     * of the one before are what "changed" compares with); context: what
     * the names mean (the frame's function), so other frames' values of
     * the same names are not compared. */
    void set(td::Session* s, const ttr::Vector<td::Value>& roots, int stop, const char* context);
    /* Debugging ended: the values go, what is open stays. */
    void clear();
    /* Rows into l (from row first on; earlier rows are the caller's). */
    void fill(CellList* l, const ListColors& c);
    /* Open or close the value at a row (of the last fill). */
    void toggle(int row);
    bool has_row(int row) const { return row >= first_ && row - first_ < flat_.size(); }
    /* Which top value a row is under, -1 if none. */
    int root_of(int row) const;
    /* A row's value, 0 if none. */
    const td::Value* value(int row) const;
    int count() const { return flat_.size(); }

private:
    struct Node {
        td::Value v;
        ttr::Str key;           /* context and expression */
        ttr::Str text, type;
        bool open, read, kids, changed;
        int depth, root;
        ttr::Vector<Node*> children;
    };

    Node* make(const td::Value& v, int depth, int root);
    void free_nodes(ttr::Vector<Node*>* v);
    void remember(const ttr::Vector<Node*>& v);
    void flatten(Node* n);
    void read_children(Node* n);
    bool was_open(const ttr::Str& key) const;
    void set_open(const ttr::Str& key, bool on);

    td::Session* s_;
    ttr::Vector<Node*> roots_;
    ttr::Vector<Node*> flat_;
    int first_;                 /* the first row fill() wrote */
    ttr::Vector<ttr::Str> open_;
    /* At the stop before this one: keys and texts. */
    ttr::Vector<ttr::Str> last_key_, last_text_;
    ttr::Vector<ttr::Str> cur_key_, cur_text_;
    int stop_;
    ttr::Str context_;

    ValueTree(const ValueTree&);
    ValueTree& operator=(const ValueTree&);
};

} /* namespace ttrcode */

#endif
