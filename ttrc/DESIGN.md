# ttrc: To The Roots Commander

Status: design for review, 2026-10-21. Nothing is implemented yet.

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
  and no curses screen library.
- **The framework's rules**: C++98 for MIPSpro 7.4 and
  `g++ -std=c++98 -pedantic`; no exceptions, RTTI, STL or iostreams;
  plain make; per-system code in separate files, not `#ifdef`s
  (as in `tp/`); third-party code as pinned submodules.

Not in the first version: writing into archives, an internal editor
(F4 runs `$EDITOR`), network file systems (FTP, SFTP), background file
operations in a separate process.

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

- **Terminfo for capabilities.** tu reads the terminal's entry with the
  system's terminfo functions (`setupterm`, `tigetstr`, `tigetnum`,
  `tparm`). They exist in ncurses and in IRIX's SVR4 curses. tu uses
  nothing else from curses: no `initscr`, no windows, no `refresh`. If no
  entry is found, a built-in vt100/xterm description is used.
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

### 3.3 Widgets

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
2. If the shell is in front (section 4.2), the same bytes are written to
   the real terminal unchanged. Programs get exactly the terminal they
   expect, with every capability it has.
3. tl reports OSC 7 (working directory) and OSC 133 (prompt and command
   markers) to ttrc when a shell happens to send them (section 4.4).

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

### 4.2 Three states

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
- After a command finishes (section 4.3), ttrc returns to the panels,
  as NC and mc do. A setting keeps the shell in front instead, and
  another waits for a key first.
- With the panels up, the shell's screen shows in every line the panels
  do not cover. Ctrl-Up and Ctrl-Down make the panels shorter or taller
  (NC's feature), so the last lines of output stay visible.

### 4.3 When a command has finished

ttrc needs to know this to come back to the panels and to re-read the
working directory. It does not ask the shell. In order:

1. **OSC 133 D**, if the shell sends it (section 4.4). Exact, with the
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

### 4.4 The working directory

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

### 4.5 From the panels to the shell

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

### 4.6 The command line

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

### 4.7 Leaving ttrc in the panel's directory

A program cannot change its parent shell's directory. As with mc, a
small shell function does it: ttrc writes its last directory to a file
named with `-P file`, and the function `cd`s there. ttrc ships the
function for sh/ksh/bash/zsh and an alias for csh/tcsh. **This function
is the `tc` command** (section 9).

## 5. tf: the virtual file system

### 5.1 Paths

A `tf::Path` is a chain: a path in the local file system, then for each
archive or compressed file entered, a layer and a path inside it.
Written out, `#` marks the step into a file:

```
/home/me/src/ttr-1.0.tar.gz#/ttr/README.md
/data/old.zip#/backups/home.tar#/me/.profile
/usr/share/man/man1/ls.1.gz#/ls.1
```

The layer type is not part of the path; it comes from the file's
content (magic numbers), with the name as a hint. A `#` that is part of
a file name is written `\#` in this notation only; names on disk are
never changed. The notation is what the panel title shows and what
`cd` on the command line accepts.

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
matters for quoting the `cd` line (section 4.5) and for knowing which
shells to test.

### 6.3 Additions to tp

- `Pty::foreground_group()`: `tcgetpgrp` on the master.
- `cwd_of(pid, buf, n)` and `children_of(pid, ...)`, in `sys_posix.cpp`
  (Linux `/proc`) and `sys_irix.cpp` (`/proc` ioctls), with a "not
  supported" answer elsewhere.

## 7. The interface

```
 Left     File     Command     Options     Right
+- /usr/share -------------------------++- ttr-1.0.tar.gz#/ttr ----------------+
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
- F3 views with ttrc's own viewer: text, hex, wrap on or off, search,
  through the VFS, so files inside archives can be viewed without
  extracting them first.
- F4 runs `$VISUAL`, `$EDITOR` or `vi` on the file. A file inside an
  archive is shown read-only for now.
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

Proposal: the program is installed as `ttrc` everywhere. `tc` is the
shell function from section 4.7, which is needed anyway for leaving ttrc
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

1. **tu basics and browsing.** Terminfo, the cell screen and diff,
   key input, frames and lists. tf's local layer and `Path`. Two panels
   with navigation, sorting, the function key bar, F10. Runs on Linux
   and IRIX.
2. **The subshell.** pty, screen copy, Ctrl-O both ways, the command
   line with the shell's prompt, command completion, cwd tracking (Linux
   and IRIX), lazy `cd`, `-P` and the `tc` function.
3. **File operations.** Selection, copy, move, delete, mkdir, rename
   with dialogs and progress, overwrite questions, cancel.
4. **Archives.** gz, bz2, Z, tar, zip read-only through the VFS; copy
   out of archives; zlib and bzip2 submodules.
5. **The rest of the daily set.** Viewer, F4, user menu, associations,
   hotlist, find file, compare directories, Options dialogs, colour
   schemes.
6. Later: writing archives, an internal editor, FTP/SFTP layers, file
   operations in a worker process.

## 12. Open questions for review

1. **Name**: install as `ttrc` with `tc` as a shell function (section 9),
   or install `tc` itself and accept the iproute2 conflict on Linux?
2. **Pass-through plus screen copy** (section 4.1) means programs in the
   shell talk to the real terminal and ttrc's copy follows along. The
   alternative is a full terminal multiplexer like screen or tmux, where
   ttrc redraws everything through tu. That is simpler to keep
   consistent but slower, and loses terminal features tl does not have.
   The design picks pass-through.
3. **Lazy `cd`** (section 4.5): is a `cd` line in history, only when a
   command is actually run in a different directory, acceptable as the
   default?
4. **Terminfo through the system's functions** (section 3.1), or our own
   reader of compiled terminfo files? The system's is less code and
   always matches the installed database. Our own avoids linking
   libcurses at all.
5. **Archive notation** `file#/inner/path` (section 5.1), or mc's
   `file/utar://inner/path`?
