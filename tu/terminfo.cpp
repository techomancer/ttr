#include "terminfo.h"
#include "capnames.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace tu {

/* Built-in entries, for systems without the terminal's compiled entry
 * (ttrt's is often missing on other machines). Only what tu uses. */
static const char k_builtin_xterm[] =
    "xterm-256color|xterm with 256 colours (tu built-in),"
    " am, bce, km, mir, msgr, xenl, colors#256, cols#80, it#8, lines#24,"
    " acsc=``aaffggiijjkkllmmnnooppqqrrssttuuvvwwxxyyzz{{||}}~~,"
    " bel=^G, blink=\\E[5m, bold=\\E[1m, civis=\\E[?25l, clear=\\E[H\\E[2J,"
    " cnorm=\\E[?12l\\E[?25h, cr=\\r, csr=\\E[%i%p1%d;%p2%dr,"
    " cub1=^H, cud1=\\n, cuf1=\\E[C, cup=\\E[%i%p1%d;%p2%dH, cuu1=\\E[A,"
    " dim=\\E[2m, ech=\\E[%p1%dX, ed=\\E[J, el=\\E[K, home=\\E[H,"
    " hpa=\\E[%i%p1%dG, ind=\\n, ri=\\EM, rev=\\E[7m, sitm=\\E[3m, ritm=\\E[23m,"
    " smul=\\E[4m, rmul=\\E[24m, sgr0=\\E(B\\E[m, smacs=\\E(0, rmacs=\\E(B,"
    " smcup=\\E[?1049h\\E[22;0;0t, rmcup=\\E[?1049l\\E[23;0;0t,"
    " smkx=\\E[?1h\\E=, rmkx=\\E[?1l\\E>, op=\\E[39;49m,"
    " setaf=\\E[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38;5;%p1%d%;m,"
    " setab=\\E[%?%p1%{8}%<%t4%p1%d%e%p1%{16}%<%t10%p1%{8}%-%d%e48;5;%p1%d%;m,"
    " kbs=^?, kcbt=\\E[Z, kcub1=\\EOD, kcud1=\\EOB, kcuf1=\\EOC, kcuu1=\\EOA,"
    " kdch1=\\E[3~, kend=\\EOF, khome=\\EOH, kich1=\\E[2~, knp=\\E[6~, kpp=\\E[5~,"
    " kf1=\\EOP, kf2=\\EOQ, kf3=\\EOR, kf4=\\EOS, kf5=\\E[15~, kf6=\\E[17~,"
    " kf7=\\E[18~, kf8=\\E[19~, kf9=\\E[20~, kf10=\\E[21~, kf11=\\E[23~,"
    " kf12=\\E[24~, kmous=\\E[M,";

static const char k_builtin_vt100[] =
    "vt100|DEC VT100 (tu built-in),"
    " am, msgr, xenl, xon, cols#80, it#8, lines#24,"
    " acsc=``aaffggjjkkllmmnnooppqqrrssttuuvvwwxxyyzz{{||}}~~,"
    " bel=^G, blink=\\E[5m, bold=\\E[1m, clear=\\E[H\\E[J, cr=\\r,"
    " csr=\\E[%i%p1%d;%p2%dr, cub1=^H, cud1=\\n, cuf1=\\E[C,"
    " cup=\\E[%i%p1%d;%p2%dH, cuu1=\\E[A, ed=\\E[J, el=\\E[K, enacs=\\E(B\\E)0,"
    " home=\\E[H, ind=\\n, ri=\\EM, rev=\\E[7m, rmacs=^O, smacs=^N,"
    " sgr0=\\E[m^O, smul=\\E[4m, rmul=\\E[m,"
    " smkx=\\E[?1h\\E=, rmkx=\\E[?1l\\E>,"
    " kbs=^H, kcub1=\\EOD, kcud1=\\EOB, kcuf1=\\EOC, kcuu1=\\EOA,"
    " kf1=\\EOP, kf2=\\EOQ, kf3=\\EOR, kf4=\\EOS,";

/* IRIX's iris-ansi, as shipped with 6.5 (xwsh, winterm). */
static const char k_builtin_iris_ansi[] =
    "iris-ansi|IRIS emulating 40 line ANSI terminal (tu built-in),"
    " am, cols#80, it#8, lines#40,"
    " bel=^G, bold=\\E[1m, clear=\\E[H\\E[2J, cr=\\r, cub1=\\E[D, cud1=\\n,"
    " cuf1=\\E[C, cup=\\E[%i%p1%d;%p2%dH, cuu1=\\E[A, dl1=\\E[M, ed=\\E[J,"
    " el=\\E[K, home=\\E[H, il1=\\E[L, ind=\\ED, ri=\\EM, rev=\\E[7m,"
    " sgr0=\\E[m, smul=\\E[4m, rmul=\\E[m,"
    " kbs=\\b, kcbt=\\E[Z, kcub1=\\E[D, kcud1=\\E[B, kcuf1=\\E[C, kcuu1=\\E[A,"
    " kdch1=^?, kend=\\E[146q, khome=\\E[H, kich1=\\E[139q, knp=\\E[154q,"
    " kpp=\\E[150q, kf1=\\E[001q, kf2=\\E[002q, kf3=\\E[003q, kf4=\\E[004q,"
    " kf5=\\E[005q, kf6=\\E[006q, kf7=\\E[007q, kf8=\\E[008q, kf9=\\EOP,"
    " kf10=\\EOQ, kf11=\\EOR, kf12=\\EOS,";

struct Builtin {
    const char* name;
    const char* source;
};

static const Builtin k_builtins[] = {
    { "xterm-256color", k_builtin_xterm },
    { "xterm", k_builtin_xterm },
    { "ttrt", k_builtin_xterm },
    { "vt100", k_builtin_vt100 },
    { "vt102", k_builtin_vt100 },
    { "iris-ansi", k_builtin_iris_ansi },
    { "iris-ansi-net", k_builtin_iris_ansi },
    { 0, 0 }
};

static int read16(const unsigned char* p)
{
    int v = p[0] | (p[1] << 8);
    return v >= 0x8000 ? v - 0x10000 : v;
}

static long read32(const unsigned char* p)
{
    unsigned long v = (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
                      ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
    return v >= 0x80000000ul ? (long)(v - 0x80000000ul) - 0x7fffffffl - 1 : (long)v;
}

Terminfo::Terminfo() : caps_(0), ncaps_(0), capcap_(0), blocks_(0)
{
    name_[0] = 0;
    source_[0] = 0;
}

Terminfo::~Terminfo()
{
    clear();
}

void Terminfo::clear()
{
    free(caps_);
    caps_ = 0;
    ncaps_ = capcap_ = 0;
    while (blocks_) {
        Block* next = blocks_->next;
        free(blocks_);
        blocks_ = next;
    }
    name_[0] = 0;
    source_[0] = 0;
}

char* Terminfo::keep(const char* s, size_t n)
{
    Block* b = blocks_;
    if (!b || b->used + n + 1 > b->size) {
        size_t size = n + 1 > 8192 ? n + 1 : 8192;
        b = (Block*)malloc(sizeof(Block) + size);
        if (!b)
            return 0;
        b->next = blocks_;
        b->used = 0;
        b->size = size;
        blocks_ = b;
    }
    char* d = (char*)(b + 1) + b->used;
    memcpy(d, s, n);
    d[n] = 0;
    b->used += n + 1;
    return d;
}

void Terminfo::add(const char* name, char type, int num, const char* str)
{
    if (!name || !*name || find(name, type))
        return;
    if (ncaps_ == capcap_) {
        int nc = capcap_ ? capcap_ * 2 : 128;
        Cap* n = (Cap*)realloc(caps_, (size_t)nc * sizeof(Cap));
        if (!n)
            return;
        caps_ = n;
        capcap_ = nc;
    }
    Cap& c = caps_[ncaps_++];
    c.name = name;
    c.type = type;
    c.num = num;
    c.str = str;
}

const Terminfo::Cap* Terminfo::find(const char* name, char type) const
{
    for (int i = 0; i < ncaps_; i++)
        if (caps_[i].type == type && strcmp(caps_[i].name, name) == 0)
            return &caps_[i];
    return 0;
}

bool Terminfo::flag(const char* name) const
{
    const Cap* c = find(name, 'b');
    return c && c->num;
}

int Terminfo::num(const char* name) const
{
    const Cap* c = find(name, 'n');
    return c ? c->num : -1;
}

const char* Terminfo::str(const char* name) const
{
    const Cap* c = find(name, 's');
    return c ? c->str : 0;
}

/* ------------------------------------------------------------------ */
/* Compiled entries                                                    */

bool Terminfo::load_compiled(const unsigned char* d, size_t n)
{
    clear();
    if (n < 12)
        return false;
    int magic = read16(d);
    int nsize;
    if (magic == 0432)
        nsize = 2;
    else if (magic == 01036)
        nsize = 4;
    else
        return false;
    int names_size = read16(d + 2), nbool = read16(d + 4), nnum = read16(d + 6),
        nstr = read16(d + 8), strtab = read16(d + 10);
    if (names_size < 0 || nbool < 0 || nnum < 0 || nstr < 0 || strtab < 0)
        return false;
    size_t off = 12;
    size_t names_at = off;
    off += (size_t)names_size;
    size_t bools_at = off;
    off += (size_t)nbool;
    if (off & 1)
        off++;
    size_t nums_at = off;
    off += (size_t)nnum * (size_t)nsize;
    size_t stroff_at = off;
    off += (size_t)nstr * 2;
    size_t strtab_at = off;
    off += (size_t)strtab;
    if (off > n)
        return false;

    const char* nm = (const char*)d + names_at;
    size_t nl = 0;
    while (nl < (size_t)names_size && nm[nl] && nm[nl] != '|' && nl < sizeof(name_) - 1)
        nl++;
    memcpy(name_, nm, nl);
    name_[nl] = 0;

    int i;
    int nbn = 0, nnn = 0, nsn = 0;
    while (k_bool_names[nbn])
        nbn++;
    while (k_num_names[nnn])
        nnn++;
    while (k_str_names[nsn])
        nsn++;
    for (i = 0; i < nbool && i < nbn; i++)
        if (d[bools_at + (size_t)i] == 1)
            add(k_bool_names[i], 'b', 1, 0);
    for (i = 0; i < nnum && i < nnn; i++) {
        long v = nsize == 2 ? read16(d + nums_at + (size_t)i * 2)
                            : read32(d + nums_at + (size_t)i * 4);
        if (v >= 0)
            add(k_num_names[i], 'n', (int)v, 0);
    }
    for (i = 0; i < nstr && i < nsn; i++) {
        int so = read16(d + stroff_at + (size_t)i * 2);
        if (so < 0 || so >= strtab)
            continue;
        const char* s = (const char*)d + strtab_at + so;
        size_t len = 0;
        while (strtab_at + (size_t)so + len < off && s[len])
            len++;
        add(k_str_names[i], 's', 0, keep(s, len));
    }

    /* The extended section (ncurses user capabilities). */
    if (off & 1)
        off++;
    if (off + 10 <= n) {
        int ebool = read16(d + off), enum_ = read16(d + off + 2),
            estr = read16(d + off + 4), etab = read16(d + off + 8);
        if (ebool >= 0 && enum_ >= 0 && estr >= 0 && etab >= 0) {
            size_t p = off + 10;
            size_t eb_at = p;
            p += (size_t)ebool;
            if (p & 1)
                p++;
            size_t en_at = p;
            p += (size_t)enum_ * (size_t)nsize;
            size_t es_at = p;
            p += (size_t)estr * 2;
            size_t enm_at = p;
            int nnames = ebool + enum_ + estr;
            p += (size_t)nnames * 2;
            size_t tab_at = p;
            if (tab_at + (size_t)etab <= n) {
                const char* tab = (const char*)d + tab_at;
                /* Names follow the last string value. */
                size_t names_base = 0;
                for (i = 0; i < estr; i++) {
                    int so = read16(d + es_at + (size_t)i * 2);
                    if (so < 0 || so >= etab)
                        continue;
                    size_t e = (size_t)so;
                    while (e < (size_t)etab && tab[e])
                        e++;
                    if (e + 1 > names_base)
                        names_base = e + 1;
                }
                for (i = 0; i < nnames; i++) {
                    int no = read16(d + enm_at + (size_t)i * 2);
                    if (no < 0 || names_base + (size_t)no >= (size_t)etab)
                        continue;
                    const char* en = tab + names_base + no;
                    char* name = keep(en, strlen(en));
                    if (!name)
                        continue;
                    if (i < ebool) {
                        if (d[eb_at + (size_t)i] == 1)
                            add(name, 'b', 1, 0);
                    } else if (i < ebool + enum_) {
                        int k = i - ebool;
                        long v = nsize == 2 ? read16(d + en_at + (size_t)k * 2)
                                            : read32(d + en_at + (size_t)k * 4);
                        if (v >= 0)
                            add(name, 'n', (int)v, 0);
                    } else {
                        int k = i - ebool - enum_;
                        int so = read16(d + es_at + (size_t)k * 2);
                        if (so < 0 || so >= etab)
                            continue;
                        add(name, 's', 0, keep(tab + so, strlen(tab + so)));
                    }
                }
            }
        }
    }
    return true;
}

bool Terminfo::load_file(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    unsigned char buf[65536];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (!load_compiled(buf, n))
        return false;
    strncpy(source_, path, sizeof(source_) - 1);
    source_[sizeof(source_) - 1] = 0;
    return true;
}

/* dir/x/xterm or dir/78/xterm */
static bool entry_path(const char* dir, const char* term, char* out, size_t n)
{
    if (!dir || !*dir || strlen(dir) + strlen(term) + 8 > n)
        return false;
    sprintf(out, "%s/%c/%s", dir, term[0], term);
    if (access(out, R_OK) == 0)
        return true;
    sprintf(out, "%s/%02x/%s", dir, (unsigned char)term[0], term);
    return access(out, R_OK) == 0;
}

bool Terminfo::load(const char* term)
{
    if (!term || !*term || strchr(term, '/') || strlen(term) > 100)
        return false;
    char path[1200];
    const char* env = getenv("TERMINFO");
    if (env && entry_path(env, term, path, sizeof(path)) && load_file(path))
        return true;
    const char* home = getenv("HOME");
    if (home && strlen(home) < 900) {
        char dir[1000];
        sprintf(dir, "%s/.terminfo", home);
        if (entry_path(dir, term, path, sizeof(path)) && load_file(path))
            return true;
    }
    const char* dirs = getenv("TERMINFO_DIRS");
    while (dirs && *dirs) {
        const char* colon = strchr(dirs, ':');
        size_t l = colon ? (size_t)(colon - dirs) : strlen(dirs);
        char dir[1000];
        if (l > 0 && l < sizeof(dir)) {
            memcpy(dir, dirs, l);
            dir[l] = 0;
            if (entry_path(dir, term, path, sizeof(path)) && load_file(path))
                return true;
        }
        if (!colon)
            break;
        dirs = colon + 1;
    }
    static const char* const system_dirs[] = {
        "/usr/share/terminfo", "/usr/share/lib/terminfo", "/usr/lib/terminfo",
        "/etc/terminfo", "/lib/terminfo", "/usr/local/share/terminfo", 0
    };
    for (int i = 0; system_dirs[i]; i++)
        if (entry_path(system_dirs[i], term, path, sizeof(path)) && load_file(path))
            return true;
    return load_builtin(term);
}

/* ------------------------------------------------------------------ */
/* Source syntax                                                       */

static int parse_number(const char* s, size_t n)
{
    char buf[32];
    if (n >= sizeof(buf))
        n = sizeof(buf) - 1;
    memcpy(buf, s, n);
    buf[n] = 0;
    return (int)strtol(buf, 0, 0);
}

/* Unescape a string value into out; returns its length. */
static size_t unescape(const char* s, size_t n, char* out)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '^' && i + 1 < n) {
            char x = s[++i];
            out[o++] = x == '?' ? (char)0x7f : (char)(toupper((unsigned char)x) & 0x1f);
        } else if (c == '\\' && i + 1 < n) {
            char x = s[++i];
            switch (x) {
            case 'E': case 'e': out[o++] = 033; break;
            case 'n': case 'l': out[o++] = '\n'; break;
            case 'r': out[o++] = '\r'; break;
            case 't': out[o++] = '\t'; break;
            case 'b': out[o++] = '\b'; break;
            case 'f': out[o++] = '\f'; break;
            case 's': out[o++] = ' '; break;
            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7': {
                int v = 0, k = 0;
                i--;
                while (k < 3 && i + 1 < n && s[i + 1] >= '0' && s[i + 1] <= '7') {
                    v = v * 8 + (s[++i] - '0');
                    k++;
                }
                out[o++] = v ? (char)v : (char)0200;    /* \0 is NUL as \200 */
                break;
            }
            default: out[o++] = x; break;               /* \\ \, \^ \: */
            }
        } else {
            out[o++] = c;
        }
    }
    out[o] = 0;
    return o;
}

void Terminfo::parse_fields(const char* text, bool first_is_name)
{
    size_t tl = strlen(text);
    char* tmp = (char*)malloc(tl + 1);
    if (!tmp)
        return;
    /* Fields are separated by unescaped commas. */
    const char* p = text;
    bool first = first_is_name;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        if (!*p)
            break;
        const char* start = p;
        while (*p && *p != ',') {
            if (*p == '\\' && p[1])
                p++;
            p++;
        }
        size_t len = (size_t)(p - start);
        if (*p == ',')
            p++;
        if (first) {
            size_t k = 0;
            while (k < len && start[k] != '|' && k < sizeof(name_) - 1)
                k++;
            memcpy(name_, start, k);
            name_[k] = 0;
            first = false;
            continue;
        }
        size_t k = 0;
        while (k < len && start[k] != '=' && start[k] != '#' && start[k] != '@')
            k++;
        char* name = keep(start, k);
        if (!name)
            break;
        if (k == len) {
            add(name, 'b', 1, 0);
        } else if (start[k] == '#') {
            add(name, 'n', parse_number(start + k + 1, len - k - 1), 0);
        } else if (start[k] == '=') {
            size_t vl = unescape(start + k + 1, len - k - 1, tmp);
            add(name, 's', 0, keep(tmp, vl));
        }
        /* name@ cancels: nothing to add */
    }
    free(tmp);
}

bool Terminfo::load_source(const char* text)
{
    clear();
    parse_fields(text, true);
    strcpy(source_, "built-in");
    return name_[0] != 0;
}

void Terminfo::supplement(const char* caps)
{
    parse_fields(caps, false);
}

bool Terminfo::load_builtin(const char* term)
{
    for (int i = 0; k_builtins[i].name; i++) {
        if (strcmp(k_builtins[i].name, term) == 0) {
            if (!load_source(k_builtins[i].source))
                return false;
            strncpy(name_, term, sizeof(name_) - 1);
            name_[sizeof(name_) - 1] = 0;
            return true;
        }
    }
    return false;
}

} /* namespace tu */
