/*
 * tl/parser.h - from bytes to terminal actions.
 *
 * Paul Williams' DEC ANSI parser (vt100.net/emu/dec_ansi_parser) with the
 * deviations every current emulator makes: colon sub-parameters, APC
 * strings collected like OSC strings, UTF-8 decoded in front of the state
 * machine, bounded string buffers. See DESIGN.md section 4.
 *
 * The parser knows nothing about what the sequences mean. It hands them
 * to a ParserSink, which the interpreter implements.
 */
#ifndef TL_PARSER_H
#define TL_PARSER_H

#include "port.h"

namespace tl {

enum {
    TL_MAX_PARAMS = 32,
    TL_MAX_INTERMEDIATES = 4,
    TL_PARAM_MAX = 65535
};

/* Numeric parameters of a CSI or DCS sequence. Entries are stored in the
 * order sent. An entry introduced by ':' is a sub-parameter of the
 * top-level parameter before it (SGR 38:2::r:g:b, SGR 4:3). Empty
 * parameters are 0. */
struct Params {
    uint16_t value[TL_MAX_PARAMS];
    uint8_t is_sub[TL_MAX_PARAMS];
    int count;
    bool overflow;      /* more than TL_MAX_PARAMS were sent; the rest were dropped */

    void clear();
    /* i-th top-level parameter (sub-parameters skipped); dflt if it is
     * missing or 0. */
    int top(int i, int dflt) const;
    int top_count() const;
    /* Entry index of the i-th top-level parameter, or -1. */
    int top_index(int i) const;
    /* Number of sub-parameters that follow entry e. */
    int subs_of(int e) const;
};

/* The header of an ESC, CSI or DCS sequence. */
struct Seq {
    uint8_t priv;                              /* '<', '=', '>', '?' or 0 */
    uint8_t intermediates[TL_MAX_INTERMEDIATES];
    int n_intermediates;
    uint8_t final_byte;
    Params params;

    void clear();
    /* First intermediate or 0. Most sequences have at most one. */
    uint8_t intermediate() const { return n_intermediates ? intermediates[0] : 0; }
};

/* What the parser produces. Every member has an empty default so that a
 * sink implements only what it needs. Byte strings handed over are valid
 * only during the call. */
class ParserSink {
public:
    virtual ~ParserSink() {}

    /* A printable character. In UTF-8 mode a code point; in 8-bit mode the
     * byte (0x20-0x7E, 0xA0-0xFF), to be mapped through the character sets
     * by the interpreter. Invalid UTF-8 arrives as U+FFFD. */
    virtual void print(uint32_t) {}
    /* A run of plain ASCII printables (0x20-0x7E). The default calls
     * print() for each byte; interpreters override it for speed. */
    virtual void print_run(const uint8_t* s, size_t n);
    /* A C0 or C1 control: 0x00-0x1F, 0x7F never, 0x80-0x9F. */
    virtual void execute(uint8_t) {}
    virtual void esc_dispatch(const Seq&) {}
    virtual void csi_dispatch(const Seq&) {}
    virtual void dcs_hook(const Seq&) {}
    virtual void dcs_put(const uint8_t*, size_t) {}
    /* aborted: ended by CAN, SUB, a C1 control or reset rather than ST. */
    virtual void dcs_unhook(bool /*aborted*/) {}
    /* The whole string, without introducer and terminator. truncated: it
     * was longer than the limit and cut. */
    virtual void osc_dispatch(const uint8_t*, size_t, bool /*truncated*/) {}
    virtual void apc_dispatch(const uint8_t*, size_t, bool /*truncated*/) {}
};

class Parser {
public:
    Parser();
    ~Parser();

    void set_sink(ParserSink* sink) { sink_ = sink; }
    /* UTF-8 (default) or 8-bit single-byte input. */
    void set_utf8(bool on);
    bool utf8() const { return utf8_; }
    /* Longest OSC and APC strings kept (bytes). Longer strings are cut and
     * dispatched with truncated = true. Defaults: 1 MiB and 16 MiB. */
    void set_string_limits(size_t osc_max, size_t apc_max);

    void feed(const uint8_t* data, size_t n);
    /* Back to ground; a DCS in progress is unhooked as aborted, strings are
     * dropped. */
    void reset();
    bool in_ground() const { return state_ == GROUND && utf_state_ == 0; }

private:
    enum State {
        GROUND,
        ESCAPE,
        ESCAPE_INTERMEDIATE,
        CSI_ENTRY,
        CSI_PARAM,
        CSI_INTERMEDIATE,
        CSI_IGNORE,
        DCS_ENTRY,
        DCS_PARAM,
        DCS_INTERMEDIATE,
        DCS_PASSTHROUGH,
        DCS_IGNORE,
        OSC_STRING,
        APC_STRING,
        SOS_PM_STRING
    };

    void byte(uint8_t c);
    void anywhere_c1(uint8_t c);
    void enter(State s);
    void leave_string(bool normal_end);
    void collect(uint8_t c);
    void param_digit(uint8_t c);
    void param_sep(bool sub);
    void param_finish();
    void dispatch_esc(uint8_t c);
    void dispatch_csi(uint8_t c);
    void hook(uint8_t c);
    void str_put(uint8_t c);
    void str_reset(size_t limit);
    void utf8_byte(uint8_t c);
    void char_in_state(uint32_t cp);
    void flush_dcs();

    ParserSink* sink_;
    State state_;
    bool utf8_;
    Seq seq_;
    bool seq_ignore_;           /* too many intermediates: dispatch as no-op */
    bool param_open_;           /* a parameter is being built */
    uint32_t param_acc_;
    bool param_acc_sub_;

    uint8_t* str_;              /* OSC / APC buffer */
    size_t str_len_;
    size_t str_cap_;
    size_t str_limit_;
    bool str_truncated_;
    size_t osc_max_;
    size_t apc_max_;

    enum { DCS_BUF = 512 };
    uint8_t dcs_buf_[DCS_BUF];  /* dcs_put is batched */
    size_t dcs_len_;

    uint32_t utf_state_;        /* Hoehrmann decoder state, 0 = accept */
    uint32_t utf_cp_;

    Parser(const Parser&);
    Parser& operator=(const Parser&);
};

} /* namespace tl */

#endif
