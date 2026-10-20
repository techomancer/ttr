#include "style.h"

#include <stdlib.h>
#include <string.h>

namespace tl {

StyleTable::StyleTable()
    : entries_(0), heads_(0), marks_(0), cap_(65535), used_(1), free_(0), scan_(1)
{
    entries_ = (Entry*)calloc((size_t)cap_, sizeof(Entry));
    heads_ = (uint16_t*)calloc(HASH_SIZE, sizeof(uint16_t));
    marks_ = (uint8_t*)calloc((size_t)cap_, 1);
    entries_[0].style.clear();
    entries_[0].used = 1;
}

StyleTable::~StyleTable()
{
    free(entries_);
    free(heads_);
    free(marks_);
}

unsigned StyleTable::hash(const Style& s) const
{
    uint32_t h = s.fg * 2654435761u;
    h ^= s.bg * 2246822519u;
    h ^= s.ul * 3266489917u;
    h ^= (uint32_t)s.attrs * 668265263u;
    h ^= h >> 15;
    return h & (HASH_SIZE - 1);
}

uint16_t StyleTable::intern(const Style& s)
{
    if (s.is_default())
        return 0;
    unsigned h = hash(s);
    for (uint16_t id = heads_[h]; id; id = entries_[id].next)
        if (entries_[id].style == s)
            return id;
    uint16_t id;
    if (free_) {
        id = free_;
        free_ = entries_[id].next;
    } else if (scan_ < cap_) {
        id = (uint16_t)scan_++;
    } else {
        return 0;
    }
    Entry& e = entries_[id];
    e.style = s;
    e.used = 1;
    e.next = heads_[h];
    heads_[h] = id;
    used_++;
    return id;
}

void StyleTable::mark_begin()
{
    memset(marks_, 0, (size_t)cap_);
}

void StyleTable::unlink(uint16_t id)
{
    unsigned h = hash(entries_[id].style);
    uint16_t* p = &heads_[h];
    while (*p && *p != id)
        p = &entries_[*p].next;
    if (*p == id)
        *p = entries_[id].next;
}

void StyleTable::sweep()
{
    for (int id = 1; id < scan_; id++) {
        Entry& e = entries_[id];
        if (!e.used || marks_[id])
            continue;
        unlink((uint16_t)id);
        e.used = 0;
        e.next = free_;
        free_ = (uint16_t)id;
        used_--;
    }
}

} /* namespace tl */
