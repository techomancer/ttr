#include "core/str.h"

#include <string.h>

namespace ttr {

Str::Str() : s_(0) {}

Str::Str(const char* s) : s_(0) { assign(s); }

Str::Str(const Str& o) : s_(0) { assign(o.s_); }

Str::~Str() { delete [] s_; }

Str& Str::operator=(const Str& o)
{
    if (this != &o)
        assign(o.s_);
    return *this;
}

Str& Str::operator=(const char* s)
{
    assign(s);
    return *this;
}

int Str::length() const
{
    return s_ ? (int)strlen(s_) : 0;
}

void Str::assign(const char* s)
{
    char* n = 0;
    if (s) {
        n = new char[strlen(s) + 1];
        strcpy(n, s);
    }
    delete [] s_;
    s_ = n;
}

}
