#include "core/text.h"

namespace ttr {

Str utf8_to_latin1(const char* s, int len)
{
    char* out = new char[len + 1];
    int i = 0, o = 0;

    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        unsigned long cp;
        int n;
        if (c < 0x80) {
            cp = c;
            n = 0;
        } else if ((c & 0xe0) == 0xc0) {
            cp = c & 0x1f;
            n = 1;
        } else if ((c & 0xf0) == 0xe0) {
            cp = c & 0x0f;
            n = 2;
        } else if ((c & 0xf8) == 0xf0) {
            cp = c & 0x07;
            n = 3;
        } else {
            out[o++] = '?';
            i++;
            continue;
        }
        i++;
        int k;
        for (k = 0; k < n; k++) {
            if (i >= len || ((unsigned char)s[i] & 0xc0) != 0x80)
                break;
            cp = (cp << 6) | ((unsigned char)s[i] & 0x3f);
            i++;
        }
        if (k < n || cp > 0xff)
            out[o++] = '?';
        else
            out[o++] = (char)cp;
    }
    out[o] = 0;

    Str r(out);
    delete [] out;
    return r;
}

}
