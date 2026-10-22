/*
 * ttrc's shell (ttrc/DESIGN.md section 4): the user's own $SHELL on a
 * pty, untouched, with every byte it writes kept in a copy of its screen
 * (a tl::Terminal). ttrc learns when a command has finished and where the
 * shell is from the kernel (tp), never from hooks in the shell.
 *
 * Shell is the process and the copy. ShellView is how the shell is shown:
 * PassThroughView sends its output straight to the terminal; a rendered
 * view (a shell pane, as tmux draws) can be added behind the same
 * interface later (section 4.2).
 */
#ifndef TTRC_SHELL_H
#define TTRC_SHELL_H

#include "../../tl/terminal.h"
#include "../../tp/tp.h"
#include "../../tu/screen.h"
#include "../../tu/term.h"
#include "core/str.h"

namespace ttrc {

/* How the shell quotes a path. */
enum ShellFamily {
    FAMILY_SH,      /* sh, ksh, bash, zsh, dash, ash, jsh */
    FAMILY_CSH,     /* csh, tcsh */
    FAMILY_FISH
};

class Shell : public tl::Host {
public:
    Shell();
    ~Shell();

    /* Start $SHELL (or /bin/sh) in cwd on a pty of this size. */
    bool start(int cols, int rows, const char* cwd, bool login, bool utf8);
    const char* error() const { return error_; }
    int fd() const { return pty_.fd(); }
    int pid() const { return proc_.pid(); }
    bool alive() const { return alive_; }
    ShellFamily family() const { return family_; }
    const char* name() const { return name_; }

    /* What the shell wrote, into the copy; the bytes are also returned in
     * out (for a view to pass through). False once the shell is gone. */
    bool read(char* out, size_t cap, size_t* n);
    void write(const char* data, size_t n);
    void write(const char* s);
    void resize(int cols, int rows);
    /* Let the copy answer the program's queries (cursor position, device
     * attributes) while the real terminal does not see them. */
    void set_answering(bool on) { answering_ = on; }

    /* At its prompt with nothing running: quiet for a moment, its own
     * process group in the terminal's foreground, and (for shells without
     * job control) no children. */
    bool idle(long now_ms);
    long last_output() const { return last_output_; }
    /* Where the shell is, from the kernel. */
    bool cwd(char* buf, size_t n) const;

    /* A `cd` line for this shell, or false if the path cannot be quoted
     * safely for it (a newline for csh). */
    bool cd_command(const char* path, char* out, size_t n) const;
    /* A path quoted as one word for this shell, for the command line. */
    bool quote(const char* path, char* out, size_t n) const;

    tl::Terminal& copy() { return copy_; }
    /* The prompt: the copy's cursor row left of the cursor. */
    ttr::Str prompt();
    /* The command typed after the prompt, if any. */
    ttr::Str typed();
    /* Remember where the prompt ends (call when the shell is idle). */
    void mark_prompt();

    /* Draw rows [from, from + n) of the copy at screen row y. */
    void paint(tu::Screen& s, int from, int y, int n);

    /* tl::Host */
    void write_pty(const uint8_t* data, size_t n);

private:
    tp::Pty pty_;
    tp::Process proc_;
    tl::Terminal copy_;
    bool alive_;
    bool answering_;
    bool job_control_;
    long last_output_;
    int prompt_x_, prompt_y_;
    ShellFamily family_;
    char name_[64];
    char error_[256];
};

/* How the shell gets on screen (section 4.2). */
class ShellView {
public:
    virtual ~ShellView() {}
    /* The shell takes the screen: the terminal shows the copy and gets
     * the shell's modes back. */
    virtual void show() = 0;
    /* ttrc takes the screen back. */
    virtual void hide() = 0;
    /* Output the shell wrote. */
    virtual void output(const char* data, size_t n) = 0;
    virtual bool shown() const = 0;
};

class PassThroughView : public ShellView {
public:
    PassThroughView(Shell& sh, tu::Term& term, tu::Screen& screen);
    void show();
    void hide();
    void output(const char* data, size_t n);
    bool shown() const { return shown_; }

private:
    void apply_modes();

    Shell& sh_;
    tu::Term& term_;
    tu::Screen& screen_;
    bool shown_;
};

} /* namespace ttrc */

#endif
