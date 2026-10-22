# ttrc: To The Roots Commander

Status: design reviewed 2026-10-21 (decisions in section 12).
Milestones 1 to 5 are implemented; section 11 lists what is left.
Milestone 1 implemented 2026-10-21: tu (terminfo reader and `tparm`, tiers,
screen diff, key and mouse input; 97 checks, also run on IRIX), tf's
local layer and paths (45 checks), and two browsable panels, run in
ttrt and xterm on Linux and in winterm on the emulated Indy.

Milestone 2 implemented 2026-10-22: the shell on a pty with its screen
copy (`src/ttrc/shell.cpp`, `ShellView` with pass-through), Ctrl-O both
ways (also around vi), command completion from the foreground process
group and, for shells without job control, the process table; the cwd
from `/proc` on Linux and `PIOCCKPTOPENCWD` on IRIX; lazy `cd`; the
shell's prompt on the command line; typed text carried both ways;
shortened panels over the shell's output. `make ttrc-test` drives ttrc
on a pty through tl (`src/ttrc/test/`) under every shell present: bash,
zsh, busybox ash on Linux; sh, ksh, jsh, csh, tcsh and bash on IRIX, all
passing.

Milestones 3 to 5 implemented 2026-10-22:

- 3: tu's widgets (`tu/widgets.h`: labels, input lines with history,
  check boxes, radio groups, buttons, lists, a progress bar, dialogs
  with shadows, a menu bar with pull-downs); tf's jobs (`tf/ops.h`: copy,
  move, delete in steps, with overwrite and error questions, totals
  first); F5 to F8, `+` `-` `*`, F9 menus, chmod, chown, symlink, the
  hotlist, find file (searching between key presses), compare
  directories, the F2 user menu and file associations with mc's `%`
  macros, Options > Configuration and Appearance (themes classic,
  midnight, dark, mono), settings in `~/.config/ttrc/settings`.
- 4: tf's VFS (`tf/vfs.h`, `tf/archive.h`): mc's path notation, a mount
  cache, tar (GNU and pax, plain, gzip, bzip2, compress), zip (stored,
  deflated, Zip64), single gz/bz2/Z files; names that would leave an
  archive are dropped. zlib 1.3.1 and bzip2 1.0.8 are submodules,
  compiled through wrappers in `tf/third/` (zlib needs one per inflate
  source: `inftrees.h` has no include guard). The `.Z` decoder is ours.
  Compressed and nested archives are read through a spool, an unlinked
  temporary file.
- 5: the viewer and editor (`src/ttrc/viewer.cpp`, `editor.cpp`) on tu,
  reading through tf.

Tests: tu 100 checks, tf 204 (with archive fixtures from
`tools/make-tf-fixtures.sh` in `tf/test/data`), and 8 ttrc screen scripts
under every shell on both systems.

Found while testing on IRIX: a stock IRIX 6.5 X server has no
`-dec-dectech` fonts, so xwsh and winterm show DEC line drawing as plain
letters. tu therefore gives iris-ansi colours (missing from its terminfo
entry) but not line drawing, and frames there are ASCII.

ttrc is an orthodox file manager in the line of Norton Commander, Volkov
Commander and Midnight Commander: two panels, a command line under them,
a function key bar, and a shell that is always there. It runs in any
terminal: winterm, xwsh and the console on IRIX, xterm, ttrt and the
Linux console. It needs neither ttr nor X.

The goal is mc's feature set without mc's problems. These are the ones
that matter to us:

- **The subshell is hijacked.** mc rewrites `PS1`, `PROMPT_COMMAND`,
  `precmd` and `HISTCONTROL`, then synchronizes with `kill -STOP $$`. It
  breaks prompt frameworks, deadlocks or stalls for 10 seconds when a
  program in between does not cooperate, and does not work at all with
  IRIX's `sh` and `csh` ([docs/shell.md](../docs/shell.md) section 10).
- **Ctrl-O depends on the terminal.** Getting the shell's screen back
  relies on the alternate screen (`smcup`/`rmcup`) or a setuid helper
  (`cons.saver`) on the Linux console. Elsewhere the output is lost.
- **The prompt is scraped** from the raw pty bytes, which breaks on
  colour prompts and multi-line prompts.
- **It is big, slow to build, and its IRIX ports do not work well**:
  glib, S-Lang or ncurses, and autotools.

## 1. Goals and non-goals

Goals:

- The classic two-panel interface and NC/mc keys (section 7). Someone
  who knows mc can use ttrc right away.
- **Any shell, untouched.** ttrc starts the user's `$SHELL` with no rc
  file, no prompt and no hooks of its own. sh, ksh, csh and tcsh on IRIX
  work; so do bash, zsh, fish, and a Python REPL.
- **The shell's screen is never lost.** ttrc keeps its own copy of the
  shell's screen and redraws it on any terminal.
- **Every file operation goes through a VFS** (section 5). Tar, zip,
  gzip, bzip2 and compress files open like directories.
- **Standalone and small.** One program, `ttrc`, with no X, no glib,
  and no curses at all: terminfo is read by our own code.
- **Plain terminals work, good terminals shine.** A vt100 or winterm
  gets a clean, fast interface. ttrt and modern Linux terminals get true
  colour, real line drawing, shadows, unambiguous keys and more
  (section 3.4), without the application code knowing the difference.
- **The framework's rules**: C++98 for MIPSpro 7.4 and
  `g++ -std=c++98 -pedantic`; no exceptions, RTTI, STL or iostreams;
  plain make; per-system code in separate files, not `#ifdef`s
  (as in `tp/`); third-party code as pinned submodules.

Not in the first version: the internal viewer and editor, writing into
archives, network file systems (FTP, SFTP), background file operations
in a separate process.

## 2. The parts

ttrc is built from three existing modules and two new ones, the same
way ttrt is.

| Module | What it is | New? |
|---|---|---|
| `tp/` | ptys and processes, one file per system | existing; gains two calls (section 6.3) |
| `tl/` | the terminal emulator library | existing; ttrc uses it for its copy of the shell's screen |
| `tu/` | **terminal UI**: terminfo, a cell screen with minimal updates, key and mouse input, widgets | new, reusable |
| `tf/` | **file systems**: paths, the VFS interface, local files, archive and compression layers, copy/move/delete jobs | new, reusable |
| `src/ttrc/` | the application: panels, command line, dialogs, viewer, settings | new |

`tu` and `tf` follow `tl`'s rules: they include nothing from `src/` and
nothing from X. Other tools can use them, for example a future `tv`
viewer or ttr's own dialogs on a console.

```
             +--------------------------------------------------+
             | ttrc (src/ttrc/)                                 |
             |  panels, command line, menus, dialogs, viewer     |
             +-----+--------------+--------------+--------------+
                   |              |              |
             tu::Screen      tf::Fs / Job     subshell
             (draw, keys)    (all file I/O)   (tp::Pty + tl::Terminal)
                   |              |              |
             real terminal   local, tar, zip,   the user's shell
             (terminfo)      gz, bz2, Z
```

One process, one thread, one `select()` loop over the terminal, the
shell's pty and a self-pipe for signals. Long file operations run in
steps from that loop, so the screen stays live and Esc cancels them.

## 3. tu: the terminal UI library

### 3.1 Output

- **Terminfo, read by tu itself.** No curses library is linked. tu
  finds the compiled entry (`$TERMINFO`, `~/.terminfo`, `$TERMINFO_DIRS`,
  `/usr/share/terminfo`, `/usr/share/lib/terminfo` on IRIX,
  `/usr/lib/terminfo`, `/etc/terminfo`; both the `x/xterm` and the hex
  `78/xterm` layouts) and parses it. The format is the same everywhere
  and little-endian on every system: checked, IRIX's files and Linux's
  vt100 entry start with the legacy magic 0432, and ncurses' newer
  entries (Linux xterm-256color) with 01036, which only widens numbers to
  32 bits. The extended section (ncurses user capabilities such as
  `RGB`, `Tc`, `Smulx`, `Ss`, `Sync`) is read too: it is how terminfo
  describes the modern features of section 3.4. `tparm`'s small stack
  language (`%p1%d`, `%i`, `%?...%t...%e...%;`) is implemented in tu. If
  no entry is found, tu has built-in entries for xterm-256color, ttrt,
  iris-ansi and vt100.
- **A cell screen and a diff.** The application draws into a back
  buffer of cells (code point, attributes, colours). `flush()` compares
  it with what the terminal shows and sends the shortest update: cursor
  motion chosen from `cup`, `hpa`, relative moves and plain overwrite,
  attribute changes only where they change, `ech`/`el` for runs of
  blanks. This is what ncurses does and what makes ttrc fast on a 9600
  baud serial line and over a slow network.
- **Line drawing that works everywhere.** On a UTF-8 terminal, boxes use
  U+2500 characters. Otherwise tu uses the terminal's alternate
  character set (`acsc`, `smacs`, `rmacs`), which iris-ansi, xwsh,
  winterm and vt100 all have, with `+ - |` as the last resort.
- **Character sets.** UTF-8 or an 8-bit set (ISO 8859-1 on IRIX), from
  the locale, overridable in settings. Widths come from `tl/unicode.h`,
  not from the system's `wcwidth`, which IRIX lacks.
- **Colour.** 8 or 16 colours through `setaf`/`setab`, 256 when the
  entry says so, and a monochrome scheme using bold, underline and
  reverse when there are no colours (vt100, a real VT220).

### 3.2 Input

- The terminal is in raw mode. A key decoder turns bytes into keys using
  the terminfo key capabilities (`kf1`, `kcuu1`, ...) and the usual
  xterm, rxvt and iris-ansi sequences, with modifiers. A lone Esc is
  told apart from an escape sequence by a short timeout.
- **Esc then a digit is F1 to F10** (mc's convention), for terminals
  without function keys. Esc then a letter is Alt plus that letter.
- Mouse: xterm reporting (1000, 1002, 1006) where the terminal has it,
  for clicking in panels and dragging scroll bars. Bracketed paste on the
  command line.
- SIGWINCH comes through the self-pipe. The screen, the panels, the
  shell's pty and the shell's screen copy all resize together.

### 3.3 What the terminal can do

tu sorts terminals into tiers once at startup and the drawing code asks
for effects, not escape sequences:

| Tier | Typical terminals | Colour | Lines and boxes |
|---|---|---|---|
| basic | vt100, VT220, serial consoles | none: bold, underline, reverse | ACS, or ASCII `+` and `-` |
| ansi | iris-ansi (winterm, xwsh), Linux console, xterm (8/16) | 8 or 16 | ACS, or Unicode on a UTF-8 console |
| 256 | xterm-256color, most Linux terminals | 256 | Unicode |
| modern | ttrt, kitty, foot, WezTerm, Ghostty, recent xterm | 24-bit | Unicode, rounded and heavy corners, block elements |

How tu finds out, in order: the terminfo entry (`colors`, `RGB`, `Tc`
and the other extended capabilities), `COLORTERM=truecolor`, and, only
when `TERM` names an xterm-like terminal, a query at startup (XTVERSION,
`CSI > 0 q`, and DA1) with a short timeout. A terminal that does not
answer simply stays in its terminfo tier, so serial lines and dumb
terminals never see garbage. ttrt answers XTVERSION with its name. The
user can force a tier in the settings.

### 3.4 Using a good terminal

On the modern tier, and partly on 256, tu uses:

- **True colour themes.** Themes are written in 24-bit colour; tu maps
  each colour to the nearest of 256, 16 or 8 when the terminal has fewer,
  and to bold/reverse on basic. A theme can say how it degrades (the
  classic NC blue maps to plain blue on 8 colours rather than to the
  nearest grey).
- **Real boxes and shadows.** Rounded or double frames, a heavy frame
  for the active panel, and dialog shadows that darken the cells
  underneath (the colours under the shadow are dimmed, text stays
  readable), as Turbo Vision drew them. On lower tiers the shadow is a
  plain dark band or nothing.
- **Unambiguous keys** with the kitty keyboard protocol (`CSI > 1 u`),
  which ttrt and tl already speak: Ctrl-Enter, Ctrl-Tab, Shift-F-keys and
  a lone Esc without timeouts. tu turns it off before the shell gets the
  terminal and back on when the panels return, so programs in the shell
  always see the keyboard mode they set themselves.
- **Synchronized output** (mode 2026) where offered, so a full redraw
  never tears.
- **The clipboard through OSC 52** (copy the selected file names or a
  path), the window title through OSC 2 (the active panel's directory),
  focus events (mode 1004) to re-read the panels when the window comes
  back, SGR mouse with exact cells (1006), styled underlines.
- **Later effects**, kept in mind but not built now: fading dialogs in
  and out, smooth scrolling by half-cells with block elements, and
  images in the viewer through sixel or the kitty graphics protocol.

The application never asks which tier it runs on. It draws "a frame in
style X", "a shadow", "the theme's colour for selected files", and tu
decides how. Adding an effect later is a change in tu, not in ttrc.

### 3.5 Ready for the viewer and the editor

F3 and F4 are later milestones, but tu is built so they need no
redesign:

- The screen diff takes **scroll hints** (`scroll(top, bottom, n)`), sent
  as scroll regions (`csr` with `ind`/`ri`) or insert and delete line,
  so scrolling a long file costs one line of output, not a screen.
- Text cells hold full grapheme clusters with widths from
  `tl/unicode.h`: combining marks, double-width CJK, tabs expanded by
  the widget.
- A text area widget with a cursor, cursor shapes (`Ss`, DECSCUSR),
  selection, and an input line that edits, so a find dialog and the
  editor share code.
- Key maps per mode (panels, viewer, editor, dialog), so each can bind
  keys without the others seeing them.

### 3.6 Widgets

A small set, drawn into the cell screen: frames, a list with a cursor
and a scroll bar, an input line with history, buttons, check boxes,
radio buttons, a menu bar with pull-downs, a function key bar, message
boxes, and a progress dialog. Dialogs are modal loops over the same
event loop, so the shell keeps running and its output keeps arriving.

## 4. The subshell

This is where ttrc differs most from mc.

### 4.1 One pty, every byte seen

At startup ttrc opens a pty with `tp::Pty` and starts `$SHELL` in it as
an interactive shell (`tp::Process`, login shell if configured). It adds
nothing to the shell's environment except `TERM`, `COLUMNS` and `LINES`
corrections and `TTRC=1`, so scripts can tell they run inside ttrc. All
output from the pty goes through ttrc:

1. It is fed to a `tl::Terminal` the size of the real terminal. This is
   ttrc's **copy of the shell's screen**, including its scrollback.
2. If the shell is in front (section 4.3), the same bytes are written to
   the real terminal unchanged. Programs get exactly the terminal they
   expect, with every capability it has.
3. tl reports OSC 7 (working directory) and OSC 133 (prompt and command
   markers) to ttrc when a shell happens to send them (sections 4.4
   and 4.5).

Because ttrc has the screen copy, it never depends on the terminal's
alternate screen and needs no `cons.saver`. Going back to the shell
redraws the copy through tu. That works on winterm, a serial line and
the Linux console alike.

For the copy to match what the terminal shows, tl must interpret the
bytes the way the real terminal does. The shell gets the real terminal's
`TERM` (iris-ansi, xterm, ttrt), and tl is xterm-compatible, which
covers all of them closely enough. Where the two could still drift
(a terminal tl does not know), the copy is only used for redrawing and
Ctrl-O restores what tl understood.

### 4.2 Pass-through now, rendering later

There are two ways to show the shell:

- **Pass-through** (chosen for now): when the shell is in front, its
  bytes go to the real terminal unchanged, and ttrc's copy follows along.
  Simple, fast, and programs get every feature the terminal has.
- **Rendered**, as screen and tmux do: the shell's bytes only go into the
  copy, and tu draws the copy onto the terminal. Slower and limited to
  what tl understands, but it allows a shell pane next to the panels, a
  shell in a window over them, and several shells.

The code keeps the switch open. The shell lives behind one interface,
`ShellView` (`feed(bytes)`, `show()`, `hide()`, `resize()`, `key()`),
with pass-through as its first implementation. Drawing the copy through
tu is needed anyway, to restore the screen on Ctrl-O and to show the
shell's last lines under shortened panels, so a rendered implementation
reuses that code. Nothing else in ttrc knows which one is in use.

### 4.3 Three states

| State | The screen shows | Keys go to |
|---|---|---|
| **Panels** | ttrc. The shell waits at its prompt (or runs a background job). | ttrc |
| **Shell** (Ctrl-O) | the shell, as pass-through | the pty, except Ctrl-O |
| **Command** | the shell, running a command started from ttrc's command line | the pty, except Ctrl-O |

- Ctrl-O from Panels redraws the shell's screen from the copy, puts the
  real terminal into pass-through, and from then on output flows
  straight through. Ctrl-O from Shell draws the panels again. Output that
  arrives while the panels are up goes into the copy only.
- Ctrl-O works while a full-screen program runs (vi, top). The panels
  come up; going back redraws vi's screen from the copy. mc cannot do
  this without the alternate screen.
- After a command finishes (section 4.4), ttrc returns to the panels,
  as NC and mc do. A setting keeps the shell in front instead, and
  another waits for a key first.
- With the panels up, the shell's screen shows in every line the panels
  do not cover. Ctrl-Up and Ctrl-Down make the panels shorter or taller
  (NC's feature), so the last lines of output stay visible.

### 4.4 When a command has finished

ttrc needs to know this to come back to the panels and to re-read the
working directory. It does not ask the shell. In order:

1. **OSC 133 D**, if the shell sends it (section 4.5). Exact, with the
   exit code.
2. **The pty's foreground process group** (`tcgetpgrp` on the master,
   new `tp::Pty::foreground_group()`). While a command runs, its process
   group owns the terminal. When the shell's group owns it again, the
   command is done. This works with every shell that does job control:
   ksh (IRIX's `sh` invoked as `ksh`), csh, tcsh, bash, zsh, fish.
3. **The shell has no children and is waiting for input**, for shells
   without job control (IRIX `/bin/sh` under that name, `jsh`, dash). The
   process table is read per system (new `tp::children()`): `/proc` on
   Linux, `PIOCPSINFO` on IRIX. ttrc checks this when the pty has been
   quiet for 50 ms.

### 4.5 The working directory

Again without asking the shell:

1. **OSC 7** (`ESC ] 7 ; file://host/path`), if the shell sends it.
   Modern shell setups do. ttrc documents a one-line snippet for users
   who want it in their own rc files, but never adds it itself.
2. **Reading it from the kernel** (new `tp::cwd_of(pid)`):
   - Linux: `readlink("/proc/<pid>/cwd")`.
   - IRIX: `open("/proc/<pid>")`, `ioctl(PIOCCKPTOPENCWD)` gives a
     descriptor for the process's working directory, then `fchdir` and
     `getcwd`. **Verified on the IRIX 6.5.22 guest**: a test parent read
     its child's `/usr/share` this way (a 40-line C probe, kept in
     `ignore/probe/`, outside version control; the real code goes in
     `tp/sys_irix.cpp`).
   - Other systems: Solaris `/proc/<pid>/path/cwd`, FreeBSD
     `KERN_PROC_CWD`. Without one, ttrc falls back to OSC 7 only.

ttrc reads the directory whenever a command finishes and when the user
switches from the shell to the panels. If it changed, the active panel
follows, as in mc.

### 4.6 From the panels to the shell

The other direction cannot be done without typing into the shell: only
the shell can change its own directory. ttrc types as little as
possible.

- **Lazily.** Moving around in the panels types nothing. Only when the
  user runs a command or switches to the shell, and the shell's
  directory differs from the active panel's, does ttrc send one
  `cd` line first. A setting turns it to "always" (mc's behaviour) or
  "never".
- **Quoted for the shell family.** sh, ksh, bash and zsh get
  `cd '...'` with `'\''` for quotes. csh and tcsh get single quotes with
  `!` escaped. fish gets its own escaping. A path that cannot be quoted
  safely for that shell (a newline in csh) is not sent, and ttrc says
  so. The family comes from the shell's name; nothing is probed.
- **On an empty line only.** ttrc sends it only when the shell is at its
  prompt with an empty input line, which it can see in the screen copy.
- The `cd` lines do land in the shell's history. ttrc does not change
  `HISTCONTROL` or similar to hide them; lazy sync keeps them rare.

### 4.7 The command line

- The line under the panels is ttrc's own input line, so Up, Down, Tab
  and the F-keys keep their panel meanings. Ctrl-Enter and Alt-Enter put
  the current file name in, Ctrl-X Ctrl-P the other panel's path, and
  Alt-P/Alt-N recall history, as in mc.
- **The prompt in front of it is the shell's real prompt,** taken from
  the screen copy: the text left of the cursor on the cursor row when
  the shell is idle. tl has already interpreted the colours and cursor
  motion, so there is no scraping of raw bytes. Multi-line prompts show
  their last line.
- Ctrl-O with text on ttrc's line types it into the shell, so the user
  can finish it with the shell's own completion. Going back, the text
  right of the prompt in the copy becomes ttrc's line again. This is
  mc's two-way command buffer without `bind -x` hooks.
- `cd` typed on ttrc's line (without `;`, `|`, `&` or substitutions)
  changes the panel directly, also into archives, which the shell could
  not do.
- `exit` on the line, or F10, quits ttrc. The shell gets SIGHUP.

### 4.8 Leaving ttrc in the panel's directory

A program cannot change its parent shell's directory. As with mc, a
small shell function does it: ttrc writes its last directory to a file
named with `-P file`, and the function `cd`s there. ttrc ships the
function for sh/ksh/bash/zsh and an alias for csh/tcsh. **This function
is the `tc` command** (section 9).

## 5. tf: the virtual file system

### 5.1 Paths

A `tf::Path` is a chain: a path in the local file system, then for each
archive or compressed file entered, a layer and a path inside it. The
written form is mc's, so mc users read it at once: the file, then
`/<scheme>://`, then the path inside.

```
/home/me/src/ttr-1.0.tar.gz/utar://ttr/README.md
/data/old.zip/uzip://backups/home.tar/utar://me/.profile
/usr/share/man/man1/ls.1.gz/ugz://ls.1
```

| Scheme | Layer |
|---|---|
| `utar` | tar, including `.tar.gz`, `.tgz`, `.tar.bz2`, `.tbz2`, `.tar.Z`: the decompression happens underneath, as in mc |
| `uzip` | zip |
| `ugz`, `ubz2`, `uz` | a single gzip, bzip2 or compress file, shown as a directory holding the uncompressed file |

Enter on a file picks the scheme from its content (magic numbers), with
the name as a hint; typing a path with a scheme forces it. The notation
is what the panel title shows and what `cd` on the command line accepts.
A real directory whose name happens to be `utar:` followed by more of
the path is the only case that reads ambiguously; `tf` checks the local
file system first, so it still works.

### 5.2 The interface

```
class Fs {
public:
    virtual ~Fs() {}
    virtual int caps() const = 0;                 /* CAP_WRITE, CAP_SEEK, CAP_SYMLINKS, ... */
    virtual int stat(const char* p, Stat* st, bool follow) = 0;
    virtual DirReader* open_dir(const char* p, int* err) = 0;
    virtual Stream* open(const char* p, int flags, int mode, int* err) = 0;
    virtual int mkdir(const char* p, int mode) = 0;
    virtual int remove(const char* p) = 0;       /* file or empty directory */
    virtual int rename(const char* from, const char* to) = 0;
    virtual int symlink(const char* target, const char* p) = 0;
    virtual int readlink(const char* p, char* buf, size_t n) = 0;
    virtual int set_attrs(const char* p, const Attrs& a, int which) = 0;  /* mode, owner, times */
    virtual int space(const char* p, Space* out) = 0;
};

class Stream {
public:
    virtual ~Stream() {}
    virtual long read(void* buf, size_t n) = 0;
    virtual long write(const void* buf, size_t n) = 0;
    virtual int seek(int64 off) = 0;              /* where CAP_SEEK */
    virtual int close() = 0;                      /* reports write errors */
};
```

Errors are `errno`-style integers, never exceptions. Every file access
in ttrc, including panel listings, the viewer, copying, and running a
file's association, goes through `Fs`. Nothing in `src/ttrc/` calls
`open` or `stat` itself.

### 5.3 Layers in the first version

| Layer | Reads | Writes | Notes |
|---|---|---|---|
| local | yes | yes | POSIX calls; `statvfs` for free space |
| gzip (`.gz`, `.tgz`) | yes | no | zlib `inflate`; appears as a directory with one file |
| bzip2 (`.bz2`, `.tbz2`) | yes | no | libbz2 |
| compress (`.Z`) | yes | no | our own LZW decoder, about 150 lines; the format is simple and the patents are long gone |
| tar | yes | no | ustar, GNU long names and sparse files, pax headers; usually under one of the three above |
| zip | yes | no | central directory, stored and deflate (zlib) entries, Zip64 sizes; encrypted entries listed but not opened |

Archives are indexed once when entered and the index is cached, keyed
by path, size and modification time. Reading a member of a compressed
tar means decompressing up to it. A member read twice, or a large
`.tar.gz` browsed a lot, gets its decompressed data spooled to a temp
file so the next access seeks instead. Zip needs seeking; a zip inside
a compressed file is spooled to a temp file first.

zlib and bzip2 come in as pinned submodules in `third_party/`, compiled
through one wrapper file each (AGENTS.md). IRIX's own `libz` (1.1.x)
would do for reading, but bundling keeps both systems on the same code,
and IRIX has no libbz2 at all.

### 5.4 File operations

Copy, move, delete, make directory, rename, symlink and change
attributes are `tf::Job`s over `Fs`:

- A job works in steps: `step(budget_ms)` does some work and returns, so
  ttrc's loop shows progress, keeps reading the shell, and lets Esc
  cancel. A large file is copied in chunks across steps.
- Questions (overwrite? skip? retry after an error?) are returned to
  the caller as a state, answered by a dialog, and the job continues.
  "All" answers are remembered for the rest of the job.
- Same-file-system fast paths: `rename` for moves, and plain local reads
  and writes for local copies. Across layers, a copy is read from one
  `Stream` and written to another. That is how files come out of
  archives.
- Copies keep mode, owner where permitted, and times. Symlinks are
  copied as links, devices and FIFOs recreated, hard links within one
  job kept as hard links.
- A move between file systems is copy then delete, and deletes the
  source only after the copy is complete.

## 6. Platform layer

### 6.1 Per-system files

The rule from `tp/` applies: no `#ifdef __sgi` spread through the code.
Each system-specific piece is a function with one implementation file
per system, chosen by the Makefile:

| Need | Linux | IRIX |
|---|---|---|
| another process's cwd | `/proc/<pid>/cwd` | `PIOCCKPTOPENCWD` |
| a process's children and state | `/proc/<pid>/stat` | `PIOCPSINFO` over `/proc` |
| free space | `statvfs` | `statvfs` |
| file system type of a directory | `statfs` `f_type` | `statvfs` `f_basetype` |

### 6.2 IRIX shells, as found on the 6.5.22 guest

This corrects [docs/shell.md](../docs/shell.md) section 10.1:

- `/bin/sh` and `/bin/ksh` are the same binary, `/sbin/sh`, a ksh88. As
  `ksh` it has `$(...)` and job control. As `sh` it rejects `$(...)` but
  has `${#var}`. ttrc treats both as the sh family for quoting.
- `/bin/jsh` is the old Bourne shell (`/sbin/bsh`).
- `/usr/bin/tcsh` is part of the base system, and root's shell is
  `/bin/csh`. Both get the csh-family quoting.
- GNU bash is installed from freeware on this guest.

None of this matters much for ttrc, since it hooks into no shell. It
matters for quoting the `cd` line (section 4.6) and for knowing which
shells to test.

### 6.3 Additions to tp

- `Pty::foreground_group()`: `tcgetpgrp` on the master.
- `cwd_of(pid, buf, n)` and `children_of(pid, ...)`, in `sys_posix.cpp`
  (Linux `/proc`) and `sys_irix.cpp` (`/proc` ioctls), with a "not
  supported" answer elsewhere.

## 7. The interface

```
 Left     File     Command     Options     Right
+- /usr/share -------------------------++- ttr-1.0.tar.gz/utar://ttr ----------+
|     Name        |   Size|  Modified  ||     Name        |   Size|  Modified  |
|/..              | UP-DIR|Oct 21 09:14||/..              | UP-DIR|Oct 20 18:02|
|/X11             |   4096|Jun 26  2024||/src             |   4096|Oct 20 18:02|
|/doc             |   4096|Jun 26  2024|| Makefile        |  18011|Oct 20 18:02|
| magic           |   6122|Mar 29  1996|| README.md       |  21704|Oct 20 18:02|
|                 |       |            ||                 |       |            |
+--------------------------------------++--------------------------------------+
| magic                                || README.md                            |
+--------------------------------------++--------------------------------------+
IRIS 3# _
 1Help  2Menu  3View  4Edit  5Copy  6RenMov 7Mkdir 8Delete 9PullDn 10Quit
```

- Panels: full, brief (names in columns) and long listings; a quick
  view of the file under the cursor in the other panel; info about the
  file system and selection. Sort by name, extension, size, time,
  unsorted, reverse. Hidden files on or off.
- Keys as in NC and mc: Tab switches panels, Insert or Ctrl-T selects,
  `+` `-` `*` select by pattern, Ctrl-U swaps panels, Ctrl-R re-reads,
  Alt-S or typing starts a quick search, Ctrl-PgUp goes up, Enter enters
  directories and archives or runs, Ctrl-\ the directory hotlist, Alt-?
  find file, Ctrl-O the shell, F9 the menu bar.
- F3 and F4 come in a later milestone: a viewer (text, hex, wrap,
  search, through the VFS, so files inside archives open without
  extracting) and an editor, both on tu (section 3.5). Until then F3
  runs `$PAGER` (or `more`) and F4 runs `$VISUAL`, `$EDITOR` or `vi` in
  the shell, on local files; for a file in an archive they work on a
  temporary copy.
- F2 is the user menu (`~/.config/ttrc/menu`) and Enter on a file runs
  its association (`~/.config/ttrc/assoc`). Both use mc's `%f %d %s %t`
  macros so existing habits carry over. The file formats are ours and
  simpler than mc's.
- Colours: the blue NC scheme by default, a monochrome one, and
  themes in the same settings format.

## 8. Settings and files

ttrc follows ttrt's rule: its settings are its own. They are in
`~/.config/ttrc/settings` ("name: value" lines, `core/settings.h`
format), changed from Options > Configuration and kept in the same way
(only changed values are written). Other files in that directory:
`hotlist`, `history`, `menu`, `assoc`.

## 9. The name `tc`

**Linux already has a `tc`.** It is iproute2's traffic control tool, in
`/sbin/tc` here, and `/usr/bin/tc` on Arch, where a package installing
`/usr/bin/tc` would conflict with iproute2. Nothing is called `tc` on
IRIX.

Decided: the program is installed as `ttrc` everywhere. `tc` is the
shell function from section 4.8, which is needed anyway for leaving ttrc
in its last directory:

```sh
tc() { ttrc -P "${TMPDIR:-/tmp}/ttrc.$$" "$@"; ... cd to what it wrote ...; }
```

`make install` puts the function in `/usr/share/ttrc/tc.sh` (and
`tc.csh`), and the README tells users to source it from their shell's rc
file. A user who types `tc` gets ttrc and keeps iproute2's `/sbin/tc`
reachable by its path. On IRIX the package could also install a `tc`
link, since nothing is in the way there.

## 10. Testing

- **tf**: unit tests over fixture archives made with the system's own
  `tar`, `zip`, `gzip`, `bzip2` and `compress` (plus damaged and
  truncated copies, and archives with absolute paths and `..` that must
  never escape their layer), run on Linux and in the IRIX guest.
- **tu**: its output is fed into a `tl::Terminal` and the resulting
  screen is compared with what was drawn. tl is the test oracle, so the
  tests run headless, for xterm, iris-ansi and vt100 terminfo entries.
- **The subshell**: ttrc runs inside a `tp::Pty` driven by a test, with
  another `tl::Terminal` as the screen. Tests cover Ctrl-O, command
  completion and directory tracking with sh, ksh, csh and tcsh on IRIX
  and bash, dash, zsh on Linux.
- On the emulated Indy: ttrc inside winterm and inside ttrt, with
  screenshots, as for ttrt.

## 11. Milestones

The first goal is the commander itself: two panels, the shell, and file
operations. Viewer and editor come after.

1. **tu basics and browsing.** The terminfo reader and `tparm`, tier
   detection, the cell screen and diff, key input, frames, lists, the
   function key bar. tf's local layer and `Path`. Two panels with
   navigation, sorting, listing modes, quick search, F10. Linux and
   IRIX, from vt100 to ttrt.
2. **The subshell.** pty, screen copy, `ShellView` with pass-through,
   Ctrl-O both ways, panels shortened over the shell's output, the
   command line with the shell's prompt, command completion, cwd tracking
   on Linux and IRIX, lazy `cd`, `-P` and the `tc` function.
3. **File operations.** Selection, copy, move, delete, mkdir, rename,
   symlink, attributes, with dialogs and progress, overwrite questions,
   cancel. Menus (F9), hotlist, find file, compare directories, user
   menu, associations, settings dialog, colour themes.
4. **Archives.** gz, bz2, Z, tar, zip read-only through the VFS; copy
   out of archives; zlib and bzip2 submodules.
5. **Viewer and editor** on tu (F3, F4).
6. Later: writing archives, the rendered `ShellView` (shell panes and
   windows), FTP/SFTP layers, effects, images in the viewer, file
   operations in a worker process, syntax colouring in the editor,
   panelize (find results as a panel), hard links kept as hard links
   when copying. From section 3.3 and 3.4 the tiers, true colour,
   Unicode frames, shadows and synchronized output are in; the startup
   query (XTVERSION), the kitty keyboard protocol, OSC 52, the window
   title and focus-driven re-reading are not yet.

## 12. Decisions from the review (2026-10-21)

1. **Name**: `ttrc`, with `tc` as a shell function (section 9).
2. **Shell display**: pass-through first, behind a `ShellView` interface
   so the rendered (tmux-like) way can be added later (section 4.2).
3. **Lazy `cd`**: accepted; a `cd` line in the shell's history when a
   command runs in another directory is expected.
4. **Terminfo**: our own reader and `tparm`, no curses library
   (section 3.1).
5. **Archive paths**: mc's notation, `file.tar.gz/utar://inner/path`
   (section 5.1).
6. **Terminals**: plan for ttrt and modern Linux terminals (true colour,
   Unicode boxes, shadows, kitty keyboard, OSC 52, synchronized output)
   while keeping vt100 and winterm fully usable (sections 3.3 and 3.4).
7. **Scope**: the commander first; F3 and F4 later, but tu designed for
   them now (section 3.5).
