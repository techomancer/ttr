# ttrt: the ttr terminal

Status: 2026-10-20. Milestones 1-4 below are done: `tl/` through its
milestone 3 plus key, paste and focus encoding, `tp/`, `tw/` milestones
1 and 2, selection and clipboard, terminfo, packages, and the settings
page. Next: the soft and GL renderers (milestone 5).

ttrt is a Motif terminal emulator for IRIX and Linux. It is assembled
from three reusable modules plus a thin application:

| Module | What | Knows about |
|---|---|---|
| [`tl/`](../tl/DESIGN.md) | terminal emulation: bytes to screen state, input encoding | nothing outside itself |
| [`tw/`](../tw/DESIGN.md) | cell-grid drawing engine and the `TwGrid` Motif widget | X11, Motif; not terminals |
| [`tp/`](#4-tp-pty-and-process) | pseudo-terminals and child processes, one implementation file per system | the operating system |
| `src/ttrt/` | the application: window, menus, settings, glue | all of the above and ttr's `core/` |

The decisions below come from taking apart IRIX's own terminal, xwsh,
and from our libtsm-based dash; both are written up in
[XWSH-NOTES.md](XWSH-NOTES.md).

---

## 1. Goals

1. An xterm-class terminal (tl's feature set) that feels at home on an
   Indy: starts fast, scrolls without lag in the 8-bit default visual,
   uses Motif for every widget and the IRIX or ttr colour scheme.
2. The same program on Linux under ttr, with truecolor and good fonts.
3. No setuid, no ifdef forests: system differences live in `tp`'s
   per-system files, display differences in `tw`'s renderers.
4. Reusable pieces: another program can embed a terminal with `tl` +
   `tp` + `TwGrid` and a page of glue.

Non-goals for the first version: tabs (the window manager manages
windows; `ttrt` starts fast enough to open more), session restore,
utmp entries (section 4.5).

---

## 2. Architecture

```
          +-----------------------------------------------------+
          | ttrt application (src/ttrt/)                         |
          |  XmMainWindow: menu bar, TwGrid, XmScrollBar          |
          |  settings (ttr core), resources, command line         |
          +----+----------------+------------------+--------------+
               |                |                  |
     pty bytes |  tl::Terminal  |  tw::Source      |  keys, mouse,
     (XtApp-   |  feed()        |  adapter over    |  resize, paste
     AddInput) v                v  tl::View        v
          +---------+     +-----------+      +-----------+
          | tp::Pty | --> | tl        | ---> | tw        |
          | tp::    | <-- | encoders  | <--- | TwGrid    |
          | Process |     +-----------+      +-----------+
          +---------+
```

One process, one thread, one Xt event loop, like ttr itself.

---

## 3. Event loop and pacing

- The pty master is non-blocking and registered with `XtAppAddInput`.
- **Reading**: on readability, read in 16 KB chunks and feed `tl` until
  the read would block or 256 KB have been consumed in this callback;
  then return to Xt so input events and redraws are not starved (`cat`
  of a large file must not freeze the window). xwsh read 2 KB at a time;
  larger chunks matter more for throughput than for latency.
- **Drawing** is scheduled by `tw` (its section 6): a short delay after
  the first change coalesces echo and prompt redraws, bulk output is
  drawn at most at the frame rate, synchronized output (mode 2026) holds
  frames. This replaces xwsh's 200-byte heuristic with the same idea.
- **Writing** to the pty goes through a queue: key presses and replies
  are written immediately when the pty accepts them; what does not fit
  (a large paste) waits for writability (`XtInputWriteMask`). A paste
  never blocks the event loop.
- **Child exit**: `SIGCHLD` is caught with a self-pipe and
  `XtAppAddSignal` (X11R6 Xt; IRIX's Xt is R6.6-based) so the loop wakes
  up; `tp::Process::poll()` reaps. Reading EOF or `EIO` from the master
  also ends the session. On exit, ttrt closes the window (or, with
  `-hold`, keeps it and shows the exit status in the title).
- **Window close** sends `SIGHUP` to the child's process group, waits
  briefly, then exits.

---

## 4. tp: pty and process

### 4.1 Interface

```
namespace tp {

struct WinSize { int cols, rows, width_px, height_px; };

class Pty {                      /* the master side */
public:
    bool open(const WinSize& size);  /* non-blocking, close-on-exec */
    void close();
    int fd() const;
    const char* slave_name() const;
    bool resize(const WinSize& size);  /* TIOCSWINSZ: the child gets SIGWINCH */
    long read(void* buf, size_t n);    /* >0 bytes, 0 would block, -1 closed */
    long write(const void* buf, size_t n);  /* bytes, 0 would block, -1 error */
    const char* error() const;
};

struct SpawnOptions {
    const char* const* argv;     /* null: the user's shell */
    bool login;                  /* argv[0] gets a leading '-' */
    const char* const* env;      /* "NAME=value" to set; "NAME" to unset */
    const char* cwd;
    bool console;                /* TIOCCONS: receive console output */
    bool utf8;                   /* tty in UTF-8 mode where the system has one */
    int erase;                   /* erase character: 0x7f (default) or 0x08 */
};

class Process {
public:
    bool spawn(Pty& pty, const SpawnOptions& opts);  /* false: error() says why,
                                                         including exec failure */
    int pid() const;
    bool poll();                 /* reap without blocking; true while running */
    bool wait(int timeout_ms);
    bool exited() const;  int exit_code() const;  int signal() const;
    bool signal_group(int sig);  /* kill(-pgid): IRIX has no TIOCSIG */
    const char* error() const;
};

}
```

### 4.2 One file per system, not ifdefs

The portable parts (`tp/pty.cpp`, `tp/process.cpp`) are written against
POSIX calls that every target has: `fork`, `pipe`, `fcntl`, `waitpid`,
`kill`, `execve`, `ioctl(TIOCSWINSZ)`, `tcsetattr`. Everything that
differs sits behind a small internal interface, `tp/sys.h`, implemented
once per system:

| `tp::sys` function | `sys_posix.cpp` (Linux, BSD) | `sys_irix.cpp` |
|---|---|---|
| `open_master` | `posix_openpt`, `grantpt`, `unlockpt`, `ptsname` | `_getpty(O_RDWR\|O_NOCTTY, 0620)` (uses `mkpts` for non-root callers) |
| `child_acquire_tty` | `setsid`, open slave `O_NOCTTY`, `ioctl(TIOCSCTTY)` | `setsid`, open slave **without** `O_NOCTTY`: SVR4 makes it the controlling tty |
| `tune_termios` | `IUTF8`, `VDISCARD`/`VLNEXT`... | full set of control characters (a fresh IRIX pty has SysV ones: DEL for intr), no `IUTF8`, `VDSUSP` |
| `child_close_fds` | loop to `sysconf(_SC_OPEN_MAX)` | loop to `getdtablehi()` |
| `set_console` | `TIOCCONS` | `TIOCCONS` (needs privilege; failure is reported, not fatal) |
| `wait_readable` | `poll` | `poll` from `<sys/poll.h>` |
| `default_shell` | `$SHELL`, `getpwuid`, `/bin/sh` | same |
| `VMIN`/`VTIME` | set (own slots) | not set: they are the `VEOF`/`VEOL` slots on IRIX |

The Makefile picks the implementation by object name
(`TP_SYS=$(B)/tp_sys_posix.o`, overridden to `tp_sys_irix.o` on IRIX), so
neither file contains a single `#ifdef` and each compiles only where its
headers exist. Adding a system (macOS, Solaris) is one new file.

Rules the portable code follows because one system or another forces
them:

- The **environment is built in the parent** (a copy of `environ` with
  the overrides applied) and passed to `execve`; the program is looked
  up in `PATH` in the parent too. The child after `fork` calls nothing
  that is not async-signal-safe, and nobody needs `setenv` (IRIX has
  none).
- **Exec failure is reported**: a close-on-exec pipe carries `errno` from
  the child to the parent if `execve` fails; a successful exec closes it
  with nothing written.
- **Close-on-exec is set with `fcntl`** everywhere (no `O_CLOEXEC`).
- **Signals go to the process group** (`kill(-pid, sig)`): the child is a
  session leader, so its pid is the group id. IRIX has no `TIOCSIG`.
- **The child's signal state is reset** (mask cleared, every handler
  `SIG_DFL`) before exec, so ttrt's handlers do not leak into the shell.
- **The tty modes are set completely** by tp (`tcsetattr` with every
  control character and the usual `ICRNL IXON OPOST ONLCR ISIG ICANON
  ECHO ECHOE ECHOK IEXTEN ECHOCTL ECHOKE` flags), on every system,
  rather than inheriting whatever the pty driver starts with. That is
  the lesson of dash's `VINTR` fix and of xwsh's `TCSETA`.

### 4.3 Reading, EOF and EIO

When the last process holding the slave exits, Linux returns `EIO` from
the master and IRIX returns end of file. `Pty::read` maps both, and
`ECONNRESET`-style variants, to "closed" (-1); `EAGAIN` and `EINTR` to 0.

### 4.4 Slave permissions

The slave is mode 0620 and group `tty` where the system has one, as
`grantpt` and IRIX's `mkpts` set it, so `mesg y` and `write` work. ttrt
is not setuid; it does not `chown` the slave itself (xwsh did, as root).

### 4.5 utmp

Writing utmp/wtmp needs root on both systems (xwsh is setuid root for
this). ttrt does not; `who` will not list its sessions. A small helper
in the style of libutempter can be added later if wanted.

---

## 5. The window

- `XmMainWindow` with a menu bar (File: New Window, Close; Edit: Copy,
  Paste, Select All, Clear Scrollback; View: Font, Bigger, Smaller,
  Reset; Terminal: Reset, Full Reset; Help), the `TwGrid` as work area
  and an `XmScrollBar` bound to tl's viewport (scroll back with the bar,
  Shift+PageUp/Down, the wheel).
- The menu bar can be hidden (`-nomenu`, a resource) for an xterm-like
  bare window; then a popup menu on Ctrl+button 3 has the same entries.
- Title and icon name follow OSC 0/1/2 (`tl::Host::title_changed`).
- The shell's size hints use the cell size as resize increments, so
  window managers resize in whole cells and show "80x24" while resizing.
- Bell: `XkbBell` (as xwsh) or a visual bell (`-vb`).

## 6. Input

- `TwGrid`'s input callback gives keysym, modifiers and text; ttrt
  translates the keysym to `tl::Key` with a table (cursor keys, keypad,
  function keys, editing keys) and lets tl encode it in the current
  modes (legacy xterm or the kitty protocol). Text goes through the
  widget's input method (`XmIm`), so compose and dead keys work.
- Configurable Backspace (DEL or BS) and Meta (ESC prefix or 8th bit),
  default DEL and ESC prefix as xterm and xwsh do.
- Mouse: tl's mouse modes when the application asks; otherwise
  selection (button 1 drag, double-click word, triple-click line,
  button 3 extend) on tl anchors, so selections survive scrolling, and
  button 2 pastes PRIMARY.
- Wheel: viewport scroll, or arrow keys on the alternate screen (mode
  1007), or mouse reports when tracking.

## 7. Selection and clipboard

- PRIMARY: owned on selection end with Xt's selection functions; the
  text comes from `tl::Terminal::text` with soft wraps joined.
- CLIPBOARD: Edit > Copy and Ctrl+Shift+C; Ctrl+Shift+V pastes. Both
  selections are plain ICCCM selections through Xt (not `XmClipboard`),
  offering UTF8_STRING, STRING (Latin-1) and TEXT; pasting asks for
  UTF8_STRING and falls back to STRING, which is all xwsh offers.
- Paste is bracketed (mode 2004) when the application asks and is fed
  through the write queue (section 3).
- OSC 52 set is allowed, get is denied by default (setting).

## 8. Fonts, colours, renderer

All through `tw` (its sections 4 and 5): the renderer is chosen per
display (core on 8-bit Indy visuals, GL on IMPACT and VPro, soft or GL on
Linux), fonts default to the IRIX `FixedWidthFont` scheme font (or ttr's
setting) with the bundled misc-fixed fonts as Unicode fallback. Colours:
tl's palette with the default foreground and background taken from the
active ttr colour scheme (or the IRIX scheme), so ttrt matches the
desktop like xwsh's `winterm` resources do.

`TERM` is `ttrt` when the terminfo entry is installed (shipped as
`data/ttrt.ti`, standard capabilities only so that both ncurses `tic` and
IRIX's SVR4 `tic` compile it; `make install` runs `tic`), else
`xterm-256color`, else `xterm`. When the entry is found in `$TERMINFO` or
`~/.terminfo`, ttrt passes `TERMINFO` to the shell: IRIX's curses does not
look in `~/.terminfo`. `COLORTERM=truecolor` only with a TrueColor visual.

## 9. Settings and command line

Settings live in ttr's settings store (a "Terminal" page in ttr's
settings dialog) and as X resources (`TTRt*font`, `TTRt*renderer`,
...), so ttrt also works stand-alone on a desktop without ttr.

Implemented 2026-10-20: `src/core/termsettings.h` lists the settings
(name and default) for both sides. In ttr's settings file they are
`ttrt.<name>` lines; ttrt puts them into its resource database as
`<app name>.<name>` (tight binding, so they reach the application's own
resources and no Motif widget), over X resources, then parses its command
line again with the same option table so the command line wins over the
file. Options therefore name the application's resources (`.font`, not
`*font`): `-fg` colours the terminal text and not the menu bar. The
Terminal page (`src/shell/terminalpage.cpp`) writes only values that
differ from the default and keeps a value's original text ("wheat",
"9x15") unless the user changed it, so hand-written lines and X
resources survive OK. ttrt reads the settings when a window opens;
running windows keep theirs. Command
line options follow xterm and xwsh where they overlap: `-e`, `-fn`,
`-fg`, `-bg`, `-geometry`, `-title`, `-n`, `-sl`, `-ls`, `-hold`,
`-console`, `-vb`, plus `-renderer`.

## 10. Testing

- `tl` and `tp` have unit tests that run on Linux and in the IRIX guest
  (`make tl-test tp-test`, `tools/irix-test.sh`).
- `tw` has golden-image tests per renderer (its section 9).
- ttrt end to end: the headless driver from tl's design runs vttest and
  esctest2 against `tl` + `tp` without a window; screenshots of ttrt on
  the emulated Indy (`tools/irix-shot.sh`) check the real window.

## 11. Milestones

1. **tp** with Linux and IRIX implementations and tests. (Done: 71
   checks pass on Linux with g++, clang++, ASan/UBSan and on IRIX with
   MIPSpro.)
2. **tw milestone 1** (core renderer, TwGrid) and **ttrt 0.1**: a window
   that runs a shell on Linux and on the Indy, keys, resize, scrollbar,
   title. (Done.)
3. Selection, clipboard, paste, mouse modes; tl's input encoders. (Done:
   PRIMARY and CLIPBOARD go through Xt selections with UTF8_STRING,
   STRING and TEXT targets, which IRIX's xwsh and Motif clients read; a
   selection is dropped when text under it changes, as in xterm.)
4. Unicode on IRIX (tw milestone 2), settings page, terminfo, install.
   (Done.)
5. Soft and GL renderers (tw milestones 3-4), console mode, `-hold`.
