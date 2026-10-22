#include "tparm.h"

#include <stdio.h>
#include <string.h>

namespace tu {

namespace {

const int k_stack = 32;

struct Machine {
    long stack[k_stack];
    int sp;
    long params[9];
    char* out;
    size_t n, len;

    void push(long v)
    {
        if (sp < k_stack)
            stack[sp++] = v;
    }
    long pop() { return sp > 0 ? stack[--sp] : 0; }
    void put(char c)
    {
        if (len + 1 < n)
            out[len] = c;
        len++;
    }
    void put_str(const char* s)
    {
        while (*s)
            put(*s++);
    }
};

/* Skip forward from s (just after %t or %e) to the matching %e or %;
 * of this level; returns the position after it. 'else_ok' stops at %e. */
const char* skip(const char* s, bool else_ok)
{
    int level = 0;
    while (*s) {
        if (*s == '%' && s[1]) {
            char c = s[1];
            s += 2;
            if (c == '?')
                level++;
            else if (c == ';') {
                if (level == 0)
                    return s;
                level--;
            } else if (c == 'e' && level == 0 && else_ok)
                return s;
            continue;
        }
        s++;
    }
    return s;
}

} /* namespace */

int tparm(char* out, size_t n, const char* cap, const long* p, int np)
{
    if (!cap)
        return -1;
    static long statics[26];
    long dynamics[26];
    memset(dynamics, 0, sizeof(dynamics));
    Machine m;
    m.sp = 0;
    m.out = out;
    m.n = n;
    m.len = 0;
    int i;
    for (i = 0; i < 9; i++)
        m.params[i] = i < np ? p[i] : 0;

    const char* s = cap;
    while (*s) {
        if (*s == '$' && s[1] == '<') {             /* padding */
            const char* e = strchr(s, '>');
            if (e) {
                s = e + 1;
                continue;
            }
        }
        if (*s != '%') {
            m.put(*s++);
            continue;
        }
        s++;
        char c = *s;
        if (!c)
            break;
        s++;
        switch (c) {
        case '%': m.put('%'); break;
        case 'c': m.put((char)m.pop()); break;
        case 's': {                                 /* no string params: a number */
            char b[24];
            sprintf(b, "%ld", m.pop());
            m.put_str(b);
            break;
        }
        case 'p':
            if (*s >= '1' && *s <= '9')
                m.push(m.params[*s - '1']);
            if (*s)
                s++;
            break;
        case 'P':
            if (*s >= 'a' && *s <= 'z')
                dynamics[*s - 'a'] = m.pop();
            else if (*s >= 'A' && *s <= 'Z')
                statics[*s - 'A'] = m.pop();
            if (*s)
                s++;
            break;
        case 'g':
            if (*s >= 'a' && *s <= 'z')
                m.push(dynamics[*s - 'a']);
            else if (*s >= 'A' && *s <= 'Z')
                m.push(statics[*s - 'A']);
            if (*s)
                s++;
            break;
        case '\'':
            m.push((unsigned char)*s);
            if (*s)
                s++;
            if (*s == '\'')
                s++;
            break;
        case '{': {
            long v = 0;
            bool neg = false;
            if (*s == '-') {
                neg = true;
                s++;
            }
            while (*s >= '0' && *s <= '9')
                v = v * 10 + (*s++ - '0');
            if (*s == '}')
                s++;
            m.push(neg ? -v : v);
            break;
        }
        case 'l': m.pop(); m.push(0); break;        /* strlen of a number param */
        case '+': { long b = m.pop(), a = m.pop(); m.push(a + b); break; }
        case '-': { long b = m.pop(), a = m.pop(); m.push(a - b); break; }
        case '*': { long b = m.pop(), a = m.pop(); m.push(a * b); break; }
        case '/': { long b = m.pop(), a = m.pop(); m.push(b ? a / b : 0); break; }
        case 'm': { long b = m.pop(), a = m.pop(); m.push(b ? a % b : 0); break; }
        case '&': { long b = m.pop(), a = m.pop(); m.push(a & b); break; }
        case '|': { long b = m.pop(), a = m.pop(); m.push(a | b); break; }
        case '^': { long b = m.pop(), a = m.pop(); m.push(a ^ b); break; }
        case '=': { long b = m.pop(), a = m.pop(); m.push(a == b); break; }
        case '>': { long b = m.pop(), a = m.pop(); m.push(a > b); break; }
        case '<': { long b = m.pop(), a = m.pop(); m.push(a < b); break; }
        case 'A': { long b = m.pop(), a = m.pop(); m.push(a && b); break; }
        case 'O': { long b = m.pop(), a = m.pop(); m.push(a || b); break; }
        case '!': m.push(!m.pop()); break;
        case '~': m.push(~m.pop()); break;
        case 'i': m.params[0]++; m.params[1]++; break;
        case '?': break;
        case 't':
            if (!m.pop())
                s = skip(s, true);
            break;
        case 'e': s = skip(s, false); break;
        case ';': break;
        default: {
            /* %[[:]flags][width[.precision]][doxX] */
            const char* spec = s - 1;
            char fmt[16];
            int k = 0;
            fmt[k++] = '%';
            if (*spec == ':')
                spec++;
            while (*spec && strchr("-+# 0123456789.", *spec) && k < 12)
                fmt[k++] = *spec++;
            if (*spec == 'd' || *spec == 'o' || *spec == 'x' || *spec == 'X') {
                fmt[k++] = 'l';
                fmt[k++] = *spec;
                fmt[k] = 0;
                char b[64];
                sprintf(b, fmt, m.pop());
                m.put_str(b);
                s = spec + 1;
            }
            break;
        }
        }
    }
    if (n > 0)
        out[m.len < n ? m.len : n - 1] = 0;
    return (int)m.len;
}

int tparm1(char* out, size_t n, const char* cap, long a)
{
    return tparm(out, n, cap, &a, 1);
}

int tparm2(char* out, size_t n, const char* cap, long a, long b)
{
    long p[2];
    p[0] = a;
    p[1] = b;
    return tparm(out, n, cap, p, 2);
}

} /* namespace tu */
