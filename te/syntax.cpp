#include "syntax.h"

#include <stdlib.h>
#include <string.h>

namespace te {

namespace {

/* ---- words ---- */

const char* const k_c_keywords[] = {
    "auto", "break", "case", "const", "continue", "default", "do", "else", "enum", "extern",
    "for", "goto", "if", "inline", "register", "restrict", "return", "sizeof", "static",
    "struct", "switch", "typedef", "union", "volatile", "while", "true", "false", "NULL", 0
};
const char* const k_cpp_keywords[] = {
    "alignas", "alignof", "and", "asm", "catch", "class", "constexpr", "const_cast",
    "decltype", "delete", "dynamic_cast", "explicit", "export", "final", "friend", "mutable",
    "namespace", "new", "noexcept", "not", "nullptr", "operator", "or", "override", "private",
    "protected", "public", "reinterpret_cast", "static_assert", "static_cast", "template",
    "this", "thread_local", "throw", "try", "typeid", "typename", "using", "virtual", 0
};
const char* const k_c_types[] = {
    "bool", "char", "double", "float", "int", "long", "short", "signed", "unsigned", "void",
    "wchar_t", "_Bool", "FILE", 0
};
const char* const k_py_keywords[] = {
    "False", "None", "True", "and", "as", "assert", "async", "await", "break", "class",
    "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global",
    "if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return",
    "try", "while", "with", "yield", "match", "case", 0
};
const char* const k_py_types[] = {
    "self", "cls", "int", "str", "float", "bool", "bytes", "list", "dict", "tuple", "set",
    "object", "type", "complex", "frozenset", "bytearray", 0
};
const char* const k_sh_keywords[] = {
    "if", "then", "else", "elif", "fi", "case", "esac", "for", "select", "while", "until",
    "do", "done", "in", "function", "time", "return", "exit", "break", "continue", "local",
    "export", "readonly", "declare", "typeset", "unset", "shift", "source", "alias", "eval",
    "exec", "set", "trap", "wait", "foreach", "end", "endif", "switch", "endsw", "setenv", 0
};
const char* const k_make_keywords[] = {
    "ifeq", "ifneq", "ifdef", "ifndef", "else", "endif", "include", "-include", "sinclude",
    "define", "endef", "export", "unexport", "override", "vpath", 0
};

bool in_list(const char* const* list, const char* s, size_t n)
{
    for (int i = 0; list[i]; i++)
        if (strlen(list[i]) == n && memcmp(list[i], s, n) == 0)
            return true;
    return false;
}

bool ident_start(unsigned char c)
{
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80;
}

bool ident_char(unsigned char c)
{
    return ident_start(c) || (c >= '0' && c <= '9');
}

bool digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

void mark(uint8_t* out, size_t a, size_t b, uint8_t cls)
{
    if (out)
        memset(out + a, cls, b - a);
}

/* A number from s[i]: digits, letters (hex, suffixes), dots, exponent
 * signs and C++'s ' separators. */
size_t number_end(const char* s, size_t n, size_t i)
{
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (ident_char(c) || c == '.' || c == '\'') {
            i++;
        } else if ((c == '+' || c == '-') && i > 0 &&
                   (s[i - 1] == 'e' || s[i - 1] == 'E' || s[i - 1] == 'p' || s[i - 1] == 'P')) {
            i++;
        } else {
            break;
        }
    }
    return i;
}

/* A quoted string from s[i] (the quote); the end, past the closing
 * quote, or n with *open set when the line ends first. */
size_t string_end(const char* s, size_t n, size_t i, char q, bool escapes, bool* open)
{
    i++;
    while (i < n) {
        if (escapes && s[i] == '\\' && i + 1 < n) {
            i += 2;
            continue;
        }
        if (escapes && s[i] == '\\') {
            i++;
            break;
        }
        if (s[i] == q) {
            *open = false;
            return i + 1;
        }
        i++;
    }
    *open = true;
    return n;
}

/* ---- C and C++ ----
 * States: 0 code, 1 in a block comment, 2 a preprocessor line goes on. */
unsigned lex_c(bool cpp, const char* s, size_t n, unsigned state, uint8_t* out)
{
    size_t i = 0;
    bool preproc = state == 2;
    if (state == 1) {
        const char* e = 0;
        for (size_t k = 0; k + 1 < n; k++)
            if (s[k] == '*' && s[k + 1] == '/') {
                e = s + k;
                break;
            }
        if (!e) {
            mark(out, 0, n, SYN_COMMENT);
            return 1;
        }
        i = (size_t)(e - s) + 2;
        mark(out, 0, i, SYN_COMMENT);
    } else if (!preproc) {
        size_t k = 0;
        while (k < n && (s[k] == ' ' || s[k] == '\t'))
            k++;
        if (k < n && s[k] == '#')
            preproc = true;
    }
    mark(out, i, n, preproc ? SYN_PREPROC : SYN_NONE);
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            mark(out, i, n, SYN_COMMENT);
            return 0;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            size_t k = i + 2;
            while (k + 1 < n && !(s[k] == '*' && s[k + 1] == '/'))
                k++;
            if (k + 1 >= n) {
                mark(out, i, n, SYN_COMMENT);
                return 1;
            }
            mark(out, i, k + 2, SYN_COMMENT);
            i = k + 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            bool open;
            size_t e = string_end(s, n, i, (char)c, true, &open);
            mark(out, i, e, preproc && c == '"' ? SYN_PREPROC : SYN_STRING);
            if (!preproc)
                mark(out, i, e, SYN_STRING);
            i = e;
            continue;
        }
        if (digit(c) || (c == '.' && i + 1 < n && digit((unsigned char)s[i + 1]))) {
            size_t e = number_end(s, n, i);
            if (!preproc)
                mark(out, i, e, SYN_NUMBER);
            i = e;
            continue;
        }
        if (ident_start(c)) {
            size_t e = i;
            while (e < n && ident_char((unsigned char)s[e]))
                e++;
            if (!preproc) {
                size_t len = e - i;
                if (in_list(k_c_keywords, s + i, len) || (cpp && in_list(k_cpp_keywords, s + i, len)))
                    mark(out, i, e, SYN_KEYWORD);
                else if (in_list(k_c_types, s + i, len) ||
                         (len > 2 && s[e - 2] == '_' && s[e - 1] == 't'))
                    mark(out, i, e, SYN_TYPE);
            }
            i = e;
            continue;
        }
        i++;
    }
    if (preproc && n > 0 && s[n - 1] == '\\')
        return 2;
    return 0;
}

/* ---- Python ----
 * States: 0 code, 1 in """, 2 in '''. */
unsigned lex_python(const char* s, size_t n, unsigned state, uint8_t* out)
{
    size_t i = 0;
    mark(out, 0, n, SYN_NONE);
    if (state == 1 || state == 2) {
        const char* q = state == 1 ? "\"\"\"" : "'''";
        size_t k = 0;
        while (k + 2 < n && memcmp(s + k, q, 3) != 0) {
            if (s[k] == '\\')
                k++;
            k++;
        }
        if (k + 2 >= n) {
            mark(out, 0, n, SYN_STRING);
            return state;
        }
        i = k + 3;
        mark(out, 0, i, SYN_STRING);
    }
    /* A decorator line. */
    size_t ws = 0;
    while (ws < n && (s[ws] == ' ' || s[ws] == '\t'))
        ws++;
    if (i == 0 && ws < n && s[ws] == '@') {
        size_t e = ws + 1;
        while (e < n && (ident_char((unsigned char)s[e]) || s[e] == '.'))
            e++;
        mark(out, ws, e, SYN_PREPROC);
        i = e;
    }
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c == '#') {
            mark(out, i, n, SYN_COMMENT);
            return 0;
        }
        /* String prefixes: r, b, f, u and pairs, then a quote. */
        size_t q = i;
        while (q < n && q - i < 2 && strchr("rRbBfFuU", s[q]) && s[q])
            q++;
        if (q < n && (s[q] == '"' || s[q] == '\'') && (q == i || !ident_char((unsigned char)(i > 0 ? s[i - 1] : ' ')))) {
            char quote = s[q];
            bool raw = false;
            for (size_t k = i; k < q; k++)
                if (s[k] == 'r' || s[k] == 'R')
                    raw = true;
            if (q + 2 < n && s[q + 1] == quote && s[q + 2] == quote) {
                const char three[4] = { quote, quote, quote, 0 };
                size_t k = q + 3;
                while (k + 2 < n && memcmp(s + k, three, 3) != 0) {
                    if (s[k] == '\\' && !raw)
                        k++;
                    k++;
                }
                if (k + 2 >= n) {
                    mark(out, i, n, SYN_STRING);
                    return quote == '"' ? 1 : 2;
                }
                mark(out, i, k + 3, SYN_STRING);
                i = k + 3;
                continue;
            }
            bool open;
            size_t e = string_end(s, n, q, quote, !raw, &open);
            mark(out, i, e, SYN_STRING);
            i = e;
            continue;
        }
        if (digit(c) || (c == '.' && i + 1 < n && digit((unsigned char)s[i + 1]))) {
            size_t e = number_end(s, n, i);
            mark(out, i, e, SYN_NUMBER);
            i = e;
            continue;
        }
        if (ident_start(c)) {
            size_t e = i;
            while (e < n && ident_char((unsigned char)s[e]))
                e++;
            if (in_list(k_py_keywords, s + i, e - i))
                mark(out, i, e, SYN_KEYWORD);
            else if (in_list(k_py_types, s + i, e - i))
                mark(out, i, e, SYN_TYPE);
            i = e;
            continue;
        }
        i++;
    }
    return 0;
}

/* $name, ${...}, $(, $1, $?: the end of a variable reference at s[i]. */
size_t variable_end(const char* s, size_t n, size_t i)
{
    size_t e = i + 1;
    if (e >= n)
        return e;
    if (s[e] == '{') {
        while (e < n && s[e] != '}')
            e++;
        return e < n ? e + 1 : n;
    }
    if (s[e] == '(')
        return e + 1;
    if (ident_start((unsigned char)s[e])) {
        while (e < n && ident_char((unsigned char)s[e]))
            e++;
        return e;
    }
    if (strchr("0123456789?#@*$!-_", s[e]))
        return e + 1;
    return e;
}

/* ---- shell ----
 * States: 0 code, 1 in '...', 2 in "...". */
unsigned lex_shell(const char* s, size_t n, unsigned state, uint8_t* out)
{
    size_t i = 0;
    mark(out, 0, n, SYN_NONE);
    if (state == 1 || state == 2) {
        char q = state == 1 ? '\'' : '"';
        size_t k = 0;
        while (k < n && s[k] != q) {
            if (q == '"' && s[k] == '\\')
                k++;
            k++;
        }
        if (k >= n) {
            mark(out, 0, n, SYN_STRING);
            return state;
        }
        i = k + 1;
        mark(out, 0, i, SYN_STRING);
    }
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\') {
            i += 2;
            continue;
        }
        if (c == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t' || s[i - 1] == ';')) {
            mark(out, i, n, SYN_COMMENT);
            return 0;
        }
        if (c == '\'' || c == '"') {
            bool open;
            size_t e = string_end(s, n, i, (char)c, c == '"', &open);
            mark(out, i, e, SYN_STRING);
            /* Variables inside double quotes stand out. */
            if (c == '"')
                for (size_t k = i + 1; k < e; k++)
                    if (s[k] == '$' && (k == 0 || s[k - 1] != '\\')) {
                        size_t v = variable_end(s, e, k);
                        mark(out, k, v, SYN_PREPROC);
                        k = v - 1;
                    }
            if (open)
                return c == '\'' ? 1 : 2;
            i = e;
            continue;
        }
        if (c == '$') {
            size_t e = variable_end(s, n, i);
            mark(out, i, e, SYN_PREPROC);
            i = e;
            continue;
        }
        if (ident_char(c)) {
            size_t e = i;
            while (e < n && (ident_char((unsigned char)s[e]) || s[e] == '-' || s[e] == '.'))
                e++;
            bool word_start = i == 0 || strchr(" \t;|&(`", s[i - 1]);
            bool word_end = e == n || strchr(" \t;|&)`", s[e]);
            bool all_digits = true;
            for (size_t k = i; k < e; k++)
                if (!digit((unsigned char)s[k]))
                    all_digits = false;
            if (word_start && word_end && in_list(k_sh_keywords, s + i, e - i))
                mark(out, i, e, SYN_KEYWORD);
            else if (all_digits && word_end)
                mark(out, i, e, SYN_NUMBER);
            i = e;
            continue;
        }
        i++;
    }
    return 0;
}

/* ---- make ---- */
unsigned lex_make(const char* s, size_t n, uint8_t* out)
{
    mark(out, 0, n, SYN_NONE);
    if (n > 0 && s[0] == '\t') {
        /* A recipe line: shell, with make's variables. */
        lex_shell(s, n, 0, out);
        for (size_t k = 0; k + 1 < n; k++)
            if (s[k] == '$' && (s[k + 1] == '(' || s[k + 1] == '{')) {
                char close = s[k + 1] == '(' ? ')' : '}';
                size_t e = k + 2;
                while (e < n && s[e] != close)
                    e++;
                mark(out, k, e < n ? e + 1 : n, SYN_PREPROC);
            }
        return 0;
    }
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t'))
        i++;
    /* A directive, an assignment's name, or a rule's targets. */
    size_t w = i;
    while (w < n && !strchr(" \t:=+?#", s[w]))
        w++;
    if (w > i && in_list(k_make_keywords, s + i, w - i)) {
        mark(out, i, w, SYN_KEYWORD);
    } else {
        size_t k = w;
        while (k < n && (s[k] == ' ' || s[k] == '\t'))
            k++;
        bool assign = k < n && (s[k] == '=' || ((s[k] == ':' || s[k] == '+' || s[k] == '?') &&
                                                k + 1 < n && s[k + 1] == '='));
        if (assign && w > i) {
            mark(out, i, w, SYN_TYPE);
        } else {
            /* targets: everything before a single ':' */
            for (size_t c = i; c < n && s[c] != '#'; c++)
                if (s[c] == ':' && !(c + 1 < n && s[c + 1] == '=')) {
                    mark(out, i, c, SYN_TYPE);
                    break;
                }
        }
    }
    for (size_t k = i; k < n; k++) {
        if (s[k] == '#' && (k == 0 || s[k - 1] != '\\')) {
            mark(out, k, n, SYN_COMMENT);
            break;
        }
        if (s[k] == '$') {
            size_t e;
            if (k + 1 < n && (s[k + 1] == '(' || s[k + 1] == '{')) {
                char close = s[k + 1] == '(' ? ')' : '}';
                e = k + 2;
                int depth = 1;
                while (e < n && depth > 0) {
                    if (s[e] == s[k + 1])
                        depth++;
                    else if (s[e] == close)
                        depth--;
                    e++;
                }
            } else {
                e = k + 2 <= n ? k + 2 : n;
            }
            mark(out, k, e, SYN_PREPROC);
            k = e - 1;
        }
    }
    return 0;
}

bool ends_with(const char* s, const char* suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

} /* namespace */

Language language_for(const char* path, const char* first, size_t first_len)
{
    const char* base = path ? strrchr(path, '/') : 0;
    base = base ? base + 1 : (path ? path : "");
    static const char* const c_ext[] = { ".c", 0 };
    static const char* const cpp_ext[] = {
        ".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".h++", ".h", ".ipp", ".inl", 0
    };
    static const char* const py_ext[] = { ".py", ".pyw", ".pyi", 0 };
    static const char* const sh_ext[] = {
        ".sh", ".bash", ".ksh", ".zsh", ".csh", ".tcsh", ".bashrc", ".profile", ".drive", 0
    };
    for (int i = 0; c_ext[i]; i++)
        if (ends_with(base, c_ext[i]))
            return LANG_C;
    for (int i = 0; cpp_ext[i]; i++)
        if (ends_with(base, cpp_ext[i]))
            return LANG_CPP;
    for (int i = 0; py_ext[i]; i++)
        if (ends_with(base, py_ext[i]))
            return LANG_PYTHON;
    for (int i = 0; sh_ext[i]; i++)
        if (ends_with(base, sh_ext[i]))
            return LANG_SHELL;
    if (strcmp(base, "Makefile") == 0 || strcmp(base, "makefile") == 0 ||
        strcmp(base, "GNUmakefile") == 0 || ends_with(base, ".mk") || ends_with(base, ".mak"))
        return LANG_MAKE;
    if (first && first_len > 2 && first[0] == '#' && first[1] == '!') {
        char line[128];
        size_t n = first_len < sizeof(line) - 1 ? first_len : sizeof(line) - 1;
        memcpy(line, first, n);
        line[n] = 0;
        if (strstr(line, "python"))
            return LANG_PYTHON;
        if (strstr(line, "sh"))         /* sh, bash, ksh, zsh, csh */
            return LANG_SHELL;
    }
    return LANG_NONE;
}

const char* language_name(Language l)
{
    static const char* const names[LANG_COUNT] = { "Plain text", "C", "C++", "Python", "Shell", "Make" };
    return l >= 0 && l < LANG_COUNT ? names[l] : "";
}

unsigned Syntax::lex(Language l, const char* s, size_t n, unsigned state, uint8_t* out)
{
    switch (l) {
    case LANG_C: return lex_c(false, s, n, state, out);
    case LANG_CPP: return lex_c(true, s, n, state, out);
    case LANG_PYTHON: return lex_python(s, n, state, out);
    case LANG_SHELL: return lex_shell(s, n, state, out);
    case LANG_MAKE: return lex_make(s, n, out);
    default:
        mark(out, 0, n, SYN_NONE);
        return 0;
    }
}

Syntax::Syntax(Document& doc) : doc_(doc), lang_(LANG_NONE), states_(0), valid_(0), cap_(0)
{
}

Syntax::~Syntax()
{
    free(states_);
}

void Syntax::set_language(Language l)
{
    lang_ = l;
    valid_ = 0;
}

unsigned Syntax::state_at(long line)
{
    long changed = doc_.take_changed();
    if (changed >= 0 && changed + 1 < valid_)
        valid_ = changed + 1;
    if (line >= cap_) {
        long nc = cap_ ? cap_ : 1024;
        while (nc <= line)
            nc *= 2;
        unsigned* p = (unsigned*)realloc(states_, sizeof(unsigned) * (size_t)nc);
        if (!p)
            return 0;
        states_ = p;
        cap_ = nc;
    }
    if (valid_ == 0) {
        states_[0] = 0;
        valid_ = 1;
    }
    while (valid_ <= line) {
        long l = valid_ - 1;
        states_[valid_] = lex(lang_, doc_.line(l), doc_.length(l), states_[l], 0);
        valid_++;
    }
    return states_[line];
}

void Syntax::classify(long line, uint8_t* out)
{
    if (line < 0 || line >= doc_.lines())
        return;
    if (lang_ == LANG_NONE) {
        memset(out, SYN_NONE, doc_.length(line));
        return;
    }
    lex(lang_, doc_.line(line), doc_.length(line), state_at(line), out);
}

} /* namespace te */
