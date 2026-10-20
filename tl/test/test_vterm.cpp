/*
 * Runs libvterm's test fixtures (third_party/libvterm/t/NN*.test) against
 * tl::Terminal. Only the assertions about the final screen and cursor are
 * checked (?cursor, ?screen_row, ?screen_chars); libvterm's callback
 * expectations (movecursor, putglyph, ...) and its pen/cell dumps are
 * specific to its API and skipped.
 *
 * Where tl deliberately behaves like xterm instead of libvterm, the
 * failing assertion is listed in tl/test/data/vterm-known.txt as
 * "file:line reason"; those are reported but do not fail the run.
 */
#include "tltest.h"
#include "../terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace tl;

const char* tl_test_vterm_dir = "third_party/libvterm/t";

namespace {

class NullHost : public Host {};

struct Known {
    char key[128];
    bool hit;
};

struct Runner {
    Terminal* term;
    NullHost host;
    bool utf8;
    bool reflow;            /* libvterm reflows only with WANTSCREEN r */
    const char* file;
    int line;
    char label[128];
    int checks, fails, known_fails, skipped;
    Known* known;
    int n_known;
    bool verbose;

    Runner() : term(0), utf8(false), reflow(false), file(""), line(0), checks(0), fails(0),
               known_fails(0), skipped(0), known(0), n_known(0), verbose(false)
    {
        label[0] = 0;
    }
    ~Runner() { delete term; }

    void init()
    {
        delete term;
        term = new Terminal();
        term->init(80, 25, 0);
        term->set_host(&host);
        term->set_utf8(utf8);
        term->set_reflow(reflow);
    }

    Known* find_known()
    {
        char key[128];
        sprintf(key, "%s:%d", file, line);
        for (int i = 0; i < n_known; i++)
            if (strcmp(known[i].key, key) == 0)
                return &known[i];
        return 0;
    }

    void result(bool ok, const char* what, const char* want, const char* got)
    {
        checks++;
        Known* k = find_known();
        if (ok) {
            if (k) {
                k->hit = true;
                fprintf(stderr, "vterm %s:%d: [%s] listed as known but passes now\n", file, line, label);
            }
            return;
        }
        if (k) {
            k->hit = true;
            known_fails++;
            if (verbose)
                fprintf(stderr, "vterm %s:%d: [%s] known: %s\n", file, line, label, what);
            return;
        }
        fails++;
        tl_test_failures++;
        fprintf(stderr, "vterm %s:%d: [%s] %s\n     got: %s\n    want: %s\n",
                file, line, label, what, got, want);
    }

    /* Decode a Perl double-quoted string body (after the opening quote).
     * With as_codepoints, \xHH is a code point appended as UTF-8;
     * otherwise it is a raw byte (what PUSH sends). Returns the index of
     * the closing quote. */
    static size_t perl_string(const char* s, Buffer& out, bool as_codepoints = false)
    {
        size_t i = 0;
        while (s[i] && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') {
                out.append_byte((uint8_t)c);
                continue;
            }
            c = s[i++];
            switch (c) {
            case 'e': out.append_byte(0x1b); break;
            case 'n': out.append_byte('\n'); break;
            case 'r': out.append_byte('\r'); break;
            case 't': out.append_byte('\t'); break;
            case 'b': out.append_byte('\b'); break;
            case 'a': out.append_byte(7); break;
            case 'f': out.append_byte(12); break;
            case 'x': {
                unsigned long v = 0;
                if (s[i] == '{') {
                    i++;
                    while (s[i] && s[i] != '}') {
                        char d = s[i++];
                        v = v * 16 + (d <= '9' ? d - '0' : (d | 0x20) - 'a' + 10);
                    }
                    if (s[i] == '}')
                        i++;
                    out.append_utf8((uint32_t)v);
                    break;
                }
                int n = 0;
                while (n < 2 && ((s[i] >= '0' && s[i] <= '9') || ((s[i] | 0x20) >= 'a' && (s[i] | 0x20) <= 'f'))) {
                    char d = s[i++];
                    v = v * 16 + (d <= '9' ? d - '0' : (d | 0x20) - 'a' + 10);
                    n++;
                }
                if (as_codepoints)
                    out.append_utf8((uint32_t)v);
                else
                    out.append_byte((uint8_t)v);
                break;
            }
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
                unsigned v = (unsigned)(c - '0');
                int n = 1;
                while (n < 3 && s[i] >= '0' && s[i] <= '7') {
                    v = v * 8 + (unsigned)(s[i++] - '0');
                    n++;
                }
                out.append_byte((uint8_t)v);
                break;
            }
            case 0:
                return i - 1;
            default:
                out.append_byte((uint8_t)c);
                break;
            }
        }
        return i;
    }

    /* The expected value of an assertion, as UTF-8. libvterm's harness
     * compares code points: a quoted string is one code point per
     * character (\xHH and \x{HHHH} are code points), a bare list is
     * 0xHHHH code points. */
    static void expectation(const char* s, Buffer& out)
    {
        while (*s == ' ')
            s++;
        if (*s == '"') {
            Buffer raw;
            perl_string(s + 1, raw, true);
            out.append(raw.data(), raw.size());
            return;
        }
        while (*s) {
            char* end;
            unsigned long v = strtoul(s, &end, 16);
            if (end == s)
                break;
            out.append_utf8((uint32_t)v);
            s = end;
            while (*s == ',' || *s == ' ')
                s++;
        }
    }

    /* Columns c0..c1-1 of row r, trailing blanks dropped. */
    void row_chars(int r, int c0, int c1, Buffer& out)
    {
        const Grid& g = term->grid();
        if (r < 0 || r >= g.rows())
            return;
        const Row& row = g.row(r);
        if (c1 > row.cols)
            c1 = row.cols;
        int last = c1 - 1;
        while (last >= c0 && row.cells[last].is_empty() && row.cells[last].wide() != CELL_SPACER_TAIL)
            last--;
        for (int x = c0; x <= last; x++) {
            const Cell& c = row.cells[x];
            if (c.wide() == CELL_SPACER_TAIL)
                continue;
            if (c.is_empty() || c.wide() == CELL_SPACER_HEAD) {
                out.append_byte(' ');
                continue;
            }
            uint32_t cps[TL_GRAPHEME_MAX];
            int n = term->cell_text(c, cps, TL_GRAPHEME_MAX);
            for (int i = 0; i < n; i++)
                out.append_utf8(cps[i]);
        }
    }

    void do_line(const char* l)
    {
        while (*l == ' ' || *l == '\t')
            l++;
        if (!*l || *l == '#')
            return;
        if (*l == '!') {
            strncpy(label, l + 1, sizeof(label) - 1);
            label[sizeof(label) - 1] = 0;
            return;
        }
        if (strncmp(l, "$SEQ", 4) == 0) {
            int lo, hi;
            const char* colon = strchr(l, ':');
            if (!colon || sscanf(l + 4, "%d %d", &lo, &hi) != 2)
                return;
            for (int v = lo; v <= hi; v++) {
                Buffer b;
                char num[16];
                sprintf(num, "%d", v);
                for (const char* p = colon + 1; *p; p++) {
                    if (p[0] == '\\' && p[1] == '#') {
                        b.append(num);
                        p++;
                    } else {
                        b.append_byte((uint8_t)*p);
                    }
                }
                do_line(b.c_str());
            }
            return;
        }
        if (strncmp(l, "$REP", 4) == 0) {
            int n;
            const char* colon = strchr(l, ':');
            if (!colon || sscanf(l + 4, "%d", &n) != 1)
                return;
            for (int i = 0; i < n; i++)
                do_line(colon + 1);
            return;
        }
        if (strcmp(l, "INIT") == 0) {
            if (!term)
                init();
            return;
        }
        if (strncmp(l, "WANTSCREEN", 10) == 0) {
            bool sense = true;
            for (const char* p = l + 10; *p; p++) {
                if (*p == '+') sense = true;
                else if (*p == '-') sense = false;
                else if (*p == 'r') reflow = sense;
            }
            if (term)
                term->set_reflow(reflow);
            return;
        }
        if (strncmp(l, "UTF8 ", 5) == 0) {
            utf8 = atoi(l + 5) != 0;
            if (term)
                term->set_utf8(utf8);
            return;
        }
        if (!term)
            return;
        if (strcmp(l, "RESET") == 0) {
            term->reset();
            return;
        }
        if (strncmp(l, "RESIZE ", 7) == 0) {
            int r, c;
            if (sscanf(l + 7, "%d, %d", &r, &c) == 2)
                term->resize(c, r);
            return;
        }
        if (strncmp(l, "PUSH ", 5) == 0) {
            const char* q = strchr(l, '"');
            if (!q)
                return;
            Buffer b;
            size_t end = perl_string(q + 1, b);
            int times = 1;
            const char* rest = q + 1 + end;
            if (*rest == '"' && rest[1] == 'x')
                times = atoi(rest + 2);         /* Perl's "..."xN */
            for (int i = 0; i < times; i++)
                term->feed(b.data(), b.size());
            return;
        }
        if (strncmp(l, "?cursor", 7) == 0) {
            const char* eq = strchr(l, '=');
            int r, c;
            if (!eq || sscanf(eq + 1, "%d,%d", &r, &c) != 2)
                return;
            char want[32], got[32];
            sprintf(want, "%d,%d", r, c);
            sprintf(got, "%d,%d", term->cursor().y, term->cursor().x);
            result(strcmp(want, got) == 0, "?cursor", want, got);
            return;
        }
        if (strncmp(l, "?screen_row", 11) == 0 || strncmp(l, "?screen_chars", 13) == 0) {
            const char* eq = strchr(l, '=');
            if (!eq)
                return;
            int r0, c0, r1, c1;
            Buffer want, got;
            if (l[8] == 'r') {
                if (sscanf(l + 11, "%d", &r0) != 1)
                    return;
                c0 = 0;
                c1 = term->cols();
                r1 = r0 + 1;
            } else if (sscanf(l + 13, "%d,%d,%d,%d", &r0, &c0, &r1, &c1) != 4) {
                if (sscanf(l + 13, "%d", &r0) != 1)
                    return;
                r1 = r0 + 1;
                c0 = 0;
                c1 = term->cols();
            }
            for (int r = r0; r < r1; r++) {
                if (r > r0)
                    got.append_byte('\n');
                row_chars(r, c0, c1, got);
            }
            expectation(eq + 1, want);
            result(strcmp(want.c_str(), got.c_str()) == 0, l[8] == 'r' ? "?screen_row" : "?screen_chars",
                   want.c_str(), got.c_str());
            return;
        }
        skipped++;
    }

    bool run_file(const char* dir, const char* name)
    {
        char path[1024];
        sprintf(path, "%s/%s", dir, name);
        FILE* fp = fopen(path, "r");
        if (!fp)
            return false;
        file = name;
        line = 0;
        label[0] = 0;
        utf8 = false;
        reflow = false;
        delete term;
        term = 0;
        char buf[4096];
        while (fgets(buf, sizeof(buf), fp)) {
            line++;
            size_t n = strlen(buf);
            while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
                buf[--n] = 0;
            if (strncmp(buf, "WANTPARSER", 10) == 0)
                break;                      /* parser-level file: tested elsewhere */
            do_line(buf);
        }
        fclose(fp);
        return true;
    }

    void load_known(const char* path)
    {
        FILE* fp = fopen(path, "r");
        if (!fp)
            return;
        char buf[512];
        int cap = 0;
        while (fgets(buf, sizeof(buf), fp)) {
            if (buf[0] == '#' || buf[0] == '\n')
                continue;
            char key[128];
            if (sscanf(buf, "%127s", key) != 1)
                continue;
            if (n_known == cap) {
                cap = cap ? cap * 2 : 32;
                known = (Known*)realloc(known, (size_t)cap * sizeof(Known));
            }
            strcpy(known[n_known].key, key);
            known[n_known].hit = false;
            n_known++;
        }
        fclose(fp);
    }
};

/* The fixtures in order. A directory listing is not portable C++98, so
 * the names are listed here; a missing file is skipped. */
const char* const k_files[] = {
    "03encoding_utf8.test",
    "10state_putglyph.test",
    "11state_movecursor.test",
    "12state_scroll.test",
    "13state_edit.test",
    "14state_encoding.test",
    "15state_mode.test",
    "16state_resize.test",
    "17state_mouse.test",
    "18state_termprops.test",
    "20state_wrapping.test",
    "21state_tabstops.test",
    "22state_save.test",
    "25state_input.test",
    "26state_query.test",
    "27state_reset.test",
    "28state_dbl_wh.test",
    "29state_fallback.test",
    "30state_pen.test",
    "31state_rep.test",
    "32state_flow.test",
    "40state_selection.test",
    "60screen_ascii.test",
    "61screen_unicode.test",
    "62screen_damage.test",
    "63screen_resize.test",
    "64screen_pen.test",
    "65screen_protect.test",
    "66screen_extent.test",
    "67screen_dbl_wh.test",
    "68screen_termprops.test",
    "69screen_pushline.test",
    "69screen_reflow.test",
    "90vttest_01-movement-1.test",
    "90vttest_01-movement-2.test",
    "90vttest_01-movement-3.test",
    "90vttest_01-movement-4.test",
    "90vttest_02-screen-1.test",
    "90vttest_02-screen-2.test",
    "90vttest_02-screen-3.test",
    "90vttest_02-screen-4.test",
    "92lp1640917.test",
    0
};

} /* namespace */

void tl_test_vterm()
{
    Runner r;
    r.verbose = getenv("TL_VTERM_VERBOSE") != 0;
    char path[1024];
    sprintf(path, "%s/vterm-known.txt", tl_test_data_dir);
    r.load_known(path);
    int files = 0;
    for (int i = 0; k_files[i]; i++)
        if (r.run_file(tl_test_vterm_dir, k_files[i]))
            files++;
    tl_test_checks += r.checks;
    if (files == 0) {
        fprintf(stderr, "%s: no libvterm fixtures found, skipping\n", tl_test_vterm_dir);
        return;
    }
    for (int i = 0; i < r.n_known; i++)
        if (!r.known[i].hit)
            fprintf(stderr, "vterm-known.txt: %s did not run\n", r.known[i].key);
    printf("libvterm fixtures: %d files, %d checks, %d failures, %d known differences, %d lines skipped\n",
           files, r.checks, r.fails, r.known_fails, r.skipped);
    free(r.known);
}
