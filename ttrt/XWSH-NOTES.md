# How xwsh works, and what dash taught us

Notes from taking apart IRIX 6.5.22's `/usr/sbin/xwsh` (disassembled with
spimdisasm, decompiled with m2c's `irixn32` branch, read against its man
page and app-defaults) and from our own libtsm-based IRIX terminal,
`dash`. They are the evidence behind [DESIGN.md](DESIGN.md).

The binary and the generated assembly and C are kept out of the
repository, under `ignore/xwsh/` (`asm/funcs/xwsh/*.s`, `c/*.c`,
`xwsh.man.txt`, `XWsh`). Offsets below refer to that decompilation.

---

## 1. xwsh

### 1.1 What it is made of

| | |
|---|---|
| Binary | N32 MIPS-III, C++ (MIPSpro, cfront-style mangled names kept in the dynamic symbol table), 640 KB, **setuid root** |
| Libraries | `libX11`, `libC`, `libCsup`, `libm`, `libc`. **No Xt, no Motif, no GL, no IRIS GL.** |
| Toolkit | SGI's private `tu` toolkit (`tuGadget`, `tuTopLevel`, `tuMenu`, `tuScrollBar`, `tuFontBrowser`, `tuExec`...), linked in statically: about 1,700 of its 2,400 functions |
| Terminal | `TTYEmulator` (state, parser glue, rendering), `TTYLine` / `TTYLineArray` (text storage), `TTYAttr`, `TTYEmulatorPatternTable` (escape sequence matcher), `wshTTYEmulator` (key maps, colour names) |
| Process | `Shell` (pty and child), `wshApp` (options, resources, login/utmp, main loop), `wshCap` (capabilities) |

### 1.2 Text storage: lines of attribute runs, not a cell grid

A `TTYLine` holds a character buffer and a linked list of **runs**. Each
run is one 32-bit attribute word plus a pixel width:

| Bits of the run word | Meaning |
|---|---|
| 0-8 | length in characters (up to 511) |
| 21-24 | foreground colour index (16 colours) |
| 25-28 | background colour index |
| 29 | reverse / selected |
| 30 | underline |
| separate fields | font index (normal, bold, italic, bold italic), width in pixels |

The line has a dirty bit (bit 31 of its flags), its pixel width and its
character count. Lines form a linked list (`TTYLineArray::findLine` walks
it); scrolling re-links lines rather than copying characters.

Runs carry pixel widths because xwsh supports **proportional fonts**:
characters are placed at their natural widths and only the column count
is emulated (man page, `-fn`).

### 1.3 Drawing: plain Xlib, CI8 on an Indy

- `render()` walks the visible lines and calls `renderLine()` only for
  lines with the dirty bit, unless a full repaint is pending. Below the
  last line it `XClearArea`s; then `renderCursor()`.
- `renderLine()` draws each run as one `XFillRectangle` (background) and
  one `XmbDrawString` (text, through an X output context font set). The
  remainder of the line is filled with `XFillRectangles`. Underline is
  drawn with **one `XDrawLine` per non-space character**.
- One GC; `getOtherGC()` changes its foreground and background only when
  the pixel differs from the cached one (`XSetForeground` is skipped for
  repeated colours).
- **No `XCopyArea` anywhere.** Scrolling a region re-links the lines and
  sets the dirty bit on every line in it; the next frame repaints them.
- The visual is the **screen's default visual**. On an Indy that Xsgi
  starts with `-pseudomap 4sight` this is 8-bit PseudoColor: xwsh is a
  CI8 program. It creates and installs its own colormap through the tu
  colour classes. Its "overlay mode" escape sequence has been a no-op
  since IRIX 4.0 (man page).
- Fonts: `XCreateFontSet` with the `FixedWidthFont` scheme name; separate
  bold, italic and bold italic fonts; DEC special graphics come from a
  separate `decGraphicsFont` (`-dec-dectech` fonts) rather than being
  drawn.

### 1.4 Input pacing

`readFromShell()` reads at most **2 KB** per call. It hands the bytes to
the emulator, then:

- if the read was **under 200 bytes** and no repaint is scheduled, it
  starts a short `tuTimer` and repaints when it fires, so keystroke echoes
  and small bursts are coalesced;
- otherwise it repaints at once (jump scroll for bulk output).

`write()` scans for runs of printable bytes and hands each run to
`flush()` in one call; control bytes and escape sequences go through
`match()` against the pattern table. URLs are recognised inline while the
text streams by (`match_url`, `end_url`) and are clickable.

### 1.5 Pty and child

`Shell::findPseudoTTY`:

```c
fd = -1;
{ wshCap cap(CAP_DEVICE_MGT);            /* raised for this call only */
  name = _getpty(&fd, O_RDWR | O_NDELAY, 0622, 0); }
```

`Shell::start` holds `SIGCLD` around `fork()`. The parent renices itself
(`nice(-40)` then `nice(10)`) so the terminal runs ahead of what it hosts.

`Shell::startChild`, in the child:

1. close every descriptor below `getdtablehi()`;
2. `setsid()`; **open the slave** (`O_RDWR`): on IRIX, as on SVR4, the
   first terminal a session leader opens becomes its controlling
   terminal (there is no `TIOCSCTTY`);
3. `dup(0)` twice for stdout and stderr;
4. `TIOCSWINSZ`, then `TCSETA` with an old-style `termio` (not
   `termios`);
5. for `-console`, `TIOCCONS` under `CAP_...` and `CONSOLE=1`;
6. renice back to normal priority;
7. `chown(slave, getuid(), getgid())`, `chmod(slave, 0622)` under
   `CAP_FOWNER`, then `setgid(getgid())`, `setuid(getuid())`;
8. reset `SIGINT` and `SIGQUIT`, drop all capabilities, `execvp`.

`wshApp::login` and `logout` write utmpx and wtmpx (`pututxline`,
`updwtmpx`) unless `-ut`. That and `chown` of the slave are why it is
setuid root; every privileged step raises exactly one capability in a
scoped `wshCap` object.

### 1.6 What to take from xwsh

- Repaint only dirty lines, cache GC state, and coalesce small reads with
  a short timer while bulk output repaints at once.
- Plain Xlib on the default 8-bit visual is fast enough on Newport; GL
  bought nothing for a terminal on the Indy.
- The child setup sequence above is the IRIX recipe, step for step.
- Not to copy: per-character underline lines, full repaints on scroll
  (no `XCopyArea`), 16 colours, server fonts as the only glyph source,
  setuid root.

---

## 2. dash (our libtsm terminal)

dash is a Motif + OpenGL terminal (`GLwMDrawingArea`, `glXCreateContext`)
on libtsm, with tabs, themes and animated backgrounds.

### 2.1 Why its text is blurry

`load_font()` loads the display font for the metrics, then a **second,
larger font** (up to 4x the pixel size) and `build_font_atlas()` draws
each glyph of the large font into a pixmap, reads it back with
`XGetImage`, and box-filters it down to the cell size in 24.8 fixed
point. The two fonts are different designs with different metrics and
the ratio is rarely an integer, so stems land between pixels and come
out grey. The texture is then sampled with `GL_NEAREST`, which keeps
the grey exactly. Box-filtered bitmap fonts are not anti-aliased
outlines; they are just soft.

Lesson: **glyphs must be rasterized at the cell size from the font that
is actually used**, and placed on whole pixels. Anti-aliasing is worth
having only from outline fonts with proper hinting (FreeType on Linux).

The IRIX tree (`/mnt/files.nfs/share/prj/libtsm`, commits "fixed
textures on impact graphics" and "make the font atlas creation a little
bit more fancy ... apply gamma 0.75 to boost brightness") shows the
symptom being fought downstream: a float intermediate buffer, texture
normalization and a gamma boost, none of which can restore pixels the
resampling already blurred.

### 2.2 IRIX port of the pty code

The working IRIX version of libtsm's `shl-pty.c` lives in that tree. Its
changes, all as `#ifdef __sgi` blocks:

- `_getpty(&fd, O_RDWR | O_NOCTTY, 0600, 0)` instead of `posix_openpt`,
  then `fcntl` for `O_NONBLOCK` and `FD_CLOEXEC`;
- `pipe()` plus `fcntl(FD_CLOEXEC)` instead of `pipe2(O_CLOEXEC)`;
- child: reset the signal mask and all handlers, `setsid()`, then
  `open(slave, O_RDWR)` to acquire the controlling terminal (no
  `TIOCSCTTY`);
- **`VINTR` set to `^C` explicitly** next to `VERASE` = `^H`: a fresh
  IRIX pty starts with SysV control characters, so a shell got DEL as its
  interrupt character. xwsh avoids the problem by pushing a complete
  `termio` (`TCSETA`) built from its own settings;
- no `IUTF8`;
- no `TIOCSIG`: `shl_pty_signal` returns `ENOSYS` on IRIX; signals must go
  to the child's process group with `kill()`;
- no epoll: the bridge is stubbed, and dash uses `XtAppAddInput`.

The slave mode differs from xwsh's: 0600 here, 0622 in xwsh (group and
other may write, so `write(1)` and `talk` reach the terminal).

### 2.3 IMPACT

dash detects `IMPACT` in `GL_RENDERER` and builds its atlas as
`GL_LUMINANCE_ALPHA` (luminance 255, coverage in alpha) because
`GL_ALPHA` textures did not render on IMPACT ("fixed textures on impact
graphics"); it also enables `GL_TEXTURE_2D` per frame and sets
`GL_MODULATE` explicitly.

### 2.4 Other lessons

- **The whole screen is redrawn every frame** from `tsm_screen_draw` into
  vertex arrays, on a timer (`-burn` sets it to 8 ms). Fine on an
  Octane, wasteful on an Indy, and it ties text rendering to the frame
  rate of the background animations.
- **The pty code is one file with two programs in it**, interleaved by
  `#ifdef __sgi` (2.2), plus more `#ifdef __sgi` at dash's call sites
  (`TIOCCONS`, `putenv` instead of `setenv`). That is the sprawl the new
  design avoids by putting each system in its own file.
- libtsm's model (no reflow, no damage beyond per-cell age counters, no
  graphemes) is what tl replaced.

---

## 3. IRIX 6.5.22 facts that shape the design

Measured on the guest with a MIPSpro probe program:

| Item | IRIX 6.5.22 |
|---|---|
| `_getpty()`, `grantpt()`, `unlockpt()`, `ptsname()` | present (`/dev/ptc` clone device, also `/dev/ptmx`; non-root callers go through the setuid `/usr/sbin/mkpts` helper) |
| `posix_openpt()`, `openpty()`, `forkpty()`, `login_tty()` | missing |
| `TIOCSCTTY` | missing: the controlling terminal is the first tty opened after `setsid()` |
| `O_CLOEXEC`, `pipe2()`, `closefrom()` | missing (`fcntl(FD_CLOEXEC)`, `pipe()`, `getdtablehi()` loop) |
| `IUTF8` | missing |
| `setenv()`, `unsetenv()` | missing (`putenv`) |
| `TIOCSWINSZ`, `SIGWINCH`, `TIOCCONS`, `O_NOCTTY`, `WCONTINUED` | present |
| `NCCS`, `sizeof(struct termios)` | 23, 48 bytes |
| Default visual | 8-bit PseudoColor (`-pseudomap 4sight`) |
| Other visuals (Indy 24-bit board) | 12-bit PseudoColor (4096 entries), 24-bit TrueColor and DirectColor, 2- and 4-bit overlay PseudoColor, 8-bit TrueColor and StaticColor |
| Extensions | MIT-SHM, GLX, DOUBLE-BUFFER, XKEYBOARD, SHAPE, SYNC, XVideo, ... |
| ISO 10646 fonts on the server | **none** (`xlsfonts '*-iso10646-1'` is empty); Latin-1 fonts and `-dec-dectech` fonts only |
