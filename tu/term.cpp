#include "term.h"
#include "tparm.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace tu {

namespace {

int g_sig_pipe[2] = { -1, -1 };

void on_signal(int sig)
{
    int saved = errno;
    char c = sig == SIGWINCH ? 'W' : sig == SIGTERM ? 'T' : 'H';
    if (g_sig_pipe[1] >= 0) {
        ssize_t r = write(g_sig_pipe[1], &c, 1);
        (void)r;
    }
    errno = saved;
}

void install_signals()
{
    if (g_sig_pipe[0] >= 0)
        return;
    if (pipe(g_sig_pipe) != 0)
        return;
    fcntl(g_sig_pipe[0], F_SETFL, fcntl(g_sig_pipe[0], F_GETFL) | O_NONBLOCK);
    fcntl(g_sig_pipe[1], F_SETFL, fcntl(g_sig_pipe[1], F_GETFL) | O_NONBLOCK);
    fcntl(g_sig_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(g_sig_pipe[1], F_SETFD, FD_CLOEXEC);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGHUP, &sa, 0);
}

bool name_starts(const char* name, const char* prefix)
{
    return strncmp(name, prefix, strlen(prefix)) == 0;
}

/* Capabilities known terminals have but their entries leave out. Only
 * added where the entry is silent. */
struct Gap {
    const char* prefix;
    const char* caps;
};

const Gap k_gaps[] = {
    /* xwsh and winterm: ANSI colours, which the iris-ansi entries of IRIX
     * 6.5 do not declare. Not line drawing: xwsh takes it from the
     * -dec-dectech fonts, which a stock IRIX X server does not have
     * (checked on 6.5.22), so frames there are ASCII. */
    { "iris-ansi", "colors#8, setaf=\\E[3%p1%dm, setab=\\E[4%p1%dm," },
    /* ttrt: the built-in entry if the installed one is missing pieces. */
    { "ttrt", "RGB, colors#256," },
    { 0, 0 }
};

/* The 16 ANSI colours as xterm shows them. */
const Color k_ansi16[16] = {
    0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd, 0x00cdcd, 0xe5e5e5,
    0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff, 0x00ffff, 0xffffff
};

long dist2(Color a, Color b)
{
    long dr = (long)((a >> 16) & 255) - (long)((b >> 16) & 255);
    long dg = (long)((a >> 8) & 255) - (long)((b >> 8) & 255);
    long db = (long)(a & 255) - (long)(b & 255);
    /* Weighted toward green, as the eye is. */
    return dr * dr * 3 + dg * dg * 4 + db * db * 2;
}

int nearest16(Color c, int n)
{
    int best = 0;
    long bd = -1;
    for (int i = 0; i < n; i++) {
        long d = dist2(c, k_ansi16[i]);
        if (bd < 0 || d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

const int k_cube[6] = { 0, 95, 135, 175, 215, 255 };

int cube_index(int v)
{
    int best = 0;
    for (int i = 1; i < 6; i++)
        if (abs(k_cube[i] - v) < abs(k_cube[best] - v))
            best = i;
    return best;
}

int nearest256(Color c)
{
    int r = (int)((c >> 16) & 255), g = (int)((c >> 8) & 255), b = (int)(c & 255);
    int ri = cube_index(r), gi = cube_index(g), bi = cube_index(b);
    Color cube = ((Color)k_cube[ri] << 16) | ((Color)k_cube[gi] << 8) | (Color)k_cube[bi];
    int idx = 16 + 36 * ri + 6 * gi + bi;
    int avg = (r + g + b) / 3;
    int gs = avg < 8 ? 0 : avg > 238 ? 23 : (avg - 8) / 10;
    int gv = 8 + gs * 10;
    Color grey = ((Color)gv << 16) | ((Color)gv << 8) | (Color)gv;
    if (dist2(c, grey) < dist2(c, cube)) {
        idx = 232 + gs;
        cube = grey;
    }
    int a = nearest16(c, 16);
    if (dist2(c, k_ansi16[a]) <= dist2(c, cube))
        return a;
    return idx;
}

/* Unicode line and block characters and their DEC ACS letters. Double,
 * heavy and rounded lines fall back to the single ones. */
struct AcsMap {
    uint32_t cp;
    char acs;
    char ascii;
};

const AcsMap k_acs[] = {
    { 0x2500, 'q', '-' }, { 0x2501, 'q', '-' }, { 0x2550, 'q', '=' },
    { 0x2502, 'x', '|' }, { 0x2503, 'x', '|' }, { 0x2551, 'x', '|' },
    { 0x250c, 'l', '+' }, { 0x250f, 'l', '+' }, { 0x2554, 'l', '+' }, { 0x256d, 'l', '+' },
    { 0x2510, 'k', '+' }, { 0x2513, 'k', '+' }, { 0x2557, 'k', '+' }, { 0x256e, 'k', '+' },
    { 0x2514, 'm', '+' }, { 0x2517, 'm', '+' }, { 0x255a, 'm', '+' }, { 0x2570, 'm', '+' },
    { 0x2518, 'j', '+' }, { 0x251b, 'j', '+' }, { 0x255d, 'j', '+' }, { 0x256f, 'j', '+' },
    { 0x251c, 't', '+' }, { 0x2523, 't', '+' }, { 0x2560, 't', '+' }, { 0x255f, 't', '+' },
    { 0x2524, 'u', '+' }, { 0x252b, 'u', '+' }, { 0x2563, 'u', '+' }, { 0x2562, 'u', '+' },
    { 0x2534, 'v', '+' }, { 0x253b, 'v', '+' }, { 0x2569, 'v', '+' }, { 0x2567, 'v', '+' },
    { 0x252c, 'w', '+' }, { 0x2533, 'w', '+' }, { 0x2566, 'w', '+' }, { 0x2564, 'w', '+' },
    { 0x2520, 't', '+' }, { 0x2528, 'u', '+' }, { 0x252f, 'w', '+' }, { 0x2537, 'v', '+' },
    { 0x253c, 'n', '+' }, { 0x254b, 'n', '+' }, { 0x256c, 'n', '+' },
    { 0x2592, 'a', '#' }, { 0x2591, 'a', '#' }, { 0x2593, 'a', '#' },
    { 0x2588, '0', '#' }, { 0x25c6, '`', '+' }, { 0x00b7, '~', '.' },
    { 0x2190, ',', '<' }, { 0x2192, '+', '>' }, { 0x2191, '-', '^' }, { 0x2193, '.', 'v' },
    { 0x25b2, '-', '^' }, { 0x25bc, '.', 'v' }, { 0x25c0, ',', '<' }, { 0x25b6, '+', '>' },
    { 0, 0, 0 }
};

const AcsMap* acs_for(uint32_t cp)
{
    if (cp < 0x2190 && cp != 0x00b7)
        return 0;
    for (int i = 0; k_acs[i].cp; i++)
        if (k_acs[i].cp == cp)
            return &k_acs[i];
    return 0;
}

} /* namespace */

Term::Term()
    : in_fd_(-1), out_fd_(-1), have_saved_(false), started_(false), cols_(80), rows_(24),
      tier_(TIER_BASIC), colors_(0), utf8_(false), am_(false), xenl_(false), bce_(false),
      xterm_like_(false), truecolor_(false), acs_on_(false), cx_(-1), cy_(-1),
      pen_known_(false), cursor_shown_(true), buf_(0), len_(0), cap_(0)
{
    memset(acs_, 0, sizeof(acs_));
}

Term::~Term()
{
    if (started_)
        stop();
    free(buf_);
}

bool Term::open(int in_fd, int out_fd, const char* term)
{
    in_fd_ = in_fd;
    out_fd_ = out_fd;
    if (!term)
        term = getenv("TERM");
    if (!term || !*term)
        term = "vt100";
    if (!ti_.load(term) && !ti_.load_builtin("vt100"))
        return false;
    supplement();
    am_ = ti_.flag("am");
    xenl_ = ti_.flag("xenl");
    bce_ = ti_.flag("bce");
    xterm_like_ = name_starts(term, "xterm") || name_starts(term, "ttrt") ||
                  name_starts(term, "rxvt") || name_starts(term, "screen") ||
                  name_starts(term, "tmux") || name_starts(term, "kitty") ||
                  name_starts(term, "foot") || name_starts(term, "alacritty") ||
                  name_starts(term, "wezterm") || name_starts(term, "ghostty") ||
                  ti_.str("kmous") != 0;

    /* The alternate character set, as pairs of (vt100 letter, terminal's
     * character). */
    const char* acsc = ti_.str("acsc");
    if (acsc && ti_.str("smacs"))
        for (const char* p = acsc; p[0] && p[1]; p += 2)
            if ((unsigned char)p[0] < 128)
                acs_[(unsigned char)p[0]] = p[1];

    /* UTF-8 from the locale. */
    const char* lc = getenv("LC_ALL");
    if (!lc || !*lc)
        lc = getenv("LC_CTYPE");
    if (!lc || !*lc)
        lc = getenv("LANG");
    utf8_ = lc && (strstr(lc, "UTF-8") || strstr(lc, "utf8") || strstr(lc, "UTF8") ||
                   strstr(lc, "utf-8"));
    decide_tier();

    install_signals();
    update_size();
    return true;
}

void Term::supplement()
{
    for (int i = 0; k_gaps[i].prefix; i++)
        if (name_starts(ti_.name(), k_gaps[i].prefix))
            ti_.supplement(k_gaps[i].caps);
}

void Term::decide_tier()
{
    colors_ = ti_.num("colors");
    const char* ct = getenv("COLORTERM");
    truecolor_ = ti_.flag("RGB") || ti_.flag("Tc") || colors_ >= 0x1000000 ||
                 (ct && (strcmp(ct, "truecolor") == 0 || strcmp(ct, "24bit") == 0));
    if (truecolor_ && utf8_)
        tier_ = TIER_MODERN;
    else if (colors_ >= 256 || truecolor_)
        tier_ = TIER_256;
    else if (colors_ >= 8 && ti_.str("setaf"))
        tier_ = TIER_ANSI;
    else
        tier_ = TIER_BASIC;
    const char* force = getenv("TU_TIER");
    if (force) {
        if (strcmp(force, "basic") == 0)
            set_tier(TIER_BASIC);
        else if (strcmp(force, "ansi") == 0)
            set_tier(TIER_ANSI);
        else if (strcmp(force, "256") == 0)
            set_tier(TIER_256);
        else if (strcmp(force, "modern") == 0)
            set_tier(TIER_MODERN);
    }
}

void Term::set_tier(Tier t)
{
    tier_ = t;
    if (t == TIER_MODERN)
        truecolor_ = true;
    if (t == TIER_ANSI && colors_ < 8)
        colors_ = 8;
    if (t == TIER_256 && colors_ < 256)
        colors_ = 256;
    pen_known_ = false;
}

int Term::signal_fd() const
{
    return g_sig_pipe[0];
}

bool Term::update_size()
{
    int c = 0, r = 0;
    struct winsize ws;
    if (ioctl(out_fd_, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        c = ws.ws_col;
        r = ws.ws_row;
    }
    if (c <= 0) {
        const char* e = getenv("COLUMNS");
        c = e ? atoi(e) : ti_.num("cols");
    }
    if (r <= 0) {
        const char* e = getenv("LINES");
        r = e ? atoi(e) : ti_.num("lines");
    }
    if (c <= 0)
        c = 80;
    if (r <= 0)
        r = 24;
    bool changed = c != cols_ || r != rows_;
    cols_ = c;
    rows_ = r;
    return changed;
}

/* ------------------------------------------------------------------ */
/* Taking and giving back the terminal                                 */

bool Term::start()
{
    if (started_)
        return true;
    if (tcgetattr(in_fd_, &saved_) == 0) {
        have_saved_ = true;
        struct termios raw = saved_;
        raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
        raw.c_oflag &= ~OPOST;
        raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
        raw.c_cflag &= ~(CSIZE | PARENB);
        raw.c_cflag |= CS8;
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(in_fd_, TCSAFLUSH, &raw);
    }
    started_ = true;
    cap("smcup");
    cap("smkx");
    if (ti_.str("enacs"))
        cap("enacs");
    if (xterm_like_) {
        out("\033[?1000h\033[?1006h");      /* mouse, SGR coordinates */
        out("\033[?2004h");                 /* bracketed paste */
    }
    cursor_shown_ = true;
    show_cursor(false);
    acs_on_ = false;
    pen_known_ = false;
    cx_ = cy_ = -1;
    clear_screen();
    flush();
    return true;
}

void Term::stop()
{
    if (!started_)
        return;
    acs(false);
    reset_style();
    if (xterm_like_)
        out("\033[?2004l\033[?1006l\033[?1000l");
    show_cursor(true);
    cap("rmkx");
    cap("rmcup");
    flush();
    if (have_saved_)
        tcsetattr(in_fd_, TCSAFLUSH, &saved_);
    started_ = false;
    cx_ = cy_ = -1;
    pen_known_ = false;
}

/* ------------------------------------------------------------------ */
/* Output                                                              */

void Term::out(const char* s, size_t n)
{
    if (len_ + n > cap_) {
        size_t nc = cap_ ? cap_ * 2 : 16384;
        while (nc < len_ + n)
            nc *= 2;
        char* nb = (char*)realloc(buf_, nc);
        if (!nb)
            return;
        buf_ = nb;
        cap_ = nc;
    }
    memcpy(buf_ + len_, s, n);
    len_ += n;
}

void Term::out(const char* s)
{
    out(s, strlen(s));
}

void Term::flush()
{
    size_t off = 0;
    while (off < len_) {
        ssize_t w = write(out_fd_, buf_ + off, len_ - off);
        if (w < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            break;
        }
        off += (size_t)w;
    }
    len_ = 0;
}

/* A terminfo string, with padding removed. */
void Term::cap_str(const char* s)
{
    if (!s)
        return;
    char b[512];
    int n = tparm(b, sizeof(b), s, 0, 0);
    if (n > 0)
        out(b, (size_t)n < sizeof(b) ? (size_t)n : sizeof(b) - 1);
}

void Term::cap(const char* name)
{
    cap_str(ti_.str(name));
}

void Term::move(int x, int y)
{
    if (x == cx_ && y == cy_)
        return;
    char b[64];
    const char* cup = ti_.str("cup");
    if (cup && tparm2(b, sizeof(b), cup, y, x) > 0) {
        out(b);
    } else {
        sprintf(b, "\033[%d;%dH", y + 1, x + 1);
        out(b);
    }
    cx_ = x;
    cy_ = y;
}

void Term::acs(bool on)
{
    if (on == acs_on_)
        return;
    cap(on ? "smacs" : "rmacs");
    acs_on_ = on;
}

void Term::reset_style()
{
    acs(false);
    const char* s = ti_.str("sgr0");
    if (s)
        cap_str(s);
    else
        out("\033[m");
    /* vt100's sgr0 also shifts in: ACS is off either way. */
    acs_on_ = false;
    pen_ = Style();
    pen_known_ = true;
}

void Term::color_sgr(Color c, bool bg, unsigned* extra)
{
    if (c == COLOR_DEFAULT || tier_ == TIER_BASIC)
        return;
    char b[64];
    if (truecolor_) {
        sprintf(b, "\033[%d;2;%lu;%lu;%lum", bg ? 48 : 38, (unsigned long)((c >> 16) & 255),
                (unsigned long)((c >> 8) & 255), (unsigned long)(c & 255));
        out(b);
        return;
    }
    int idx;
    if (colors_ >= 256)
        idx = nearest256(c);
    else if (colors_ >= 16)
        idx = nearest16(c, 16);
    else {
        idx = nearest16(c, 16);
        if (idx >= 8) {
            /* Bright colours on an 8-colour terminal: bold for text. */
            if (!bg)
                *extra |= ATTR_BOLD;
            idx -= 8;
        }
    }
    const char* s = ti_.str(bg ? "setab" : "setaf");
    if (!s)
        s = ti_.str(bg ? "setb" : "setf");
    if (s && tparm1(b, sizeof(b), s, idx) > 0)
        out(b);
}

void Term::set_style(const Style& s)
{
    if (pen_known_ && s == pen_)
        return;
    /* Attributes can only be added; anything removed, or a colour going
     * back to the default, starts from sgr0. */
    bool reset = !pen_known_ || (pen_.attrs & ~s.attrs) != 0 ||
                 (s.fg == COLOR_DEFAULT && pen_.fg != COLOR_DEFAULT) ||
                 (s.bg == COLOR_DEFAULT && pen_.bg != COLOR_DEFAULT);
    if (reset)
        reset_style();
    unsigned extra = 0;
    if (s.fg != pen_.fg || reset)
        color_sgr(s.fg, false, &extra);
    if (s.bg != pen_.bg || reset)
        color_sgr(s.bg, true, &extra);
    unsigned add = (s.attrs | extra) & ~(reset ? 0u : pen_.attrs);
    if (add & ATTR_BOLD)
        cap("bold");
    if (add & ATTR_DIM)
        cap("dim");
    if (add & ATTR_ITALIC)
        cap("sitm");
    if (add & ATTR_UNDERLINE)
        cap("smul");
    if (add & ATTR_REVERSE)
        cap(ti_.str("rev") ? "rev" : "smso");
    if (add & ATTR_BLINK)
        cap("blink");
    pen_ = s;
    pen_.attrs = s.attrs | extra;
    pen_known_ = true;
}

void Term::put_char(uint32_t cp, int width)
{
    char b[8];
    const AcsMap* m = acs_for(cp);
    if (m && !utf8_) {
        if (has_acs() && acs_[(unsigned char)m->acs]) {
            acs(true);
            out(&acs_[(unsigned char)m->acs], 1);
        } else {
            acs(false);
            out(&m->ascii, 1);
        }
    } else {
        acs(false);
        if (cp < 0x80) {
            b[0] = (char)(cp < 0x20 || cp == 0x7f ? '?' : cp);
            out(b, 1);
        } else if (!utf8_) {
            b[0] = (char)(cp < 0xa0 || cp > 0xff ? '?' : cp);
            out(b, 1);
        } else {
            int n;
            if (cp < 0x800) {
                b[0] = (char)(0xc0 | (cp >> 6));
                b[1] = (char)(0x80 | (cp & 0x3f));
                n = 2;
            } else if (cp < 0x10000) {
                b[0] = (char)(0xe0 | (cp >> 12));
                b[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
                b[2] = (char)(0x80 | (cp & 0x3f));
                n = 3;
            } else {
                b[0] = (char)(0xf0 | (cp >> 18));
                b[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
                b[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
                b[3] = (char)(0x80 | (cp & 0x3f));
                n = 4;
            }
            out(b, (size_t)n);
        }
    }
    if (cx_ >= 0) {
        cx_ += width;
        /* At the right margin the cursor position depends on the
         * terminal's wrap rules: forget it. */
        if (cx_ >= cols_)
            cx_ = cy_ = -1;
    }
}

bool Term::clear_eol()
{
    const char* el = ti_.str("el");
    if (!el || (pen_.bg != COLOR_DEFAULT && !bce_) || acs_on_)
        return false;
    cap_str(el);
    return true;
}

void Term::clear_screen()
{
    reset_style();
    const char* c = ti_.str("clear");
    if (c)
        cap_str(c);
    else
        out("\033[H\033[2J");
    cx_ = cy_ = 0;
}

void Term::show_cursor(bool on)
{
    if (on == cursor_shown_)
        return;
    cap(on ? "cnorm" : "civis");
    cursor_shown_ = on;
}

void Term::begin_sync()
{
    if (tier_ == TIER_MODERN)
        out("\033[?2026h");
}

void Term::end_sync()
{
    if (tier_ == TIER_MODERN)
        out("\033[?2026l");
}

} /* namespace tu */
