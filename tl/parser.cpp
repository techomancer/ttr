#include "parser.h"

#include <stdlib.h>
#include <string.h>

namespace tl {

/* ---- Params / Seq ------------------------------------------------------ */

void Params::clear()
{
    count = 0;
    overflow = false;
}

int Params::top_index(int i) const
{
    int n = 0;
    for (int e = 0; e < count; e++) {
        if (is_sub[e])
            continue;
        if (n == i)
            return e;
        n++;
    }
    return -1;
}

int Params::top(int i, int dflt) const
{
    int e = top_index(i);
    if (e < 0 || value[e] == 0)
        return dflt;
    return value[e];
}

int Params::top_count() const
{
    int n = 0;
    for (int e = 0; e < count; e++)
        if (!is_sub[e])
            n++;
    return n;
}

int Params::subs_of(int e) const
{
    int n = 0;
    for (int i = e + 1; i < count && is_sub[i]; i++)
        n++;
    return n;
}

void Seq::clear()
{
    priv = 0;
    n_intermediates = 0;
    final_byte = 0;
    params.clear();
}

/* ---- ParserSink -------------------------------------------------------- */

void ParserSink::print_run(const uint8_t* s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        print(s[i]);
}

/* ---- UTF-8 ------------------------------------------------------------- */

/* Bjoern Hoehrmann's DFA (MIT licence, bjoern.hoehrmann.de/utf-8/decoder):
 * rejects overlong forms, surrogates and code points above U+10FFFF. */
static const uint8_t k_utf8d[] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,
    0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
    12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
    12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
    12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
    12,36,12,12,12,12,12,12,12,12,12,12,
};

enum { UTF8_ACCEPT = 0, UTF8_REJECT = 12 };

static inline uint32_t utf8_step(uint32_t* state, uint32_t* cp, uint8_t b)
{
    uint32_t type = k_utf8d[b];
    *cp = (*state != UTF8_ACCEPT) ? (b & 0x3fu) | (*cp << 6) : (0xffu >> type) & b;
    *state = k_utf8d[256 + *state + type];
    return *state;
}

/* ---- Parser ------------------------------------------------------------ */

Parser::Parser()
    : sink_(0), state_(GROUND), utf8_(true), seq_ignore_(false),
      param_open_(false), param_acc_(0), param_acc_sub_(false),
      str_(0), str_len_(0), str_cap_(0), str_limit_(0), str_truncated_(false),
      osc_max_(1u << 20), apc_max_(16u << 20),
      dcs_len_(0), utf_state_(UTF8_ACCEPT), utf_cp_(0)
{
    seq_.clear();
}

Parser::~Parser()
{
    free(str_);
}

void Parser::set_utf8(bool on)
{
    utf8_ = on;
    utf_state_ = UTF8_ACCEPT;
}

void Parser::set_string_limits(size_t osc_max, size_t apc_max)
{
    osc_max_ = osc_max;
    apc_max_ = apc_max;
}

void Parser::reset()
{
    if (state_ == DCS_PASSTHROUGH) {
        flush_dcs();
        if (sink_)
            sink_->dcs_unhook(true);
    }
    state_ = GROUND;
    utf_state_ = UTF8_ACCEPT;
    str_len_ = 0;
    dcs_len_ = 0;
}

void Parser::feed(const uint8_t* data, size_t n)
{
    size_t i = 0;
    while (i < n) {
        if (state_ == GROUND && utf_state_ == UTF8_ACCEPT) {
            /* Fast path: a run of ASCII printables. */
            size_t j = i;
            while (j < n && data[j] >= 0x20 && data[j] < 0x7f)
                j++;
            if (j > i) {
                if (sink_)
                    sink_->print_run(data + i, j - i);
                i = j;
                continue;
            }
        }
        byte(data[i]);
        i++;
    }
    if (state_ == DCS_PASSTHROUGH)
        flush_dcs();
}

void Parser::enter(State s)
{
    state_ = s;
    switch (s) {
    case ESCAPE:
    case CSI_ENTRY:
    case DCS_ENTRY:
        seq_.clear();
        seq_ignore_ = false;
        param_open_ = false;
        param_acc_ = 0;
        param_acc_sub_ = false;
        break;
    case OSC_STRING:
        str_reset(osc_max_);
        break;
    case APC_STRING:
        str_reset(apc_max_);
        break;
    default:
        break;
    }
}

/* ST, or whatever ends the string, was seen. normal_end is false for
 * CAN, SUB and C1 controls. */
void Parser::leave_string(bool normal_end)
{
    switch (state_) {
    case OSC_STRING:
        if (sink_ && normal_end)
            sink_->osc_dispatch(str_, str_len_, str_truncated_);
        break;
    case APC_STRING:
        if (sink_ && normal_end)
            sink_->apc_dispatch(str_, str_len_, str_truncated_);
        break;
    case DCS_PASSTHROUGH:
        flush_dcs();
        if (sink_)
            sink_->dcs_unhook(!normal_end);
        break;
    default:
        break;
    }
}

void Parser::collect(uint8_t c)
{
    if (c >= 0x3c && c <= 0x3f) {
        seq_.priv = c;
        return;
    }
    if (seq_.n_intermediates < TL_MAX_INTERMEDIATES)
        seq_.intermediates[seq_.n_intermediates++] = c;
    else
        seq_ignore_ = true;
}

void Parser::param_digit(uint8_t c)
{
    param_open_ = true;
    param_acc_ = param_acc_ * 10 + (c - '0');
    if (param_acc_ > TL_PARAM_MAX)
        param_acc_ = TL_PARAM_MAX;
}

/* Push the parameter being built, if any. */
void Parser::param_finish()
{
    if (!param_open_)
        return;
    Params& p = seq_.params;
    if (p.count < TL_MAX_PARAMS) {
        p.value[p.count] = (uint16_t)param_acc_;
        p.is_sub[p.count] = param_acc_sub_ ? 1 : 0;
        p.count++;
    } else {
        p.overflow = true;
    }
    param_open_ = false;
    param_acc_ = 0;
    param_acc_sub_ = false;
}

void Parser::param_sep(bool sub)
{
    /* "CSI ; 5 H": the empty first parameter still counts. */
    param_open_ = true;
    param_finish();
    param_open_ = true;
    param_acc_sub_ = sub;
}

void Parser::dispatch_esc(uint8_t c)
{
    seq_.final_byte = c;
    if (sink_ && !seq_ignore_)
        sink_->esc_dispatch(seq_);
    state_ = GROUND;
}

void Parser::dispatch_csi(uint8_t c)
{
    param_finish();
    seq_.final_byte = c;
    if (sink_ && !seq_ignore_)
        sink_->csi_dispatch(seq_);
    state_ = GROUND;
}

void Parser::hook(uint8_t c)
{
    param_finish();
    seq_.final_byte = c;
    dcs_len_ = 0;
    if (seq_ignore_) {
        state_ = DCS_IGNORE;
        return;
    }
    state_ = DCS_PASSTHROUGH;
    if (sink_)
        sink_->dcs_hook(seq_);
}

void Parser::flush_dcs()
{
    if (dcs_len_ && sink_)
        sink_->dcs_put(dcs_buf_, dcs_len_);
    dcs_len_ = 0;
}

void Parser::str_reset(size_t limit)
{
    str_len_ = 0;
    str_limit_ = limit;
    str_truncated_ = false;
}

void Parser::str_put(uint8_t c)
{
    if (state_ == DCS_PASSTHROUGH) {
        if (dcs_len_ == DCS_BUF)
            flush_dcs();
        dcs_buf_[dcs_len_++] = c;
        return;
    }
    if (str_len_ >= str_limit_) {
        str_truncated_ = true;
        return;
    }
    if (str_len_ == str_cap_) {
        size_t ncap = str_cap_ ? str_cap_ * 2 : 256;
        if (ncap > str_limit_)
            ncap = str_limit_;
        uint8_t* p = (uint8_t*)realloc(str_, ncap);
        if (!p) {
            str_truncated_ = true;
            return;
        }
        str_ = p;
        str_cap_ = ncap;
    }
    str_[str_len_++] = c;
}

/* A C1 control (0x80-0x9F), from an 8-bit byte or a decoded UTF-8 C1. */
void Parser::anywhere_c1(uint8_t c)
{
    bool in_string = state_ == OSC_STRING || state_ == APC_STRING ||
                     state_ == SOS_PM_STRING || state_ == DCS_PASSTHROUGH ||
                     state_ == DCS_IGNORE;
    if (c == 0x9c) {            /* ST */
        if (in_string)
            leave_string(true);
        state_ = GROUND;
        return;
    }
    if (in_string)
        leave_string(false);
    switch (c) {
    case 0x90: enter(DCS_ENTRY); break;
    case 0x9b: enter(CSI_ENTRY); break;
    case 0x9d: enter(OSC_STRING); break;
    case 0x9f: enter(APC_STRING); break;
    case 0x98: case 0x9e: enter(SOS_PM_STRING); break;
    default:
        state_ = GROUND;
        if (sink_)
            sink_->execute(c);
        break;
    }
}

void Parser::utf8_byte(uint8_t c)
{
    uint32_t prev = utf_state_;
    uint32_t r = utf8_step(&utf_state_, &utf_cp_, c);
    if (r == UTF8_ACCEPT) {
        char_in_state(utf_cp_);
        return;
    }
    if (r != UTF8_REJECT)
        return;                     /* need more bytes */
    /* Invalid: one U+FFFD per maximal invalid subsequence. */
    utf_state_ = UTF8_ACCEPT;
    char_in_state(0xfffd);
    if (prev != UTF8_ACCEPT)
        byte(c);                    /* this byte may start a new sequence */
}

/* A decoded character (UTF-8 mode, >= 0x80) in the current state. */
void Parser::char_in_state(uint32_t cp)
{
    if (cp >= 0x80 && cp <= 0x9f) {
        anywhere_c1((uint8_t)cp);
        return;
    }
    switch (state_) {
    case GROUND:
        if (sink_)
            sink_->print(cp);
        break;
    case OSC_STRING:
    case APC_STRING:
    case DCS_PASSTHROUGH: {
        /* Keep the original bytes. */
        uint8_t b[4];
        int n;
        if (!utf8_) {
            str_put((uint8_t)cp);
            break;
        }
        if (cp < 0x800) {
            b[0] = (uint8_t)(0xc0 | (cp >> 6));
            b[1] = (uint8_t)(0x80 | (cp & 0x3f));
            n = 2;
        } else if (cp < 0x10000) {
            b[0] = (uint8_t)(0xe0 | (cp >> 12));
            b[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
            b[2] = (uint8_t)(0x80 | (cp & 0x3f));
            n = 3;
        } else {
            b[0] = (uint8_t)(0xf0 | (cp >> 18));
            b[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3f));
            b[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
            b[3] = (uint8_t)(0x80 | (cp & 0x3f));
            n = 4;
        }
        for (int i = 0; i < n; i++)
            str_put(b[i]);
        break;
    }
    default:
        /* Inside an escape or control sequence: ignored, like DEL. */
        break;
    }
}

void Parser::byte(uint8_t c)
{
    /* Bytes above 0x7F: UTF-8, or 8-bit controls and GR characters. */
    if (c >= 0x80) {
        if (utf8_) {
            utf8_byte(c);
            return;
        }
        if (c <= 0x9f) {
            anywhere_c1(c);
            return;
        }
        char_in_state(c);           /* GR: like GL in every state */
        return;
    }
    if (utf_state_ != UTF8_ACCEPT) {
        /* An ASCII byte inside a UTF-8 sequence: the sequence is invalid. */
        utf_state_ = UTF8_ACCEPT;
        char_in_state(0xfffd);
    }

    /* Anywhere. */
    if (c == 0x1b) {
        switch (state_) {
        case OSC_STRING:
        case APC_STRING:
            /* xterm: ESC ends the string; "ESC \" then dispatches an ST
             * that the interpreter ignores. */
            leave_string(true);
            break;
        case DCS_PASSTHROUGH:
            leave_string(true);
            break;
        default:
            break;
        }
        enter(ESCAPE);
        return;
    }
    if (c == 0x18 || c == 0x1a) {
        leave_string(false);
        state_ = GROUND;
        if (sink_)
            sink_->execute(c);
        return;
    }

    switch (state_) {
    case GROUND:
        if (c < 0x20) {
            if (sink_)
                sink_->execute(c);
        } else if (c < 0x7f) {
            if (sink_)
                sink_->print(c);
        }
        break;

    case ESCAPE:
        if (c < 0x20) {
            if (sink_) sink_->execute(c);
        } else if (c < 0x30) {
            collect(c);
            state_ = ESCAPE_INTERMEDIATE;
        } else if (c == 0x50) {
            enter(DCS_ENTRY);
        } else if (c == 0x5b) {
            enter(CSI_ENTRY);
        } else if (c == 0x5d) {
            enter(OSC_STRING);
        } else if (c == 0x5f) {
            enter(APC_STRING);
        } else if (c == 0x58 || c == 0x5e) {
            enter(SOS_PM_STRING);
        } else if (c < 0x7f) {
            dispatch_esc(c);
        }
        break;

    case ESCAPE_INTERMEDIATE:
        if (c < 0x20) {
            if (sink_) sink_->execute(c);
        } else if (c < 0x30) {
            collect(c);
        } else if (c < 0x7f) {
            dispatch_esc(c);
        }
        break;

    case CSI_ENTRY:
        if (c < 0x20) {
            if (sink_) sink_->execute(c);
        } else if (c < 0x30) {
            collect(c);
            state_ = CSI_INTERMEDIATE;
        } else if (c <= 0x39) {
            param_digit(c);
            state_ = CSI_PARAM;
        } else if (c == 0x3a || c == 0x3b) {
            param_sep(c == 0x3a);
            state_ = CSI_PARAM;
        } else if (c <= 0x3f) {
            collect(c);
            state_ = CSI_PARAM;
        } else if (c < 0x7f) {
            dispatch_csi(c);
        }
        break;

    case CSI_PARAM:
        if (c < 0x20) {
            if (sink_) sink_->execute(c);
        } else if (c < 0x30) {
            param_finish();
            collect(c);
            state_ = CSI_INTERMEDIATE;
        } else if (c <= 0x39) {
            param_digit(c);
        } else if (c == 0x3a || c == 0x3b) {
            param_sep(c == 0x3a);
        } else if (c <= 0x3f) {
            state_ = CSI_IGNORE;
        } else if (c < 0x7f) {
            dispatch_csi(c);
        }
        break;

    case CSI_INTERMEDIATE:
        if (c < 0x20) {
            if (sink_) sink_->execute(c);
        } else if (c < 0x30) {
            collect(c);
        } else if (c <= 0x3f) {
            state_ = CSI_IGNORE;
        } else if (c < 0x7f) {
            dispatch_csi(c);
        }
        break;

    case CSI_IGNORE:
        if (c < 0x20) {
            if (sink_) sink_->execute(c);
        } else if (c >= 0x40 && c < 0x7f) {
            state_ = GROUND;
        }
        break;

    case DCS_ENTRY:
        if (c < 0x20 || c == 0x7f) {
            /* ignored */
        } else if (c < 0x30) {
            collect(c);
            state_ = DCS_INTERMEDIATE;
        } else if (c <= 0x39) {
            param_digit(c);
            state_ = DCS_PARAM;
        } else if (c == 0x3a || c == 0x3b) {
            param_sep(c == 0x3a);
            state_ = DCS_PARAM;
        } else if (c <= 0x3f) {
            collect(c);
            state_ = DCS_PARAM;
        } else {
            hook(c);
        }
        break;

    case DCS_PARAM:
        if (c < 0x20 || c == 0x7f) {
            /* ignored */
        } else if (c < 0x30) {
            param_finish();
            collect(c);
            state_ = DCS_INTERMEDIATE;
        } else if (c <= 0x39) {
            param_digit(c);
        } else if (c == 0x3a || c == 0x3b) {
            param_sep(c == 0x3a);
        } else if (c <= 0x3f) {
            state_ = DCS_IGNORE;
        } else {
            hook(c);
        }
        break;

    case DCS_INTERMEDIATE:
        if (c < 0x20 || c == 0x7f) {
            /* ignored */
        } else if (c < 0x30) {
            collect(c);
        } else if (c <= 0x3f) {
            state_ = DCS_IGNORE;
        } else {
            hook(c);
        }
        break;

    case DCS_PASSTHROUGH:
        if (c != 0x7f)
            str_put(c);
        break;

    case DCS_IGNORE:
    case SOS_PM_STRING:
        break;

    case OSC_STRING:
    case APC_STRING:
        if (c == 0x07) {
            leave_string(true);
            state_ = GROUND;
        } else if (c >= 0x20 && c < 0x7f) {
            str_put(c);
        }
        break;
    }
}

} /* namespace tl */
