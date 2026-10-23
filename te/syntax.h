/*
 * te/syntax.h - syntax colouring: each byte of a line gets a class
 * (comment, keyword, string, ...), and the host picks the colours
 * (ttrcode: th's text schemes). Languages: C, C++, Python, shell, make.
 *
 * Lexing is per line, from the state the previous line ended in (inside
 * a block comment, a triple-quoted string, ...). The states at the
 * start of each line are cached; an edit invalidates them from the
 * edited line on (Document::take_changed), so typing relexes one line
 * and the lines after it only until the state matches again.
 */
#ifndef TE_SYNTAX_H
#define TE_SYNTAX_H

#include "document.h"

namespace te {

enum SynClass {
    SYN_NONE = 0,
    SYN_COMMENT,
    SYN_KEYWORD,
    SYN_TYPE,           /* type names: int, size_t, self, ... */
    SYN_STRING,
    SYN_NUMBER,
    SYN_PREPROC,        /* #include, decorators, $variables */
    SYN_COUNT
};

enum Language {
    LANG_NONE = 0,
    LANG_C,
    LANG_CPP,
    LANG_PYTHON,
    LANG_SHELL,
    LANG_MAKE,
    LANG_COUNT
};

/* From the file name, then the first line (#!/bin/sh, #!/usr/bin/env
 * python). */
Language language_for(const char* path, const char* first_line, size_t first_len);
const char* language_name(Language l);

class Syntax {
public:
    explicit Syntax(Document& doc);
    ~Syntax();

    void set_language(Language l);
    Language language() const { return lang_; }

    /* The class of each byte of line i, into out[0 .. length(i) - 1]. */
    void classify(long line, uint8_t* out);

    /* One line, from a start state; returns the state at its end. For
     * tests and for hosts without a Document. */
    static unsigned lex(Language l, const char* s, size_t n, unsigned state, uint8_t* out);

private:
    unsigned state_at(long line);

    Document& doc_;
    Language lang_;
    unsigned* states_;          /* state at the start of each line */
    long valid_;                /* states_[0 .. valid_ - 1] are known */
    long cap_;

    Syntax(const Syntax&);
    Syntax& operator=(const Syntax&);
};

} /* namespace te */

#endif
