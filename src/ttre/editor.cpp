#include "editor.h"

#include "../../te/file.h"
#include "../../th/scheme.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

namespace ttre {

namespace {

const tu::MenuItem k_file[] = {
    { "&Save", "F2", Editor::M_SAVE },
    { "Save &as...", "S-F2", Editor::M_SAVE_AS },
    { 0, 0, -1 },
    { "&Quit", "F10", Editor::M_QUIT },
    { 0, 0, 0 }
};
const tu::MenuItem k_edit[] = {
    { "&Undo", "C-u", Editor::M_UNDO },
    { "&Redo", "M-r", Editor::M_REDO },
    { 0, 0, -1 },
    { "&Mark block", "F3", Editor::M_MARK },
    { "&Copy block", "F5", Editor::M_COPY },
    { "Mo&ve block", "F6", Editor::M_MOVE },
    { "&Delete block", "F8", Editor::M_DELETE_BLOCK },
    { "Delete &line", "C-y", Editor::M_DELETE_LINE },
    { "Select a&ll", 0, Editor::M_SELECT_ALL },
    { 0, 0, 0 }
};
const tu::MenuItem k_search[] = {
    { "&Find...", "F7", Editor::M_FIND },
    { "Find &next", "S-F7", Editor::M_FIND_NEXT },
    { "Find &previous", 0, Editor::M_FIND_PREV },
    { "&Replace...", "F4", Editor::M_REPLACE },
    { 0, 0, -1 },
    { "&Go to line...", "M-l", Editor::M_GOTO },
    { 0, 0, 0 }
};
const tu::MenuItem k_options[] = {
    { "&Editor...", 0, Editor::M_OPTIONS },
    { "&Keys...", "F1", Editor::M_HELP },
    { 0, 0, 0 }
};

const char* const k_shapes[] = { "default", "bar", "block", "underline", 0 };
const char* const k_shape_labels[] = { "Terminal's", "Bar", "Block", "Underline", 0 };

size_t utf8(uint32_t cp, char* b)
{
    if (cp < 0x80) {
        b[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        b[0] = (char)(0xc0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }
    if (cp < 0x10000) {
        b[0] = (char)(0xe0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        b[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    b[0] = (char)(0xf0 | (cp >> 18));
    b[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    b[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    b[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

long now_ms()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (long)tv.tv_sec * 1000L + (long)(tv.tv_usec / 1000);
}

bool is_rgb(tu::Color c)
{
    return c != tu::COLOR_DEFAULT && c <= 0xffffff;
}

} /* namespace */

Editor::Editor(tu::Term& term, const char* path, long line)
    : term_(term), screen_(term), path_(path ? path : ""), start_line_(line), view_(doc_),
      syntax_(doc_), marking_(false), quit_(false), clip_(0), clip_len_(0), find_text_(""),
      match_case_(false), whole_word_(false), backwards_(false), last_click_(0),
      click_count_(0), dragging_(false)
{
    note_[0] = 0;
    set_.load();
    input_.init(term.info(), term.utf8());
    view_.set_syntax(&syntax_);
    apply_settings();
}

Editor::~Editor()
{
    free(clip_);
}

int Editor::load()
{
    if (path_.empty()) {
        doc_.set_text("", 0);
        return 0;
    }
    int err = te::load_file(doc_, path_.c_str(), 0);
    if (err == ENOENT)
        err = 0;                /* a new file */
    syntax_.set_language(te::language_for(path_.c_str(), doc_.line(0), doc_.length(0)));
    if (start_line_ > 0)
        view_.goto_line(start_line_);
    return err;
}

/* ---- the look ---- */

/* The settings on the view, the terminal and the colours: the text from
 * a th scheme if one is chosen, else from the theme, with syntax colours
 * that suit its background. */
void Editor::apply_settings()
{
    const char* t = set_.tier.c_str();
    if (strcmp(t, "basic") == 0)
        term_.set_tier(tu::TIER_BASIC);
    else if (strcmp(t, "ansi") == 0)
        term_.set_tier(tu::TIER_ANSI);
    else if (strcmp(t, "256") == 0)
        term_.set_tier(tu::TIER_256);
    else if (strcmp(t, "modern") == 0)
        term_.set_tier(tu::TIER_MODERN);
    theme_ = tu::theme_for(term_.tier(), set_.theme.c_str());
    view_.tab_width = set_.tab_width;
    view_.indent_width = set_.indent_width;
    view_.insert_spaces = set_.insert_spaces;
    view_.auto_indent = set_.auto_indent;
    view_.show_whitespace = set_.show_whitespace;

    bool color = term_.tier() != tu::TIER_BASIC && is_rgb(theme_.text.bg);
    col_.tints = term_.tier() >= tu::TIER_256 && color;
    col_.text = theme_.text;
    col_.select = theme_.text_select;
    col_.ctrl = theme_.text_ctrl;
    col_.status = theme_.text_status;
    th::Scheme sc;
    bool scheme = color && !set_.scheme.empty() && th::find_scheme(set_.scheme.c_str(), &sc);
    if (!scheme && color) {
        /* The theme's text: a scheme around its colours. Blue themes
         * (classic, midnight) get mc's bright syntax colours. */
        sc = th::Scheme();
        sc.color[th::SC_FOREGROUND] = theme_.text.fg;
        sc.color[th::SC_BACKGROUND] = theme_.text.bg;
        sc.set[th::SC_FOREGROUND] = sc.set[th::SC_BACKGROUND] = true;
        sc.dark = th::brightness(theme_.text.bg) < 128;
        th::Color bg = theme_.text.bg;
        bool blue = (bg & 0xff) > ((bg >> 16) & 0xff) + ((bg >> 8) & 0xff);
        if (blue) {
            static const th::SchemeColor k[5] = { th::SC_COMMENT, th::SC_KEYWORD, th::SC_STRING,
                                                  th::SC_NUMBER, th::SC_PREPROCESSOR };
            static const th::Color v[5] = { 0xa8a8a8, 0xffff55, 0x55ff55, 0x55ffff, 0xff55ff };
            for (int i = 0; i < 5; i++) {
                sc.color[k[i]] = v[i];
                sc.set[k[i]] = true;
            }
        }
    }
    if (scheme) {
        col_.text = tu::Style(sc.get(th::SC_FOREGROUND), sc.get(th::SC_BACKGROUND));
        col_.select = tu::Style(sc.get(th::SC_SELECTION_FG), sc.get(th::SC_SELECTION_BG));
        col_.ctrl = tu::Style(sc.get(th::SC_CONTROL), sc.get(th::SC_BACKGROUND));
    }
    tu::Color bg = col_.text.bg;
    for (int i = 0; i < te::SYN_COUNT; i++)
        col_.syntax[i] = col_.text;
    if (color) {
        col_.syntax[te::SYN_COMMENT].fg = sc.get(th::SC_COMMENT);
        col_.syntax[te::SYN_KEYWORD].fg = sc.get(th::SC_KEYWORD);
        col_.syntax[te::SYN_TYPE].fg = sc.get(th::SC_KEYWORD);
        col_.syntax[te::SYN_STRING].fg = sc.get(th::SC_STRING);
        col_.syntax[te::SYN_NUMBER].fg = sc.get(th::SC_NUMBER);
        col_.syntax[te::SYN_PREPROC].fg = sc.get(th::SC_PREPROCESSOR);
        col_.whitespace = tu::Style(th::mix(bg, col_.text.fg, 40), bg);
        col_.gutter = tu::Style(sc.get(th::SC_GUTTER_FG), sc.get(th::SC_GUTTER_BG));
    } else {
        /* No colours: keywords in bold. */
        col_.syntax[te::SYN_KEYWORD].attrs |= tu::ATTR_BOLD;
        col_.syntax[te::SYN_TYPE].attrs |= tu::ATTR_BOLD;
        col_.whitespace = col_.text;
        col_.gutter = col_.text;
    }
    col_.current_line = col_.text;
    col_.ruler_bg = tu::COLOR_DEFAULT;
    if (col_.tints) {
        col_.current_line.bg = scheme ? sc.get(th::SC_CURRENT_LINE) : th::mix(bg, col_.text.fg, 8);
        col_.ruler_bg = scheme ? th::mix(bg, sc.get(th::SC_RULER), 50) : th::mix(bg, col_.text.fg, 14);
    }

    /* The terminal's cursor. */
    const char* sh = set_.cursor_shape.c_str();
    tu::CursorShape shape = strcmp(sh, "bar") == 0         ? tu::CURSOR_BAR
                            : strcmp(sh, "block") == 0     ? tu::CURSOR_BLOCK
                            : strcmp(sh, "underline") == 0 ? tu::CURSOR_UNDERLINE
                                                           : tu::CURSOR_DEFAULT;
    term_.set_cursor_shape(shape, set_.cursor_blink);
    th::Color cc;
    term_.set_cursor_color(!set_.cursor_color.empty() &&
                                   th::parse_hex(set_.cursor_color.c_str(), &cc)
                               ? (long)cc
                               : -1L);
    screen_.invalidate();
}

int Editor::gutter() const
{
    if (!set_.line_numbers)
        return 0;
    int d = 1;
    for (long n = doc_.lines(); n >= 10; n /= 10)
        d++;
    return (d < 3 ? 3 : d) + 1;
}

void Editor::draw_key_bar()
{
    static const char* const labels[10] = {
        "Help", "Save", "Mark", "Replac", "Copy", "Move", "Search", "Delete", "Menu", "Quit"
    };
    int w = screen_.cols(), y = screen_.rows() - 1;
    int slot = w / 10;
    if (slot < 3)
        return;
    screen_.fill(0, y, w, 1, ' ', theme_.key_num);
    for (int i = 0; i < 10; i++) {
        int x = i * slot;
        char num[4];
        sprintf(num, "%d", i + 1);
        int nw = (int)strlen(num);
        screen_.text(x, y, num, theme_.key_num);
        int lw = (i == 9 ? w - x : slot) - nw;
        screen_.fill(x + nw, y, lw, 1, ' ', theme_.key_label);
        screen_.text(x + nw, y, labels[i], theme_.key_label, lw);
    }
}

void Editor::draw()
{
    int w = screen_.cols(), h = screen_.rows();
    int g = gutter();
    view_.set_size(w - g > 1 ? w - g : 1, h > 3 ? h - 2 : 1);
    view_.follow();
    screen_.fill(0, 0, w, h, ' ', col_.text);
    /* The status line. */
    char st[900];
    const char* name = strrchr(path_.c_str(), '/');
    name = path_.empty() ? "(new file)" : name ? name + 1 : path_.c_str();
    te::Pos cur = view_.cursor();
    te::Language lang = syntax_.language();
    snprintf(st, sizeof(st), " %s%s   line %ld of %ld, column %d%s%s%s", name,
             doc_.modified() ? " *" : "", cur.line + 1, doc_.lines(), view_.column_of(cur) + 1,
             lang != te::LANG_NONE ? "   " : "", lang != te::LANG_NONE ? te::language_name(lang) : "",
             marking_ ? "   [marking: F3 ends]" : "");
    screen_.fill(0, 0, w, 1, ' ', col_.status);
    screen_.text(0, 0, note_[0] ? note_ : st, col_.status, w);

    /* The text. */
    int cols = view_.cols();
    te::Cell* row = (te::Cell*)malloc(sizeof(te::Cell) * (size_t)cols);
    long caret = set_.highlight_line && !view_.has_selection() ? cur.line - view_.top() : -1;
    int ruler_x = set_.ruler > 0 && col_.ruler_bg != tu::COLOR_DEFAULT
                      ? set_.ruler - view_.left() : -1;
    for (int r = 0; row && r < view_.rows(); r++) {
        bool have = view_.row(r, row);
        int y = r + 1;
        tu::Style base = have && r == caret ? col_.current_line : col_.text;
        if (g) {
            char num[24];
            num[0] = 0;
            if (have)
                snprintf(num, sizeof(num), "%*ld ", g - 1 < 20 ? g - 1 : 20, view_.top() + r + 1);
            screen_.fill(0, y, g, 1, ' ', col_.gutter);
            screen_.text(0, y, num, col_.gutter, g);
        }
        if (r == caret)
            screen_.fill(g, y, cols, 1, ' ', base);
        if (ruler_x >= 0 && ruler_x < cols) {
            tu::Style rs = base;
            rs.bg = col_.ruler_bg;
            screen_.put(g + ruler_x, y, ' ', rs);
        }
        if (!have)
            continue;
        for (int x = 0; x < cols; x++) {
            const te::Cell& c = row[x];
            if (c.width == 0)
                continue;
            tu::Style cs;
            switch (c.kind) {
            case te::CELL_SELECTED:
            case te::CELL_BLOCK:
            case te::CELL_FOUND:
                cs = col_.select;
                break;
            case te::CELL_CONTROL:
                cs = col_.ctrl;
                cs.bg = base.bg;
                break;
            default:
                cs = c.syn > 0 && c.syn < te::SYN_COUNT ? col_.syntax[c.syn] : col_.text;
                if (c.ws)
                    cs.fg = col_.whitespace.fg;
                cs.bg = base.bg;
                if (x == ruler_x)
                    cs.bg = col_.ruler_bg;
                break;
            }
            if (c.cp == ' ' && c.kind == te::CELL_TEXT && x != ruler_x && r != caret)
                continue;               /* already blank */
            if (c.width == 2 || c.cp >= 0x80) {
                char b[5];
                b[utf8(c.cp, b)] = 0;
                screen_.text(g + x, y, b, cs, c.width == 2 ? 2 : 1);
            } else {
                screen_.put(g + x, y, c.cp, cs);
            }
        }
    }
    free(row);
    draw_key_bar();
    int cx, cy;
    bool shown = view_.screen_cursor(&cx, &cy);
    screen_.set_cursor(shown ? cx + g : 0, shown ? cy + 1 : 0, shown);
}

/* ---- the loop ---- */

bool Editor::wait_input(int timeout_ms, bool* timed_out)
{
    *timed_out = false;
    fd_set rd;
    FD_ZERO(&rd);
    int in = term_.in_fd(), sig = term_.signal_fd();
    int top = in;
    FD_SET(in, &rd);
    if (sig >= 0) {
        FD_SET(sig, &rd);
        if (sig > top)
            top = sig;
    }
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int n = select(top + 1, &rd, 0, 0, timeout_ms < 0 ? 0 : &tv);
    if (n < 0)
        return errno == EINTR;
    if (n == 0) {
        *timed_out = true;
        return true;
    }
    if (sig >= 0 && FD_ISSET(sig, &rd)) {
        char b[16];
        ssize_t r = read(sig, b, sizeof(b));
        for (ssize_t i = 0; i < r; i++) {
            if (b[i] == 'W')
                resize();
            if (b[i] == 'T' || b[i] == 'H')
                quit_ = true;
        }
    }
    if (FD_ISSET(in, &rd)) {
        char b[4096];
        ssize_t r = read(in, b, sizeof(b));
        if (r == 0 || (r < 0 && errno != EINTR && errno != EAGAIN))
            return false;
        if (r > 0)
            input_.feed(b, (size_t)r);
    }
    return true;
}

void Editor::resize()
{
    term_.update_size();
    screen_.resize();
}

int Editor::run()
{
    if (!term_.start())
        return 1;
    screen_.resize();
    apply_settings();
    while (!quit_) {
        draw();
        screen_.flush();
        bool timed_out = false;
        if (!wait_input(input_.pending() ? input_.timeout_ms() : -1, &timed_out))
            break;
        tu::Event ev;
        while (!quit_ && input_.next(&ev, timed_out))
            handle(ev);
    }
    term_.set_cursor_shape(tu::CURSOR_DEFAULT, true);
    term_.set_cursor_color(-1);
    term_.stop();
    return 0;
}

int Editor::modal(tu::Dialog& d, const tu::DialogStyle& st)
{
    while (!quit_ && d.result() == tu::Dialog::RESULT_NONE) {
        draw();
        d.draw(screen_, st);
        screen_.flush();
        bool timed_out = false;
        if (!wait_input(input_.pending() ? input_.timeout_ms() : 200, &timed_out)) {
            quit_ = true;
            break;
        }
        tu::Event ev;
        while (d.result() == tu::Dialog::RESULT_NONE && input_.next(&ev, timed_out))
            d.handle(ev);
    }
    return quit_ ? tu::Dialog::RESULT_CANCEL : d.result();
}

/* A question with buttons; returns the button's index from 1, or -1. */
int Editor::ask(const char* title, const char* text, const char* const* buttons, bool alarm)
{
    int lines = 1, longest = 0, cur = 0;
    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
            lines++;
            cur = 0;
        } else if (++cur > longest) {
            longest = cur;
        }
    }
    int nb = 0, bw = 0;
    while (buttons[nb]) {
        bw += tu::Button::width_of(buttons[nb]) + 1;
        nb++;
    }
    int w = longest + 8;
    if (w < bw + 6)
        w = bw + 6;
    if (w < 30)
        w = 30;
    if (w > screen_.cols() - 4)
        w = screen_.cols() - 4;
    tu::Dialog d(title, w, lines + 5);
    d.add(new tu::Label(text), 3, 2, w - 6, lines);
    tu::Button* b[8];
    for (int i = 0; i < nb && i < 8; i++)
        b[i] = new tu::Button(buttons[i], i + 1, i == 0);
    d.add_buttons(lines + 3, b, nb < 8 ? nb : 8);
    int r = modal(d, alarm ? theme_.alarm : theme_.dialog);
    return r > 0 ? r : -1;
}

void Editor::show_error(const char* text)
{
    static const char* const ok[] = { "&OK", 0 };
    ask("Error", text, ok, true);
}

bool Editor::ask_text(const char* title, const char* label, ttr::Str* value, tu::StrList* hist)
{
    int w = screen_.cols() - 10;
    if (w > 70)
        w = 70;
    if (w < 30)
        w = 30;
    tu::Dialog d(title, w, 8);
    d.add(new tu::Label(label), 3, 2, w - 6);
    tu::InputLine* in = new tu::InputLine(value->c_str());
    in->set_history(hist);
    d.add(in, 3, 3, w - 6);
    tu::Button* b[2] = { new tu::Button("&OK", 1, true), new tu::Button("&Cancel", 2) };
    d.add_buttons(5, b, 2);
    d.focus(in);
    if (modal(d, theme_.dialog) != 1)
        return false;
    *value = in->text();
    if (hist && *in->text())
        hist->add(in->text());
    return true;
}

void Editor::note(const char* text)
{
    snprintf(note_, sizeof(note_), "%s", text);
}

/* ---- commands ---- */

void Editor::copy_selection()
{
    if (!view_.has_selection())
        return;
    free(clip_);
    clip_ = view_.copy(&clip_len_);
}

void Editor::paste()
{
    if (clip_ && clip_len_)
        view_.paste(clip_, clip_len_);
}

bool Editor::save(bool ask_name)
{
    if (ask_name || path_.empty()) {
        ttr::Str v(path_);
        if (!ask_text("Save as", "File name:", &v, 0) || v.empty())
            return false;
        path_ = v;
        syntax_.set_language(te::language_for(path_.c_str(), doc_.line(0), doc_.length(0)));
    }
    int err = te::save_file(doc_, path_.c_str(), 0);
    if (err) {
        char m[700];
        snprintf(m, sizeof(m), "Cannot save %.400s:\n%s", path_.c_str(), strerror(err));
        show_error(m);
        return false;
    }
    note("Saved.");
    return true;
}

void Editor::request_quit()
{
    if (!doc_.modified()) {
        quit_ = true;
        return;
    }
    static const char* const b[] = { "&Save", "&Discard", "&Cancel", 0 };
    int a = ask("Quit", "The file has changed. Save it?", b, false);
    if (a == 1) {
        if (save(false))
            quit_ = true;
    } else if (a == 2) {
        quit_ = true;
    }
}

void Editor::find(bool forward)
{
    if (find_text_.empty()) {
        dialog_find(false);
        return;
    }
    marking_ = false;
    bool wrapped = false;
    if (!view_.find(find_text_.c_str(), forward, match_case_, &wrapped, whole_word_)) {
        char m[300];
        snprintf(m, sizeof(m), "Not found: %.200s", find_text_.c_str());
        note(m);
    } else if (wrapped) {
        note("Search wrapped around.");
    }
}

/* F7 and F4: what to find, how, and for F4 what to put there. */
void Editor::dialog_find(bool replace)
{
    int w = screen_.cols() - 10;
    if (w > 66)
        w = 66;
    if (w < 40)
        w = 40;
    int h = replace ? 12 : 10;
    tu::Dialog d(replace ? "Replace" : "Search", w, h);
    d.add(new tu::Label("Find:"), 3, 2, w - 6);
    ttr::Str start(find_text_);
    if (view_.has_selection()) {
        size_t n;
        char* sel = view_.copy(&n);
        if (sel && !memchr(sel, '\n', n))
            start = sel;
        free(sel);
    }
    tu::InputLine* what = new tu::InputLine(start.c_str());
    what->set_history(&hist_find_);
    what->replace_on_type();
    d.add(what, 3, 3, w - 6);
    tu::InputLine* with = 0;
    int y = 4;
    if (replace) {
        d.add(new tu::Label("Replace with:"), 3, 4, w - 6);
        with = new tu::InputLine("");
        with->set_history(&hist_replace_);
        d.add(with, 3, 5, w - 6);
        y = 6;
    }
    tu::CheckBox* mc = new tu::CheckBox("&Match case", match_case_);
    tu::CheckBox* ww = new tu::CheckBox("&Whole words", whole_word_);
    d.add(mc, 3, y + 1, 20);
    d.add(ww, 24, y + 1, 20);
    tu::CheckBox* back = 0;
    if (!replace) {
        back = new tu::CheckBox("&Backwards", backwards_);
        d.add(back, 3, y + 2, 20);
    }
    tu::Button* b[2] = { new tu::Button(replace ? "Replace &all" : "&Find", 1, true),
                         new tu::Button("&Cancel", 2) };
    d.add_buttons(h - 3, b, 2);
    d.focus(what);
    if (modal(d, theme_.dialog) != 1 || !*what->text())
        return;
    find_text_ = what->text();
    hist_find_.add(what->text());
    match_case_ = mc->on;
    whole_word_ = ww->on;
    if (back)
        backwards_ = back->on;
    if (replace) {
        hist_replace_.add(with->text());
        int n = view_.replace_all(find_text_.c_str(), with->text(), match_case_, whole_word_);
        char m[100];
        sprintf(m, "%d replaced.", n);
        note(m);
        return;
    }
    find(!backwards_);
}

void Editor::dialog_goto()
{
    ttr::Str v("");
    if (ask_text("Go to line", "Line number:", &v, 0) && atol(v.c_str()) > 0) {
        marking_ = false;
        view_.goto_line(atol(v.c_str()));
    }
}

void Editor::dialog_help()
{
    static const char* const ok[] = { "&OK", 0 };
    ask("Keys",
        "F2 save, Shift-F2 save as, F10 or Esc quit, F9 menu\n"
        "F3 mark a block (or Shift+arrows), F5 copy it here, F6 move it,\n"
        "F8 delete it, Tab / Shift-Tab indent it\n"
        "F7 search, Shift-F7 again, F4 replace, Alt-L go to line\n"
        "Ctrl-U undo, Alt-R redo, Ctrl-Y delete line\n"
        "Ctrl+arrows: words; Ctrl-Ins, Shift-Del, Shift-Ins: copy, cut, paste",
        ok, false);
}

/* F9 > Options > Editor. */
void Editor::dialog_options()
{
    int w = 64;
    if (w > screen_.cols() - 2)
        w = screen_.cols() - 2;
    tu::Dialog d("Editor options", w, 21);
    char buf[32];
    d.add(new tu::Label("Tab width:"), 3, 2, 14);
    sprintf(buf, "%d", set_.tab_width);
    tu::InputLine* tab = new tu::InputLine(buf);
    d.add(tab, 18, 2, 5);
    d.add(new tu::Label("Indent by:"), 26, 2, 12);
    sprintf(buf, "%d", set_.indent_width);
    tu::InputLine* ind = new tu::InputLine(buf);
    d.add(ind, 38, 2, 5);
    tu::CheckBox* spaces = new tu::CheckBox("Tab inserts &spaces", set_.insert_spaces);
    tu::CheckBox* autoi = new tu::CheckBox("&Keep the indentation of the line above",
                                           set_.auto_indent);
    tu::CheckBox* nums = new tu::CheckBox("&Line numbers", set_.line_numbers);
    tu::CheckBox* line = new tu::CheckBox("The &current line", set_.highlight_line);
    tu::CheckBox* ws = new tu::CheckBox("Spaces and &tabs", set_.show_whitespace);
    d.add(spaces, 3, 3, w - 6);
    d.add(autoi, 3, 4, w - 6);
    d.add(new tu::Label("Show:"), 3, 6, 10);
    d.add(nums, 10, 6, 20);
    d.add(line, 10, 7, 24);
    d.add(ws, 10, 8, 24);
    d.add(new tu::Label("Column marker (0: none):"), 3, 10, 26);
    sprintf(buf, "%d", set_.ruler);
    tu::InputLine* ruler = new tu::InputLine(buf);
    d.add(ruler, 29, 10, 6);
    d.add(new tu::Label("Cursor:"), 3, 12, 10);
    tu::RadioGroup* shape = new tu::RadioGroup();
    for (int i = 0; k_shapes[i]; i++) {
        shape->add(k_shape_labels[i]);
        if (strcmp(set_.cursor_shape.c_str(), k_shapes[i]) == 0)
            shape->selected = i;
    }
    d.add(shape, 12, 12, 16, 4);
    tu::CheckBox* blink = new tu::CheckBox("&Blink", set_.cursor_blink);
    d.add(blink, 30, 12, 12);
    d.add(new tu::Label("Color #rrggbb:"), 30, 13, 15);
    tu::InputLine* ccol = new tu::InputLine(set_.cursor_color.c_str());
    d.add(ccol, 45, 13, 10);
    /* Colours: the theme (tu's) and a text scheme (th's). */
    d.add(new tu::Label("Colors:"), 3, 17, 10);
    th::List schemes;
    th::list_schemes(&schemes);
    tu::ListBox* list = new tu::ListBox();
    list->items.add("The theme's");
    list->current = 0;
    for (int i = 0; i < schemes.count(); i++) {
        list->items.add(schemes[i].name);
        if (th::compare_names(schemes[i].name, set_.scheme.c_str()) == 0)
            list->current = i + 1;
    }
    d.add(list, 12, 17, 22, 2);
    tu::RadioGroup* theme = new tu::RadioGroup();
    const char* const* names = tu::theme_names();
    theme->add("By terminal");
    for (int i = 0; names[i]; i++) {
        theme->add(names[i]);
        if (strcmp(set_.theme.c_str(), names[i]) == 0)
            theme->selected = i + 1;
    }
    d.add(new tu::Label("Theme:"), 36, 15, 8);
    d.add(theme, 44, 15, 16, 5);
    tu::Button* b[2] = { new tu::Button("&OK", 1, true), new tu::Button("&Cancel", 2) };
    d.add_buttons(19, b, 2);
    d.focus(tab);
    if (modal(d, theme_.dialog) != 1)
        return;
    int v = atoi(tab->text());
    set_.tab_width = v >= 1 && v <= 32 ? v : set_.tab_width;
    v = atoi(ind->text());
    set_.indent_width = v >= 0 && v <= 32 ? v : set_.indent_width;
    set_.insert_spaces = spaces->on;
    set_.auto_indent = autoi->on;
    set_.line_numbers = nums->on;
    set_.highlight_line = line->on;
    set_.show_whitespace = ws->on;
    v = atoi(ruler->text());
    set_.ruler = v >= 0 && v <= 1000 ? v : set_.ruler;
    set_.cursor_shape = k_shapes[shape->selected >= 0 && shape->selected < 4 ? shape->selected : 0];
    set_.cursor_blink = blink->on;
    th::Color c;
    set_.cursor_color = *ccol->text() && th::parse_hex(ccol->text(), &c) ? ccol->text() : "";
    set_.scheme = list->current > 0 && list->current <= schemes.count()
                      ? schemes[list->current - 1].name : "";
    set_.theme = theme->selected > 0 ? names[theme->selected - 1] : "";
    apply_settings();
    if (!set_.save())
        note("Could not save the settings.");
}

void Editor::open_menu(int which)
{
    tu::MenuBar m;
    m.add("File", k_file);
    m.add("Edit", k_edit);
    m.add("Search", k_search);
    m.add("Options", k_options);
    m.open(which);
    while (!quit_ && !m.done()) {
        draw();
        m.draw(screen_, theme_.menu, theme_.menu_bar, theme_.menu_hotkey);
        screen_.flush();
        bool timed_out = false;
        if (!wait_input(input_.pending() ? input_.timeout_ms() : 200, &timed_out)) {
            quit_ = true;
            return;
        }
        tu::Event ev;
        while (!m.done() && input_.next(&ev, timed_out))
            m.handle(ev);
    }
    if (m.selected() > 0)
        command(m.selected());
}

void Editor::command(int id)
{
    switch (id) {
    case M_SAVE: save(false); break;
    case M_SAVE_AS: save(true); break;
    case M_QUIT: request_quit(); break;
    case M_UNDO:
        if (!view_.undo())
            note("Nothing to undo.");
        break;
    case M_REDO:
        if (!view_.redo())
            note("Nothing to redo.");
        break;
    case M_MARK: function_key(3, false); break;
    case M_COPY: function_key(5, false); break;
    case M_MOVE: function_key(6, false); break;
    case M_DELETE_BLOCK: function_key(8, false); break;
    case M_DELETE_LINE: view_.delete_line(); break;
    case M_SELECT_ALL: view_.select_all(); break;
    case M_FIND: dialog_find(false); break;
    case M_FIND_NEXT: find(!backwards_); break;
    case M_FIND_PREV: find(backwards_); break;
    case M_REPLACE: dialog_find(true); break;
    case M_GOTO: dialog_goto(); break;
    case M_OPTIONS: dialog_options(); break;
    case M_HELP: dialog_help(); break;
    default: break;
    }
}

void Editor::function_key(int n, bool shift)
{
    switch (n) {
    case 1: dialog_help(); break;
    case 2: save(shift); break;
    case 3:
        /* Start a block here; the next F3 (or a Shift selection) marks it. */
        if (marking_ || view_.has_selection()) {
            marking_ = false;
            view_.mark_block();
        } else {
            view_.clear_block();
            view_.set_anchor(view_.cursor());
            marking_ = true;
        }
        break;
    case 4: dialog_find(true); break;
    case 5: case 6:
        if (marking_ || view_.has_selection()) {
            marking_ = false;
            view_.mark_block();
        }
        if (!view_.has_block())
            note("No block: F3 marks one.");
        else if (!(n == 5 ? view_.copy_block() : view_.move_block()))
            note("The cursor is inside the block.");
        break;
    case 7:
        if (shift)
            find(!backwards_);
        else
            dialog_find(false);
        break;
    case 8:
        marking_ = false;
        if (!view_.erase_block() && view_.has_selection())
            view_.del();
        break;
    case 9: open_menu(0); break;
    case 10: request_quit(); break;
    case 12: save(true); break;
    default: break;
    }
}

void Editor::mouse(const tu::Event& ev)
{
    int g = gutter();
    if (ev.action == tu::MOUSE_WHEEL_UP || ev.action == tu::MOUSE_WHEEL_DOWN) {
        view_.scroll(ev.action == tu::MOUSE_WHEEL_UP ? -3 : 3);
        /* The cursor stays on screen, so drawing does not jump back. */
        te::Pos p = view_.cursor();
        long top = view_.top(), last = top + view_.rows() - 1;
        if (p.line < top || p.line > last)
            view_.set_cursor(view_.pos_at(p.line < top ? top : last, view_.column_of(p)),
                             marking_);
        return;
    }
    if (ev.y == 0 && ev.action == tu::MOUSE_PRESS) {
        open_menu(0);
        return;
    }
    if (ev.y == screen_.rows() - 1 && ev.action == tu::MOUSE_PRESS) {
        int slot = screen_.cols() / 10;
        if (slot > 0)
            function_key(ev.x / slot + 1, false);
        return;
    }
    if (ev.button != 1 || ev.y < 1 || ev.y > view_.rows())
        return;
    int x = ev.x - g + view_.left();
    te::Pos p = view_.pos_at(view_.top() + ev.y - 1, x < 0 ? 0 : x);
    if (ev.action == tu::MOUSE_PRESS) {
        long t = now_ms();
        click_count_ = t - last_click_ < 400 ? click_count_ + 1 : 1;
        last_click_ = t;
        if (click_count_ == 2)
            view_.select_word(p);
        else
            view_.set_cursor(p, (ev.mods & tu::MOD_SHIFT) || marking_);
        dragging_ = true;
    } else if (ev.action == tu::MOUSE_DRAG && dragging_) {
        view_.set_cursor(p, true);
    } else if (ev.action == tu::MOUSE_RELEASE) {
        dragging_ = false;
    }
}

void Editor::handle(const tu::Event& ev)
{
    note_[0] = 0;
    bool shift = (ev.mods & tu::MOD_SHIFT) != 0;
    bool ctrl = (ev.mods & tu::MOD_CTRL) != 0;
    bool extend = shift || marking_;
    switch (ev.key) {
    case tu::KEY_MOUSE:
        mouse(ev);
        break;
    case tu::KEY_PASTE_BEGIN:
        view_.auto_indent = false;
        break;
    case tu::KEY_PASTE_END:
        view_.auto_indent = set_.auto_indent;
        break;
    case tu::KEY_LEFT:
        view_.move(ctrl ? te::MOVE_WORD_LEFT : te::MOVE_LEFT, extend);
        break;
    case tu::KEY_RIGHT:
        view_.move(ctrl ? te::MOVE_WORD_RIGHT : te::MOVE_RIGHT, extend);
        break;
    case tu::KEY_UP:
        view_.move(te::MOVE_UP, extend);
        break;
    case tu::KEY_DOWN:
        view_.move(te::MOVE_DOWN, extend);
        break;
    case tu::KEY_PGUP:
        view_.move(ctrl ? te::MOVE_DOC_START : te::MOVE_PAGE_UP, extend);
        break;
    case tu::KEY_PGDN:
        view_.move(ctrl ? te::MOVE_DOC_END : te::MOVE_PAGE_DOWN, extend);
        break;
    case tu::KEY_HOME:
        view_.move(ctrl ? te::MOVE_DOC_START : te::MOVE_HOME, extend);
        break;
    case tu::KEY_END:
        view_.move(ctrl ? te::MOVE_DOC_END : te::MOVE_END, extend);
        break;
    case tu::KEY_ENTER:
        view_.newline();
        break;
    case tu::KEY_TAB:
        view_.tab(shift);
        break;
    case tu::KEY_BACKSPACE:
        view_.backspace();
        break;
    case tu::KEY_DELETE:
        if (shift && view_.has_selection()) {
            free(clip_);
            clip_ = view_.cut(&clip_len_);
        } else {
            view_.del();
        }
        break;
    case tu::KEY_INSERT:
        if (shift)
            paste();
        else if (ctrl)
            copy_selection();
        break;
    case tu::KEY_ESC:
        if (marking_ || view_.has_selection() || view_.has_block()) {
            marking_ = false;
            view_.clear_selection();
            view_.clear_block();
        } else {
            request_quit();
        }
        break;
    case tu::KEY_CHAR:
        if (ctrl) {
            switch (ev.ch) {
            case 'u': case 'z': command(M_UNDO); break;
            case 'y': view_.delete_line(); break;
            case 's': save(false); break;
            case 'l': dialog_goto(); break;
            default: break;
            }
        } else if (ev.mods & tu::MOD_ALT) {
            if (ev.ch == 'l' || ev.ch == 'L')
                dialog_goto();
            else if (ev.ch == 'r' || ev.ch == 'R')
                command(M_REDO);
        } else {
            char b[4];
            view_.type(b, utf8(ev.ch, b));
        }
        break;
    default:
        if (ev.key >= tu::KEY_F1 && ev.key <= tu::KEY_F1 + 11)
            function_key(ev.key - tu::KEY_F1 + 1, shift);
        break;
    }
    view_.follow();
}

} /* namespace ttre */
