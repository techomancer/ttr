/* tl/buffer.h - a growable byte buffer (tl uses no STL). */
#ifndef TL_BUFFER_H
#define TL_BUFFER_H

#include "port.h"

#include <stdlib.h>
#include <string.h>

namespace tl {

class Buffer {
public:
    Buffer() : data_(0), len_(0), cap_(0) {}
    ~Buffer() { free(data_); }

    size_t size() const { return len_; }
    const uint8_t* data() const { return data_ ? data_ : (const uint8_t*)""; }
    /* Always NUL-terminated. */
    const char* c_str() const { return data_ ? (const char*)data_ : ""; }
    void clear() { len_ = 0; if (data_) data_[0] = 0; }

    bool reserve(size_t n)
    {
        if (n >= ((size_t)-1) / 4)
            return false;
        if (n + 1 <= cap_)
            return true;
        size_t ncap = cap_ ? cap_ : 64;
        while (ncap < n + 1)
            ncap *= 2;
        uint8_t* p = (uint8_t*)realloc(data_, ncap);
        if (!p)
            return false;
        data_ = p;
        cap_ = ncap;
        return true;
    }
    bool append(const void* p, size_t n)
    {
        if (!reserve(len_ + n))
            return false;
        memcpy(data_ + len_, p, n);
        len_ += n;
        data_[len_] = 0;
        return true;
    }
    bool append(const char* s) { return append(s, strlen(s)); }
    bool append_byte(uint8_t c) { return append(&c, 1); }
    /* Append a code point as UTF-8. */
    bool append_utf8(uint32_t cp)
    {
        uint8_t b[4];
        int n;
        if (cp < 0x80) {
            b[0] = (uint8_t)cp;
            n = 1;
        } else if (cp < 0x800) {
            b[0] = (uint8_t)(0xc0 | (cp >> 6));
            b[1] = (uint8_t)(0x80 | (cp & 0x3f));
            n = 2;
        } else if (cp < 0x10000) {
            b[0] = (uint8_t)(0xe0 | (cp >> 12));
            b[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
            b[2] = (uint8_t)(0x80 | (cp & 0x3f));
            n = 3;
        } else {
            b[0] = (uint8_t)(0xf0 | (cp >> 18));
            b[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3f));
            b[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
            b[3] = (uint8_t)(0x80 | (cp & 0x3f));
            n = 4;
        }
        return append(b, n);
    }
    /* Append a decimal number. */
    bool append_int(long v)
    {
        char tmp[24];
        int i = sizeof(tmp);
        bool neg = v < 0;
        unsigned long u = neg ? (unsigned long)(-(v + 1)) + 1 : (unsigned long)v;
        tmp[--i] = 0;
        do {
            tmp[--i] = (char)('0' + u % 10);
            u /= 10;
        } while (u);
        if (neg)
            tmp[--i] = '-';
        return append(tmp + i);
    }

private:
    uint8_t* data_;
    size_t len_;
    size_t cap_;

    Buffer(const Buffer&);
    Buffer& operator=(const Buffer&);
};

} /* namespace tl */

#endif
