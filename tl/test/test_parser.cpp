#include "tltest.h"
#include "../parser.h"

#include <stdio.h>
#include <string.h>

using namespace tl;

namespace {

/* Records every action as text:
 *   R(hello)   print_run        P(1f600)  print (hex code point)
 *   X(1b)      execute          E(#8)     esc: priv, intermediates, final
 *   C(?1;2h)   csi, "!" appended on parameter overflow
 *   H(q)       dcs hook         D(data)   dcs put     U / Ua   unhook (a: aborted)
 *   O(text)    osc, "t" appended when truncated      A(text)   apc */
class Recorder : public ParserSink {
public:
    char out[8192];
    size_t len;
    Seq last_csi;

    Recorder() : len(0) { out[0] = 0; last_csi.clear(); }
    void clear() { len = 0; out[0] = 0; }

    void put(const char* s)
    {
        size_t n = strlen(s);
        if (len + n + 1 > sizeof(out))
            return;
        memcpy(out + len, s, n + 1);
        len += n;
    }
    void put_bytes(const uint8_t* s, size_t n)
    {
        for (size_t i = 0; i < n; i++) {
            char b[8];
            if (s[i] >= 0x20 && s[i] < 0x7f)
                sprintf(b, "%c", s[i]);
            else
                sprintf(b, "\\x%02x", s[i]);
            put(b);
        }
    }
    void put_seq(const Seq& q, bool with_params)
    {
        char b[16];
        if (q.priv) {
            sprintf(b, "%c", q.priv);
            put(b);
        }
        if (with_params) {
            for (int i = 0; i < q.params.count; i++) {
                if (i)
                    put(q.params.is_sub[i] ? ":" : ";");
                sprintf(b, "%d", q.params.value[i]);
                put(b);
            }
            if (q.params.overflow)
                put("!");
        }
        for (int i = 0; i < q.n_intermediates; i++) {
            sprintf(b, "%c", q.intermediates[i]);
            put(b);
        }
        sprintf(b, "%c", q.final_byte);
        put(b);
    }

    virtual void print(uint32_t cp)
    {
        char b[32];
        sprintf(b, "P(%x)", (unsigned)cp);
        put(b);
    }
    virtual void print_run(const uint8_t* s, size_t n)
    {
        put("R(");
        put_bytes(s, n);
        put(")");
    }
    virtual void execute(uint8_t c)
    {
        char b[16];
        sprintf(b, "X(%02x)", c);
        put(b);
    }
    virtual void esc_dispatch(const Seq& q) { put("E("); put_seq(q, false); put(")"); }
    virtual void csi_dispatch(const Seq& q)
    {
        last_csi = q;
        put("C(");
        put_seq(q, true);
        put(")");
    }
    virtual void dcs_hook(const Seq& q) { put("H("); put_seq(q, true); put(")"); }
    virtual void dcs_put(const uint8_t* s, size_t n) { put("D("); put_bytes(s, n); put(")"); }
    virtual void dcs_unhook(bool aborted) { put(aborted ? "Ua" : "U"); }
    virtual void osc_dispatch(const uint8_t* s, size_t n, bool t)
    {
        put("O(");
        put_bytes(s, n);
        put(")");
        if (t)
            put("t");
    }
    virtual void apc_dispatch(const uint8_t* s, size_t n, bool t)
    {
        put("A(");
        put_bytes(s, n);
        put(")");
        if (t)
            put("t");
    }
};

struct Fixture {
    Parser p;
    Recorder r;
    Fixture() { p.set_sink(&r); }
    const char* run(const char* bytes, size_t n)
    {
        r.clear();
        p.feed((const uint8_t*)bytes, n);
        return r.out;
    }
    const char* run(const char* s) { return run(s, strlen(s)); }
};

#define FEED(f, lit) (f).run(lit, sizeof(lit) - 1)

} /* namespace */

void tl_test_parser()
{
    Fixture f;

    /* Plain text and controls. */
    TL_CHECK_STR(FEED(f, "hello"), "R(hello)");
    TL_CHECK_STR(FEED(f, "a\r\nb"), "R(a)X(0d)X(0a)R(b)");
    TL_CHECK_STR(FEED(f, "\x7f"), "");
    TL_CHECK_STR(FEED(f, "\x07"), "X(07)");

    /* ESC sequences. */
    TL_CHECK_STR(FEED(f, "\x1b(B"), "E((B)");
    TL_CHECK_STR(FEED(f, "\x1b" "7\x1b" "8"), "E(7)E(8)");
    TL_CHECK_STR(FEED(f, "\x1b#8"), "E(#8)");
    TL_CHECK_STR(FEED(f, "\x1b%G"), "E(%G)");
    /* ESC then a C0: executed, sequence continues. */
    TL_CHECK_STR(FEED(f, "\x1b\n(0"), "X(0a)E((0)");

    /* CSI. */
    TL_CHECK_STR(FEED(f, "\x1b[H"), "C(H)");
    TL_CHECK_STR(FEED(f, "\x1b[1;2H"), "C(1;2H)");
    TL_CHECK_STR(FEED(f, "\x1b[;5H"), "C(0;5H)");
    TL_CHECK_STR(FEED(f, "\x1b[5;H"), "C(5;0H)");
    TL_CHECK_STR(FEED(f, "\x1b[?1049h"), "C(?1049h)");
    TL_CHECK_STR(FEED(f, "\x1b[>1;2c"), "C(>1;2c)");
    TL_CHECK_STR(FEED(f, "\x1b[2 q"), "C(2 q)");
    TL_CHECK_STR(FEED(f, "\x1b[?2026$p"), "C(?2026$p)");
    TL_CHECK_STR(FEED(f, "\x1b[38:2::255:0:0;1m"), "C(38:2:0:255:0:0;1m)");
    TL_CHECK_STR(FEED(f, "\x1b[4:3m"), "C(4:3m)");
    /* C0 inside a CSI executes and the sequence goes on. */
    TL_CHECK_STR(FEED(f, "\x1b[1\n2J"), "X(0a)C(12J)");
    /* ESC inside a CSI abandons it. */
    TL_CHECK_STR(FEED(f, "\x1b[1\x1b[2J"), "C(2J)");
    /* CAN cancels. */
    TL_CHECK_STR(FEED(f, "\x1b[1\x18J"), "X(18)R(J)");
    /* Private marker not in first place: the sequence is ignored. */
    TL_CHECK_STR(FEED(f, "\x1b[1?2hA"), "R(A)");
    /* Intermediate then a digit: ignored. */
    TL_CHECK_STR(FEED(f, "\x1b[ 1qA"), "R(A)");
    /* Too many intermediates: ignored. */
    TL_CHECK_STR(FEED(f, "\x1b[ !\"#$mA"), "R(A)");
    /* Parameter values clamp; count is capped with the overflow flag. */
    TL_CHECK_STR(FEED(f, "\x1b[99999m"), "C(65535m)");
    {
        char big[256];
        strcpy(big, "\x1b[");
        for (int i = 0; i < 40; i++)
            strcat(big, "1;");
        strcat(big, "2m");
        const char* out = f.run(big);
        TL_CHECK(strncmp(out, "C(1;1;1", 7) == 0);
        TL_CHECK(strstr(out, "!m)") != 0);
        TL_CHECK(f.r.last_csi.params.count == TL_MAX_PARAMS);
        TL_CHECK(f.r.last_csi.params.overflow);
    }

    /* Params helpers. */
    FEED(f, "\x1b[38:2::255:0:0;1;;4:3m");
    {
        const Params& p = f.r.last_csi.params;
        TL_CHECK(p.top_count() == 4);
        TL_CHECK(p.top(0, 0) == 38);
        TL_CHECK(p.subs_of(0) == 5);
        TL_CHECK(p.top(1, 0) == 1);
        TL_CHECK(p.top(2, 7) == 7);
        TL_CHECK(p.top(3, 0) == 4);
        TL_CHECK(p.top_index(3) == 8);
        TL_CHECK(p.subs_of(8) == 1);
        TL_CHECK(p.value[9] == 3);
        TL_CHECK(p.top(4, 9) == 9);
    }

    /* OSC. */
    TL_CHECK_STR(FEED(f, "\x1b]0;title\x07"), "O(0;title)");
    TL_CHECK_STR(FEED(f, "\x1b]2;x\x1b\\"), "O(2;x)E(\\)");
    TL_CHECK_STR(FEED(f, "\x1b]8;;http://a/b\x1b\\"), "O(8;;http://a/b)E(\\)");
    /* C0 (other than BEL, CAN, SUB, ESC) inside OSC are dropped. */
    TL_CHECK_STR(FEED(f, "\x1b]0;a\nb\x07"), "O(0;ab)");
    /* CAN drops the string. */
    TL_CHECK_STR(FEED(f, "\x1b]0;abc\x18x"), "X(18)R(x)");
    /* ESC followed by something other than backslash still ends the OSC. */
    TL_CHECK_STR(FEED(f, "\x1b]0;a\x1b[2J"), "O(0;a)C(2J)");
    /* Truncation. */
    f.p.set_string_limits(8, 8);
    TL_CHECK_STR(FEED(f, "\x1b]0;0123456789\x07"), "O(0;012345)t");
    f.p.set_string_limits(1u << 20, 16u << 20);

    /* APC, SOS, PM. */
    TL_CHECK_STR(FEED(f, "\x1b_Gf=100;AAAA\x1b\\"), "A(Gf=100;AAAA)E(\\)");
    TL_CHECK_STR(FEED(f, "\x1b^ignored\x1b\\"), "E(\\)");
    TL_CHECK_STR(FEED(f, "\x1bXignored\x1b\\x"), "E(\\)R(x)");

    /* DCS. */
    TL_CHECK_STR(FEED(f, "\x1bPq#0;2;0;0;0#0~~\x1b\\"), "H(q)D(#0;2;0;0;0#0~~)UE(\\)");
    TL_CHECK_STR(FEED(f, "\x1bP0;1q~\x1b\\"), "H(0;1q)D(~)UE(\\)");
    TL_CHECK_STR(FEED(f, "\x1bP$qm\x1b\\"), "H($q)D(m)UE(\\)");
    TL_CHECK_STR(FEED(f, "\x1bP+q544e\x1b\\"), "H(+q)D(544e)UE(\\)");
    TL_CHECK_STR(FEED(f, "\x1bPq123\x18x"), "H(q)D(123)UaX(18)R(x)");
    /* Data is batched per feed() call. */
    TL_CHECK_STR(FEED(f, "\x1bPqab"), "H(q)D(ab)");
    TL_CHECK_STR(FEED(f, "cd\x1b\\"), "D(cd)UE(\\)");
    /* C0 inside a DCS header is ignored, not executed. */
    TL_CHECK_STR(FEED(f, "\x1bP1\n;2qx\x1b\\"), "H(1;2q)D(x)UE(\\)");
    /* Reset while in a DCS unhooks it as aborted. */
    FEED(f, "\x1bPqab");
    f.r.clear();
    f.p.reset();
    TL_CHECK_STR(f.r.out, "Ua");
    TL_CHECK(f.p.in_ground());

    /* UTF-8. */
    TL_CHECK_STR(FEED(f, "\xc3\xa9"), "P(e9)");
    TL_CHECK_STR(FEED(f, "a\xe2\x82\xac" "b"), "R(a)P(20ac)R(b)");
    TL_CHECK_STR(FEED(f, "\xf0\x9f\x98\x80"), "P(1f600)");
    TL_CHECK_STR(FEED(f, "\xc0\x80"), "P(fffd)P(fffd)");          /* overlong */
    TL_CHECK_STR(FEED(f, "\xe2\x82" "A"), "P(fffd)P(41)");         /* cut short */
    TL_CHECK_STR(FEED(f, "\x80"), "P(fffd)");                      /* lone continuation */
    TL_CHECK_STR(FEED(f, "\xed\xa0\x80"), "P(fffd)P(fffd)P(fffd)"); /* surrogate */
    TL_CHECK_STR(FEED(f, "\xf4\x90\x80\x80"), "P(fffd)P(fffd)P(fffd)P(fffd)"); /* > 10FFFF */
    TL_CHECK_STR(FEED(f, "\xf4\x8f\xbf\xbf"), "P(10ffff)");
    /* Split across feeds. */
    TL_CHECK_STR(FEED(f, "\xe2\x82"), "");
    TL_CHECK_STR(FEED(f, "\xac"), "P(20ac)");
    TL_CHECK_STR(FEED(f, "\x1b"), "");
    TL_CHECK_STR(FEED(f, "[2J"), "C(2J)");
    /* ESC inside a UTF-8 sequence. */
    TL_CHECK_STR(FEED(f, "\xe2\x82\x1b[2J"), "P(fffd)C(2J)");
    /* UTF-8 inside a CSI is ignored; inside an OSC it is kept. */
    TL_CHECK_STR(FEED(f, "\x1b[1\xc3\xa9m"), "C(1m)");
    TL_CHECK_STR(FEED(f, "\x1b]0;\xc3\xa9\x07"), "O(0;\\xc3\\xa9)");
    /* UTF-8 encoded C1: honoured like xterm. */
    TL_CHECK_STR(FEED(f, "\xc2\x9b" "2J"), "C(2J)");
    TL_CHECK_STR(FEED(f, "\xc2\x9d" "0;t\xc2\x9c"), "O(0;t)");
    TL_CHECK_STR(FEED(f, "\xc2\x85"), "X(85)");

    /* 8-bit mode. */
    f.p.set_utf8(false);
    TL_CHECK_STR(FEED(f, "\x9b" "2J"), "C(2J)");
    TL_CHECK_STR(FEED(f, "\xe9"), "P(e9)");
    TL_CHECK_STR(FEED(f, "\x1b]0;\xe9\x9c"), "O(0;\\xe9)");
    TL_CHECK_STR(FEED(f, "\x90q~\x9c"), "H(q)D(~)U");
    TL_CHECK_STR(FEED(f, "\x85"), "X(85)");
    /* A C1 inside a string aborts it. */
    TL_CHECK_STR(FEED(f, "\x1b]0;a\x85x"), "X(85)R(x)");
    f.p.set_utf8(true);
    TL_CHECK_STR(FEED(f, "\xc3\xa9"), "P(e9)");
}
