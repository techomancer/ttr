/*
 * Minimal owned C string.
 */
#ifndef TTR_CORE_STR_H
#define TTR_CORE_STR_H

namespace ttr {

class Str {
public:
    Str();
    Str(const char* s);
    Str(const Str& o);
    ~Str();
    Str& operator=(const Str& o);
    Str& operator=(const char* s);

    const char* c_str() const { return s_ ? s_ : ""; }
    int length() const;
    bool empty() const { return !s_ || !*s_; }

private:
    void assign(const char* s);
    char* s_;
};

}

#endif
