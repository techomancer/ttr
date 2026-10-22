#include "shell.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

namespace ttrc {

namespace {

long now_ms()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* IRIX's tty takes at most 256 bytes of canonical input per line. */
const size_t k_max_line = 250;

tu::Color map_color(uint32_t c)
{
    switch (tl::color_kind(c)) {
    case tl::COLOR_PALETTE: return tu::color_index(tl::color_index(c));
    case tl::COLOR_RGB: return c & 0xffffff;
    default: return tu::COLOR_DEFAULT;
    }
}

tu::Style map_style(const tl::Style& s)
{
    tu::Style t(map_color(s.fg), map_color(s.bg), 0);
    if (s.attrs & tl::ATTR_BOLD)
        t.attrs |= tu::ATTR_BOLD;
    if (s.attrs & tl::ATTR_FAINT)
        t.attrs |= tu::ATTR_DIM;
    if (s.attrs & tl::ATTR_ITALIC)
        t.attrs |= tu::ATTR_ITALIC;
    if (s.attrs & tl::ATTR_BLINK)
        t.attrs |= tu::ATTR_BLINK;
    if (s.attrs & tl::ATTR_INVERSE)
        t.attrs |= tu::ATTR_REVERSE;
    if (s.underline() != tl::UL_NONE)
        t.attrs |= tu::ATTR_UNDERLINE;
    return t;
}

int encode(uint32_t cp, char* b)
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

} /* namespace */

Shell::Shell()
    : alive_(false), answering_(true), job_control_(false), last_output_(0), prompt_x_(0),
      prompt_y_(-1), family_(FAMILY_SH)
{
    name_[0] = 0;
    error_[0] = 0;
}

Shell::~Shell()
{
    if (alive_) {
        proc_.signal_group(SIGHUP);
        pty_.close();
        proc_.wait(500);
    }
}

bool Shell::start(int cols, int rows, const char* cwd, bool login, bool utf8)
{
    const char* sh = getenv("SHELL");
    if (!sh || !*sh)
        sh = "/bin/sh";
    const char* base = strrchr(sh, '/');
    base = base ? base + 1 : sh;
    if (*base == '-')
        base++;
    strncpy(name_, base, sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = 0;
    if (strcmp(name_, "csh") == 0 || strcmp(name_, "tcsh") == 0)
        family_ = FAMILY_CSH;
    else if (strcmp(name_, "fish") == 0)
        family_ = FAMILY_FISH;
    else
        family_ = FAMILY_SH;

    tp::WinSize ws;
    ws.cols = cols;
    ws.rows = rows;
    ws.width_px = ws.height_px = 0;
    if (!pty_.open(ws)) {
        sprintf(error_, "pty: %.200s", pty_.error());
        return false;
    }
    copy_.init(cols, rows, 1000);
    copy_.set_host(this);
    copy_.set_utf8(utf8);
    /* The shell is started as it is, with nothing of ours in it but
     * TTRC=1 (so scripts can tell) and no stale size variables. */
    static const char* const env[] = { "TTRC=1", "COLUMNS", "LINES", 0 };
    const char* argv[2] = { sh, 0 };
    tp::SpawnOptions o;
    o.argv = argv;
    o.login = login;
    o.env = env;
    o.cwd = cwd;
    o.utf8 = utf8;
    if (!proc_.spawn(pty_, o)) {
        sprintf(error_, "%.200s", proc_.error());
        pty_.close();
        return false;
    }
    alive_ = true;
    last_output_ = now_ms();
    return true;
}

bool Shell::read(char* out, size_t cap, size_t* n)
{
    *n = 0;
    if (!alive_)
        return false;
    long r = pty_.read(out, cap);
    if (r < 0) {
        alive_ = false;
        proc_.wait(200);
        return false;
    }
    if (r > 0) {
        *n = (size_t)r;
        copy_.feed((const uint8_t*)out, (size_t)r);
        last_output_ = now_ms();
    }
    return true;
}

void Shell::write(const char* data, size_t n)
{
    size_t off = 0;
    int tries = 0;
    while (alive_ && off < n && tries < 200) {
        long w = pty_.write(data + off, n - off);
        if (w < 0)
            break;
        if (w == 0) {
            usleep(5000);
            tries++;
            continue;
        }
        off += (size_t)w;
    }
}

void Shell::write(const char* s)
{
    write(s, strlen(s));
}

void Shell::resize(int cols, int rows)
{
    tp::WinSize ws;
    ws.cols = cols;
    ws.rows = rows;
    ws.width_px = ws.height_px = 0;
    copy_.resize(cols, rows);
    pty_.resize(ws);
}

void Shell::write_pty(const uint8_t* data, size_t n)
{
    if (answering_)
        write((const char*)data, n);
}

bool Shell::idle(long now)
{
    if (!alive_ || now - last_output_ < 100)
        return false;
    int fg = pty_.foreground_group();
    if (fg > 0 && fg != proc_.pid()) {
        job_control_ = true;
        return false;
    }
    /* Without job control the command stays in the shell's own group:
     * look for its processes instead. */
    if (!job_control_ && tp::process_has_children(proc_.pid()) > 0)
        return false;
    return true;
}

bool Shell::cwd(char* buf, size_t n) const
{
    return alive_ && tp::process_cwd(proc_.pid(), buf, n);
}

bool Shell::quote(const char* p, char* out, size_t n) const
{
    size_t o = 0;
    if (n < 4)
        return false;
    out[o++] = '\'';
    for (const char* c = p; *c; c++) {
        if (o + 8 >= n)
            return false;
        if (*c == '\n' && family_ != FAMILY_SH)
            return false;
        if (*c == '\'') {
            if (family_ == FAMILY_FISH) {
                out[o++] = '\\';
                out[o++] = '\'';
            } else {
                memcpy(out + o, "'\\''", 4);
                o += 4;
            }
        } else if (*c == '\\' && family_ == FAMILY_FISH) {
            out[o++] = '\\';
            out[o++] = '\\';
        } else if (*c == '!' && family_ == FAMILY_CSH) {
            /* csh expands history even inside single quotes */
            out[o++] = '\\';
            out[o++] = '!';
        } else {
            out[o++] = *c;
        }
    }
    out[o++] = '\'';
    out[o] = 0;
    return true;
}

bool Shell::cd_command(const char* path, char* out, size_t n) const
{
    char q[1024];
    if (!quote(path, q, sizeof(q)))
        return false;
    /* A leading space keeps it out of the history where the shell is set
     * up that way; nothing in the shell is changed for it. */
    if (strlen(q) + 6 > k_max_line || strlen(q) + 6 > n)
        return false;
    sprintf(out, " cd %s\r", q);
    return true;
}

ttr::Str Shell::prompt()
{
    const tl::Cursor& c = copy_.cursor();
    tl::Buffer b;
    if (c.x > 0)
        copy_.append_cells(copy_.grid().row(c.y), 0, c.x - 1, b, false);
    return ttr::Str(b.c_str());
}

void Shell::mark_prompt()
{
    prompt_x_ = copy_.cursor().x;
    prompt_y_ = copy_.cursor().y;
}

ttr::Str Shell::typed()
{
    if (prompt_y_ < 0 || prompt_y_ >= copy_.rows() || copy_.cursor().y != prompt_y_)
        return ttr::Str("");
    tl::Buffer b;
    copy_.append_cells(copy_.grid().row(prompt_y_), prompt_x_, copy_.cols() - 1, b, true);
    return ttr::Str(b.c_str());
}

void Shell::paint(tu::Screen& s, int from, int y, int n)
{
    const tl::Grid& g = copy_.grid();
    int cols = copy_.cols() < s.cols() ? copy_.cols() : s.cols();
    for (int r = 0; r < n; r++) {
        int row = from + r;
        if (row < 0 || row >= copy_.rows() || y + r >= s.rows())
            continue;
        const tl::Row& rw = g.row(row);
        for (int x = 0; x < cols; x++) {
            const tl::Cell& c = rw.cells[x];
            tu::Style st = map_style(copy_.style(c));
            if (copy_.style(c).attrs & tl::ATTR_INVISIBLE) {
                s.put(x, y + r, ' ', st);
                continue;
            }
            tl::WideKind wk = c.wide();
            if (wk == tl::CELL_SPACER_TAIL)
                continue;
            uint32_t cps[4];
            int k = copy_.cell_text(c, cps, 4);
            uint32_t cp = k > 0 ? cps[0] : ' ';
            if (cp == 0 || wk == tl::CELL_SPACER_HEAD)
                cp = ' ';
            char b[8];
            int len = encode(cp, b);
            b[len] = 0;
            s.fill(x, y + r, 1, 1, ' ', st);
            s.text(x, y + r, b, st, wk == tl::CELL_WIDE ? 2 : 1);
        }
        for (int x = cols; x < s.cols(); x++)
            s.put(x, y + r, ' ', tu::Style());
    }
}

/* ------------------------------------------------------------------ */
/* Pass-through                                                        */

PassThroughView::PassThroughView(Shell& sh, tu::Term& term, tu::Screen& screen)
    : sh_(sh), term_(term), screen_(screen), shown_(false)
{
}

void PassThroughView::show()
{
    tl::Terminal& c = sh_.copy();
    bool alt = c.alt_screen();
    /* The panels live on the alternate screen and the shell on the
     * normal one, unless a program in the shell is using the alternate
     * screen itself. */
    term_.suspend_ui(!alt);
    screen_.invalidate();
    screen_.clear(tu::Style());
    sh_.paint(screen_, 0, 0, c.rows());
    screen_.set_cursor(c.cursor().x, c.cursor().y, c.modes().cursor_visible);
    screen_.flush();
    apply_modes();
    shown_ = true;
    sh_.set_answering(false);
}

/* Give the terminal the modes the program in the shell had set. */
void PassThroughView::apply_modes()
{
    const tl::Modes& m = sh_.copy().modes();
    term_.set_style(map_style(sh_.copy().cursor().style));
    char b[160];
    size_t o = 0;
    if (term_.xterm_like()) {
        o += (size_t)sprintf(b + o, "\033[?1%c", m.app_cursor ? 'h' : 'l');
        o += (size_t)sprintf(b + o, "\033%c", m.app_keypad ? '=' : '>');
        if (m.bracketed_paste)
            o += (size_t)sprintf(b + o, "\033[?2004h");
        if (m.focus_events)
            o += (size_t)sprintf(b + o, "\033[?1004h");
        if (m.mouse)
            o += (size_t)sprintf(b + o, "\033[?%dh", m.mouse);
        if (m.mouse_encoding)
            o += (size_t)sprintf(b + o, "\033[?%dh", m.mouse_encoding);
    }
    if (!m.cursor_visible)
        o += (size_t)sprintf(b + o, "\033[?25l");
    term_.flush();
    if (o)
        term_.write_raw(b, o);
}

void PassThroughView::hide()
{
    shown_ = false;
    sh_.set_answering(true);
    if (term_.xterm_like()) {
        /* Undo what the program had on that ttrc does not reset. */
        const char* off = "\033[?1004l\033[?1003l\033[?1002l\033[?1015l\033[?1005l";
        term_.write_raw(off, strlen(off));
    }
    term_.resume_ui(!sh_.copy().alt_screen());
    screen_.invalidate();
}

void PassThroughView::output(const char* data, size_t n)
{
    if (shown_)
        term_.write_raw(data, n);
}

} /* namespace ttrc */
