#include "valuetree.h"

#include <string.h>

namespace ttrcode {

namespace {

/* Elements and members shown under one value, at most. */
const int k_max_children = 1000;

} /* namespace */

ValueTree::ValueTree() : s_(0), first_(0), stop_(-1) {}

ValueTree::~ValueTree()
{
    free_nodes(&roots_);
}

void ValueTree::free_nodes(ttr::Vector<Node*>* v)
{
    int i;
    for (i = 0; i < v->size(); i++) {
        free_nodes(&(*v)[i]->children);
        delete (*v)[i];
    }
    v->clear();
}

bool ValueTree::was_open(const ttr::Str& key) const
{
    int i;
    for (i = 0; i < open_.size(); i++)
        if (!strcmp(open_[i].c_str(), key.c_str()))
            return true;
    return false;
}

void ValueTree::set_open(const ttr::Str& key, bool on)
{
    int i;
    for (i = 0; i < open_.size(); i++)
        if (!strcmp(open_[i].c_str(), key.c_str())) {
            if (!on)
                open_.remove_at(i);
            return;
        }
    if (on)
        open_.push_back(key);
}

ValueTree::Node* ValueTree::make(const td::Value& v, int depth, int root)
{
    Node* n = new Node;
    n->v = v;
    td::Text key;
    key.add(context_.c_str());
    key.add("|");
    key.add(v.expr.c_str());
    n->key = key.c_str();
    n->text = s_->format(v);
    n->type = v.ok() ? td::type_name(v.type) : ttr::Str("");
    n->kids = s_->has_children(v);
    n->open = n->kids && was_open(n->key);
    n->read = false;
    n->depth = depth;
    n->root = root;
    n->changed = false;
    int i;
    for (i = 0; i < last_key_.size(); i++)
        if (!strcmp(last_key_[i].c_str(), n->key.c_str())) {
            n->changed = strcmp(last_text_[i].c_str(), n->text.c_str()) != 0;
            break;
        }
    cur_key_.push_back(n->key);
    cur_text_.push_back(n->text);
    if (n->open)
        read_children(n);
    return n;
}

void ValueTree::read_children(Node* n)
{
    n->read = true;
    ttr::Vector<td::Value> kids;
    s_->children(n->v, &kids, k_max_children);
    int i;
    for (i = 0; i < kids.size(); i++)
        n->children.push_back(make(kids[i], n->depth + 1, n->root));
}

void ValueTree::set(td::Session* s, const ttr::Vector<td::Value>& roots, int stop,
                    const char* context)
{
    s_ = s;
    context_ = context ? context : "";
    if (stop != stop_) {
        /* A new stop: what was shown is what the next values compare with. */
        last_key_.clear();
        last_text_.clear();
        int i;
        for (i = 0; i < cur_key_.size(); i++) {
            last_key_.push_back(cur_key_[i]);
            last_text_.push_back(cur_text_[i]);
        }
        stop_ = stop;
    }
    cur_key_.clear();
    cur_text_.clear();
    free_nodes(&roots_);
    flat_.clear();
    int i;
    for (i = 0; i < roots.size(); i++)
        roots_.push_back(make(roots[i], 0, i));
}

void ValueTree::clear()
{
    free_nodes(&roots_);
    flat_.clear();
    last_key_.clear();
    last_text_.clear();
    cur_key_.clear();
    cur_text_.clear();
    stop_ = -1;
}

void ValueTree::flatten(Node* n)
{
    flat_.push_back(n);
    if (!n->open)
        return;
    int i;
    for (i = 0; i < n->children.size(); i++)
        flatten(n->children[i]);
}

void ValueTree::fill(CellList* l, const ListColors& c)
{
    flat_.clear();
    int i;
    for (i = 0; i < roots_.size(); i++)
        flatten(roots_[i]);
    first_ = l->row_count();
    for (i = 0; i < flat_.size(); i++) {
        Node* n = flat_[i];
        int r = l->add_row(n->depth, !n->kids ? EXPAND_NONE : n->open ? EXPAND_OPEN : EXPAND_CLOSED);
        l->set_cell(r, 0, n->v.name.c_str());
        l->set_cell(r, 1, n->text.c_str(), n->changed ? c.changed : n->v.ok() ? 0 : c.dim);
        l->set_cell(r, 2, n->type.c_str(), c.dim);
    }
}

void ValueTree::toggle(int row)
{
    if (!has_row(row))
        return;
    Node* n = flat_[row - first_];
    if (!n->kids)
        return;
    n->open = !n->open;
    set_open(n->key, n->open);
    if (n->open && !n->read)
        read_children(n);
}

int ValueTree::root_of(int row) const
{
    return has_row(row) ? flat_[row - first_]->root : -1;
}

const td::Value* ValueTree::value(int row) const
{
    return has_row(row) ? &flat_[row - first_]->v : 0;
}

} /* namespace ttrcode */
