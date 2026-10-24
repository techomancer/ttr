# To The Roots (ttr)

A window manager and small desktop for X11, in the style of the late 1990s:
mwm-like window frames, a Windows 2000-like taskbar, and Motif for every
menu and dialog.

Modern desktops tend to put looks ahead of readability and consistency.
ttr goes the other way. Every window has a frame, buttons look like buttons,
menus look like menus, and things stay where you put them. It is one small
program, written in plain C++98, so that it also builds with old compilers
such as MIPSpro 7.4 on IRIX.

**Status:** usable day to day on Linux under X11. On IRIX 6.5 it builds
and links with MIPSpro 7.4 (see "Building on IRIX"), but it has not been
run as a window manager there yet. A Wayland mode is planned for later
(Linux only).

## Features

- **Window frames** in the Motif style: title bar, window menu, minimize and
  maximize buttons, resize handles. Double-click the window menu button to
  close a window, or the title bar to maximize it.
- **Focus** by click or by following the mouse.
- **Alt+Tab** either Windows-style (a list of icons, most recently used
  first) or mwm/CDE-style (cycles through the stack).
- **Taskbar** with a Start menu, a task button per window, a quick launch
  bar, a notification area (system tray) and a clock. Drag the task
  buttons into any order, and ttr keeps it when it restarts.
- **Start menu** built from the installed applications (XDG `.desktop`
  files on Linux, toolchest files on IRIX), with icons (PNG, XPM, SVG).
- **Virtual desktops** (4 by default, up to 16).
- **Docking and snapping:** drag a window to a screen edge, or use the
  keyboard, to fill the left, center or right third (or half) of the screen,
  or the top or bottom half.
- **Shortcuts:** programs with an optional icon, a keyboard shortcut and a
  place on the quick launch bar.
- **Notification area indicators:** volume (PipeWire, PulseAudio or ALSA),
  network (addresses, traffic, Wi-Fi), and removable drives (mount, unmount,
  eject).
- **Desktop notifications** from applications (the freedesktop.org service
  on D-Bus), shown as small pop-ups above the taskbar.
- **Startup programs:** a list in Settings, an `autostart.sh` script, and
  optionally the desktop's XDG autostart entries.
- **UI scale** from 100% to 300%, for high-resolution screens.
- **Color themes:** ttr's own and the 30 IRIX desktop color schemes
  (Gotham, Potrero, Lascaux, ...), with your own colors on top, and mwm's
  color resources.
- **Settings dialog** (Start > Settings...) for all of the above, stored in
  a plain text file you can also edit by hand.
- **ttrt**, a terminal emulator in the same style (early; see "The
  terminal" below).
- **ttrc**, To The Roots Commander, a two-panel file manager for any
  terminal in the Norton Commander tradition (early; see "The file
  manager" below).
- **ttre**, a text editor for any terminal: ttrc's F4, and usable as your
  `$EDITOR` (see "The editor" below).
- **ttrcode**, a Motif code editor in the spirit of Visual Studio 6:
  a workspace tree, tabs, a terminal pane and full screen with F11
  (early; see "The code editor" below).

## Building

### What you need

- A C++98 compiler: gcc, clang, or MIPSpro on IRIX.
- Motif 2.1 or later (Open Motif 2.3 on Linux), Xt and Xlib.
- libXpm and libpng, for icons; libjpeg, giflib and libtiff for images in
  ttrc's viewer. Each but libXpm is optional (see below).
- libdbus-1, for desktop notifications. This is optional (see below).
- `make`: GNU make, BSD make and IRIX smake all work.

Package names on some distributions:

| Distribution | Packages |
|---|---|
| Gentoo | `x11-libs/motif x11-libs/libXt x11-libs/libXpm media-libs/libpng media-libs/libjpeg-turbo media-libs/giflib media-libs/tiff sys-apps/dbus` |
| Debian, Ubuntu | `libmotif-dev libxt-dev libxpm-dev libpng-dev libjpeg-dev libgif-dev libtiff-dev libdbus-1-dev` |
| Arch, CachyOS | `openmotif libxt libxpm libpng libjpeg-turbo giflib libtiff dbus` |

### Build

SVG icon support comes from [nanosvg](https://github.com/memononen/nanosvg),
which is a git submodule:

```sh
git clone --recursive <repository URL>
cd ttr
make
```

In a checkout made without `--recursive`, run
`git submodule update --init` first (the submodules bring nanosvg and
ttrt's Unicode fonts, and stb_image, ttr's built-in image decoder). The programs are `build/ttr`,
`build/ttrt` (the terminal), `build/ttrc` (the file manager), `build/ttre`
(the text editor) and `build/ttrcode` (the code editor).

Other build options:

```sh
make CXX=clang++            # build with clang
make DBUS_CFLAGS="`pkg-config --cflags dbus-1`"
                            # if the D-Bus headers are not found
make asan                   # a debug build with AddressSanitizer: build-asan/ttr
make clean
```

To build without D-Bus (no desktop notifications from applications), remove
`-DTTR_HAVE_DBUS` from `CPPFLAGS` and `-ldbus-1` from `LIBS`. Similarly,
leave out `TTR_HAVE_XPM` or `TTR_HAVE_SVG` (and the matching library) to
drop an icon format. The image libraries are named by `TI_CPPFLAGS`
(`-DTTR_HAVE_PNG -DTTR_HAVE_JPEG -DTTR_HAVE_GIF -DTTR_HAVE_TIFF`) and
`TI_LIBS` (`-lpng -ljpeg -lgif -ltiff`); drop one and PNG, JPEG or GIF
are read by the built-in decoder instead, while TIFF is not read at all
(see [ti/DESIGN.md](ti/DESIGN.md)). `make ti-test` decodes the test
images with both. The top of the
[Makefile](Makefile) has an example command line for IRIX with MIPSpro.

### Building on IRIX

On an IRIX 6.5 machine with MIPSpro 7.4, the Motif development files,
libXpm and libpng, in the source directory:

```sh
make B=build-irix CXX=CC WARNFLAGS= DBUS_CFLAGS= \
     CXXFLAGS="-n32 -O2 -LANG:exceptions=OFF -no_prelink -ptused -woff 3970" \
     CPPFLAGS="-Isrc -I/usr/Motif-2.1/include -DTTR_HAVE_XPM -DTTR_HAVE_SVG" \
     LDFLAGS=-L/usr/Motif-2.1/lib32 LIBS="-lXm -lXt -lXpm -lX11 -lm" \
     TI_CPPFLAGS="-DTTR_HAVE_PNG -DTTR_HAVE_JPEG -DTTR_HAVE_TIFF" \
     TI_LIBS="-lpng -ljpeg -ltiff -lz" STB_CFLAGS=-c99 \
     TP_SYS=build-irix/tp_sys_irix.o
```

`TP_SYS` selects the IRIX pseudo-terminal code that ttrt needs.

IRIX 6.5 installs Motif 1.2 as the default `Xm` headers and library; ttr
needs Motif 2.1 (the `motif21_dev` product), which lives in
`/usr/Motif-2.1`, hence the include and library paths. IRIX 6.5's own
libpng 1.2.5, libjpeg 6a and libtiff read images; it has no giflib, so
GIF goes to the built-in decoder, whose source needs C99 mode
(`STB_CFLAGS=-c99`). `-woff 3970` silences
MIPSpro's warning about the pointer cast inside every `XtSetArg`.
`B=build-irix` keeps the IRIX objects apart from a Linux build of the same
tree, which matters when the tree is shared over NFS. `-no_prelink
-ptused` makes MIPSpro instantiate templates in each object instead of
through its prelinker, whose `ii_files/` directory does not work on an
NFS mount.

`tools/irix-build.sh` runs exactly this build inside the iris Indy
emulator (see `tools/iris.sh` and the comments in both scripts): the
emulator exports the source tree over NFS, the guest builds it, and the
result appears in `build-irix/`.

### Installing

```sh
make
sudo make install           # to /usr; or: sudo make install PREFIX=/usr/local
```

This installs:

| File | What it is |
|---|---|
| `/usr/bin/ttr` | the window manager and desktop |
| `/usr/bin/ttrt` | the terminal |
| `/usr/bin/ttrc` | the file manager |
| `/usr/bin/ttre` | the text editor (ttrc's F4) |
| `/usr/bin/ttrcode` | the code editor |
| `/usr/share/ttrc/tc.sh`, `tc.csh` | the `tc` command for your shell (see "The file manager") |
| `/usr/share/ttr/fonts/` | ttrt's Unicode fonts (public domain misc-fixed BDF) |
| `/usr/share/terminfo/t/ttrt` | ttrt's terminfo entry (`/usr/share/lib/terminfo` on IRIX; with `DESTDIR` it goes under `DESTDIR`, never into the system directory; `TERMINFO_DIR` names another place) |
| `/usr/bin/ttr-session` | starts a ttr session (see below) |
| `/usr/share/xsessions/ttr.desktop` | makes display managers offer "To The Roots" |
| `/usr/share/ttr/schemes/` | the color themes (`ttr`, `irix`) and text color schemes (`text`) |

You can install while ttr is running: the old program is renamed to
`ttr.old` and keeps running, and Start > Restart ttr switches to the new
one. `sudo make uninstall` removes them again (with the same `PREFIX`). Build
with the `PREFIX` you install to (`make PREFIX=/usr/local`, then `sudo make
install PREFIX=/usr/local`): ttr looks for its themes under
`PREFIX/share/ttr`. Run from the source tree, it finds them in `data/`.
`DESTDIR` is supported for packaging. Most display managers only look in
`/usr/share/xsessions`, so keep `PREFIX=/usr` if you log in through one.

On **Arch Linux** (and CachyOS, Manjaro), build a package instead, so that
pacman tracks the files:

```sh
cd dist/arch
makepkg -si
```

On **IRIX**, build an inst package (a `.tardist`) and install it with `inst`
or Software Manager. `tools/irix-dist.sh` builds it in the IRIX test guest
(see "Building on IRIX"); the version defaults to today's date:

```sh
tools/irix-dist.sh                      # writes build-irix/ttr-VERSION.tardist
inst -f ttr-VERSION.tardist             # on IRIX, as root
```

The package has three subsystems: `ttr.sw.wm` (ttr and its themes),
`ttr.sw.term` (ttrt, its fonts and a Desktop toolchest entry) and
`ttr.sw.commander` (ttrc, with `/usr/bin/tc` as a link to it, since IRIX has
no other `tc`). Installing it compiles ttrt's terminfo entry into
`/usr/share/lib/terminfo` with `tic`, and removing it deletes the entry.

## Running

### Try it in a window first

`tools/nested.sh` starts a nested X server (Xephyr) on display `:5` and runs
ttr in it, so you can try ttr without leaving your current desktop:

```sh
tools/nested.sh                     # Xephyr on :5, 1024x768
DPY=:7 SIZE=1280x800 tools/nested.sh
tools/nested.sh xnest               # Xnest instead of Xephyr
```

Programs started from ttr's Start menu open inside the nested display. To
start one from a terminal outside, set `DISPLAY`, for example
`DISPLAY=:5 xterm &`. Ctrl+C in the terminal running `nested.sh` stops
both ttr and the nested server.

### As your desktop

After `make install`, log out. Your display manager (SDDM, LightDM, GDM,
...) now lists **To The Roots** among its sessions.

With `startx`, put this in `~/.xinitrc`:

```sh
#!/bin/sh
exec ttr-session
```

`ttr-session` sets `XDG_CURRENT_DESKTOP=TTR` (unless the display manager has
already set it) and runs ttr. Desktop notifications and single-instance
programs need a D-Bus session bus. When there is none, which is common
without systemd, `ttr-session` starts one for the session with
`dbus-run-session`. Running plain `ttr` works too, but without that help.

ttr does not replace a window manager that is already running. It prints
"another window manager is already running" and exits.

When ttr exits or crashes, your programs keep running. Their windows go
back to the plain root window, and a new ttr picks them up again.
"Restart ttr" in the Start menu restarts ttr in place, keeping every
window. Pressing OK in Settings does the same.

### Command line

| Option | Meaning |
|---|---|
| `-focus click` / `-focus pointer` | focus policy, overriding the settings file |
| `-switch windows` / `-switch stack` | Alt+Tab style |
| `-xrm 'Ttr*name: value'` | set any setting for this run |
| `-display host:n` | the X display to manage |
| `ttr -notify SUMMARY [BODY]` | show a notification in the running ttr, then exit |

## Using it

### Keyboard

| Keys | Action |
|---|---|
| Alt+Tab, Alt+Shift+Tab | switch windows |
| Alt+Esc, Alt+Shift+Esc | circulate the window stack |
| Alt+Space, Shift+Esc | window menu |
| Ctrl+Esc, Windows key | Start menu |
| Alt+F3 / F4 | lower / close the window |
| Alt+F5 | restore (and undock) |
| Alt+F7 / F8 | move / resize with the keyboard |
| Alt+F9 / F10 | minimize / maximize |
| Ctrl+Alt+Left / Right | previous / next desktop |
| Alt+Shift+Left / Right | take the active window to the previous / next desktop |
| Super+1 / 2 / 3 | dock left / center / right (again: restore) |
| Super+Left / Right | dock left / right |
| Super+Up / Down | snap to the top / bottom half |
| Ctrl+Super+Left / Right | move the active window's taskbar button one place |
| Ctrl+Super+Home | move the active window's taskbar button to the start |
| Super+D | show the desktop: minimize all windows; again brings them back |
| Super+M / Super+Shift+M | minimize all windows / bring them back |

The Alt+function keys are mwm's and CDE's. The docking keys use the Windows
(Super) key, which CDE never used, so the two sets do not collide. You can
add keys of your own to start programs in Settings > Start Menu > Edit
Shortcuts....

### Settings

Start > Settings... opens the Settings dialog. Pick a category on the left:

- **Windows:** focus, Alt+Tab style, docked width.
- **Desktops:** the number of virtual desktops.
- **Appearance:** UI scale, scroll bars, and fonts for window titles,
  menus and the taskbar.
- **Colors:** the color theme, with a preview, and your own colors for the
  background, text and window frames.
- **Taskbar:** clock format, how long notifications stay up.
- **Start Menu:** where programs come from, the terminal to use, shortcuts.
- **Startup:** programs to start with ttr, and the startup script.
- **XDG Autostart:** the desktop's autostart entries, off by default.

OK saves the settings and restarts ttr in place.

### Files

ttr keeps its files in `~/.config/ttr/`, or in `$XDG_CONFIG_HOME/ttr/` if
that variable is set:

| File | Contents |
|---|---|
| `settings` | one `name: value` per line; comments start with `#` |
| `shortcuts` | programs for the quick launch bar and keyboard shortcuts |
| `startup` | programs started with ttr |
| `autostart.sh` | a shell script run when ttr starts |
| `themes/` | your own color themes, `<Name>.ad` (see [Colors and themes](#colors-and-themes)) |

All of them are plain text. The Settings dialog keeps comments and lines it
does not know in `settings`. It rewrites `shortcuts` and `startup` when it
saves them, so comments added to those two by hand are lost. The settings
are also X resources (for example `Ttr*focusPolicy: pointer` in
`~/.Xresources`). The command line overrides the settings file, and the
settings file overrides `~/.Xresources`.
[docs/wm-design.md §5.2](docs/wm-design.md) lists every setting.

### Colors and themes

Settings > Colors picks the color theme. The preview shows an inactive
window, the active one and the taskbar, drawn the way ttr draws them. The
themes are:

- **Classic**, ttr's own: grey widgets, blue active frames.
- **Emerald** (green and blue), **Windows 2000** and **CDE**, also ttr's.
- **The 30 IRIX desktop color schemes**: Gotham, Potrero, Lascaux (SGI's
  default), Base (the Indigo Magic look) and the rest.
- Your own, from `~/.config/ttr/themes/<Name>.ad`.

A theme colors Motif's widgets (buttons, text fields, lists, scroll bars,
toggles), menus, the taskbar, notifications and window frames.

Under **Your colors** you can replace six of the theme's colors with your
own: the background, text, and the active and inactive title bars and
title text. Click a color to open the color picker. It has sliders for
red, green and blue, a field for a hex value such as `#2344d9` (the `#` may
be left out), and a row of basic colors. **Theme** next to a color goes
back to the theme's. Your colors stay when you pick another theme. In the
settings file they are `colorBackground`, `colorForeground`, `colorActive`,
`colorActiveText`, `colorInactive` and `colorInactiveText`.

**Theme files** are X resource files, one color per role. See
[data/schemes/irix/README.md](data/schemes/irix/README.md) for the roles
and [data/schemes/ttr/README.md](data/schemes/ttr/README.md) for ttr's own
format. Copying one of `data/schemes/ttr/*.ad` into `~/.config/ttr/themes`
and changing it is the easiest way to start. On IRIX, ttr uses each
scheme's colors for the system gamma (`gamma` command) when the scheme
has them, as SGI's desktop does.

**mwm's resources.** With the Classic theme, `Mwm*activeBackground`,
`Mwm*activeForeground`, `Mwm*background` and `Mwm*foreground` from
`~/.Xresources` color the window frames, as they would under mwm. A theme
you choose wins over them. A line for every program, such as
`*background: grey`, does not count as one of these.

### Starting programs with ttr

When ttr starts, it starts these, in order:

1. The XDG autostart entries in `~/.config/autostart` and
   `/etc/xdg/autostart`, if turned on in Settings > XDG Autostart. They are
   off by default, because they usually include sound servers and applets
   your setup may already start some other way.
2. The programs on Settings > Startup.
3. `~/.config/ttr/autostart.sh`, run with `sh`. End lines that start
   long-running programs with `&`.

None of these run again when ttr restarts in place.

### The terminal (ttrt)

`ttrt` is ttr's terminal emulator: xterm-compatible, with scrollback that
re-wraps when the window is resized, and fast on an 8-bit Indy display.
It is early work. Unicode text works everywhere: where the X server's
fonts lack a character (IRIX servers have only Latin-1 fonts), ttrt draws
it from the bundled misc-fixed fonts, which cover Latin, Greek, Cyrillic,
Hebrew, Thai, many symbols, and combining accents. Chinese, Japanese and
Korean characters are not in them and show as boxes. Box drawing and
block characters always work; ttrt draws them itself. To make it the terminal
ttr starts, set Settings > Start Menu > Terminal command to `ttrt`.

```sh
ttrt                        # your shell, 80x24
ttrt -geometry 120x40 -fn 9x15 -fg wheat -bg '#202020'
ttrt -ls                    # as a login shell
ttrt -e top                 # run a program instead of the shell
```

| Option | Meaning |
|---|---|
| `-geometry COLSxROWS+X+Y` | size in characters and position |
| `-fn FONT` | font (an X font name); default: the best fixed-width font the server has. The bundled Unicode fonts are matched to its character size |
| `-fg COLOR`, `-bg COLOR`, `-cr COLOR` | text, background and cursor colours |
| `-sl LINES` | lines of scrollback (default 2000) |
| `-ls`, `+ls` | start the shell as a login shell, or not |
| `-nomenu` | no menu bar |
| `-vb` | flash the window instead of beeping |
| `-title TEXT` | window title (programs can change it) |
| `-renderer NAME` | how text is drawn: `core` (X11 server drawing, the default and fastest on an Indy), `soft` (a TrueColor framebuffer, through MIT-SHM on a local display), `gl` (OpenGL) or `auto` |
| `-dynbg NAME` | an animated background behind the text: `night`, `sunset`, `ocean`, `aurora`, `embers` or `static` (needs OpenGL; `auto` then picks it) |
| `-xrm 'ttrt.NAME: VALUE'` | any setting below, for this window |
| `-e PROGRAM ARGS...` | run a program instead of the shell; must come last |

ttrt has its own settings and does not need ttr: use it under any window
manager, and use any terminal under ttr. Terminal > Settings... (or
Ctrl+right click in the window, which works with the menu bar hidden)
sets the font, colors, scrollback, login shell, menu bar, Backspace key
and bell. OK and Apply change the window you opened it from right away,
except scrollback and the login shell, which apply to new windows. They
are saved in `~/.config/ttrt/settings` (or `$XDG_CONFIG_HOME/ttrt/`),
which you can also edit by hand:

```
font: 9x15
foreground: wheat
bell: visual
```

| Setting | Values | Default |
|---|---|---|
| `font` | an X font name | the best fixed-width font the server has |
| `foreground`, `background`, `cursorColor` | X color names or `#rrggbb` | `gray90`, `black`, the text color |
| `saveLines` | lines of scrollback | `2000` |
| `loginShell`, `menuBar` | `true`, `false` | `false`, `true` |
| `boldIsBright` | `true`: bold text in the 8 basic colors uses the bright ones | `true` |
| `backspace` | `del` or `bs`: what Backspace sends | `del` |
| `bell` | `audible`, `visual` (flash), `off` | `audible` |
| `colorScheme` | a text color scheme, shared with ttrcode (see "The code editor"): its text, background, cursor and 16 terminal colors; text, background and cursor colors set apart from their defaults still win | none: xterm's colors |
| `renderer` | `auto`, `core`, `soft`, `gl` (new windows) | `auto`: core, or gl for a background |
| `dynamicBackground` | `night`, `sunset`, `ocean`, `aurora`, `embers`, `static`, or empty | none |

The same names work as X resources of class `TTRt` (`TTRt*saveLines:
5000`). The settings file wins over X resources, and the command line wins
over both. The Settings dialog saves only what you change in it, and
removes a line when you set it back to the default, so your X resources
and command line options stay in charge of the rest. ttrt's menus and dialogs default
to Helvetica and 17-pixel scroll bars, like ttr's; X resources such as
`TTRt*XmScrollBar.width` change them.

Keys: Shift+PageUp and Shift+PageDown scroll back and forth, and so does
the mouse wheel (in full-screen programs the wheel sends arrow keys
instead). Typing scrolls back to the bottom. Backspace sends DEL unless
set to BS.

Selecting and pasting works as in xterm:

| Do this | To |
|---|---|
| Drag with button 1 | select text (dragging past the top or bottom scrolls) |
| Double-click, triple-click | select a word, a whole line |
| Click button 3 | extend the selection to there |
| Click button 2, or Shift+Insert | paste the selection (PRIMARY) |
| Ctrl+Shift+C, Edit > Copy | copy the selection to the clipboard |
| Ctrl+Shift+V, Edit > Paste | paste the clipboard |

Selections offer UTF-8 and Latin-1 text, so they paste into IRIX's own
programs (winterm, Motif text fields) and back. When a program in the
terminal uses the mouse itself (vim with `mouse=a`, mc, htop), hold
Shift to select instead. The Edit menu also has Select All; the
Terminal menu has Clear Scrollback and Reset.

ttrt sets `TERM` to `ttrt` when its terminfo entry is installed. `make
install` compiles `data/ttrt.ti` with `tic` into the system's terminfo
directory, where every user's programs find it (`/usr/share/lib/terminfo`
on IRIX, `/usr/share/terminfo` on Linux). Without root, one user can
install it with `tic -o ~/.terminfo data/ttrt.ti` (ncurses) or
`TERMINFO=$HOME/.terminfo tic data/ttrt.ti` (IRIX, where ttrt then sets
`TERMINFO` for its shell, since IRIX's curses does not look in
`~/.terminfo` by itself).
Otherwise it uses the best entry the system has: `xterm-256color` on
Linux, `xterm` on IRIX 6.5, whose old entry knows only 8 colours.
Either way it sets `TERM_PROGRAM=ttrt`.

**Images.** ttrt shows sixel graphics, as xterm and a VT340 do: try
`img2sixel` (libsixel), `chafa -f sixel`, ImageMagick's `convert
picture.png sixel:-`, or ttrc's viewer, which picks sixels in ttrt by
itself. An image scrolls with the text and into the scrollback, and
text written over it replaces it. It is drawn with every renderer; on
an 8-bit display it is dithered to 216 colours. Programs can ask for
the cell size in pixels (`CSI 16 t`, and the pty's size reports it), and
modes 80 (sixel display mode) and 8452 (cursor right of the image) are
there. Images are kept in memory up to 256 MB (32 MB on IRIX); past
that the oldest ones disappear.

Programs in ttrt get Alt+letter and F10: the menu bar has no keyboard
shortcuts of its own, so file managers and editors that use those keys
work as they do in xterm. Open the menus with the mouse.

### The file manager (ttrc)

`ttrc`, To The Roots Commander, is a file manager with two panels, a
command line and a function key bar, in the line of Norton Commander and
Midnight Commander. It runs in any terminal: ttrt, xterm, the Linux
console, winterm and xwsh on IRIX, a vt100 on a serial line. It does not
need ttr or X. Its design is in [ttrc/DESIGN.md](ttrc/DESIGN.md).

```sh
ttrc                        # both panels where they were last time
ttrc ~/src /usr/share       # left and right panel
```

To get `tc`, which starts ttrc and leaves your shell in the directory you
were in when you quit it, add this to your shell's startup file:

```sh
. /usr/share/ttrc/tc.sh             # sh, ksh, bash, zsh (~/.profile, ~/.bashrc)
source /usr/share/ttrc/tc.csh       # csh, tcsh (~/.cshrc)
```

On Linux the function hides iproute2's `/sbin/tc` in your interactive
shell; call that one by its full path.

**The shell.** The command line under the panels runs commands in your
own shell (`$SHELL`), which ttrc starts once and keeps: its aliases,
variables and history stay. ttrc changes nothing in the shell (no prompt
or rc file tricks), so any shell works, including IRIX's sh, ksh, csh and
tcsh. It knows when a command has finished and which directory the shell
is in by asking the system, not the shell. After a command, the panels
come back; Ctrl+O switches between the panels and the shell at any time,
also while a program such as vi runs in it. If you `cd` in the shell, the
panel follows; if you move the panel elsewhere, ttrc sends the shell a
`cd` before your next command (you will see it in the shell's history).

**Archives** open like directories: Enter on a `.tar`, `.tar.gz`,
`.tgz`, `.tar.bz2`, `.tar.Z`, `.zip` or `.jar` file goes into it, and a
single `.gz`, `.bz2` or `.Z` file shows the file it holds. The panel's
title uses Midnight Commander's notation, `file.tar.gz/utar://dir`, and
`cd` on the command line accepts it. Archives are read-only: copy files
out of them with F5, view them with F3.

A plain `.tar` or `.zip` is read where it is. Compressed archives are
unpacked when you enter them, into memory up to 8 MB and beyond that into
a temporary file in `$TMPDIR` (or `/tmp`), which is deleted as soon as it
is created, so nothing is left behind even if ttrc is killed. A progress
dialog shows while this takes time; Esc stops it. ttrc checks that
`$TMPDIR` has room first (gzip records the unpacked size; bzip2 and
compress do not, so for those it checks as it goes) and keeps 64 MB, or a
twentieth of a small file system, free. If there is not room, set
`TMPDIR` to a bigger place. When F3 or F4 run your own pager or editor on
a file in an archive, the copy they get is in `$TMPDIR/ttrc.<pid>`,
which ttrc removes when it quits.

**Viewer and editor.** F3 shows a file in ttrc's viewer (text, wrapped
or not, or hex; F7 or `/` searches for text or, with Hex bytes chosen,
for bytes such as `de ad be ef`; `n` finds the next; F5 or `g` goes to a
line, or in hex to a file offset such as `0x1f40`, `1f40h` or `8000`;
`q` quits); it also reads files inside archives.

Images (PNG, JPEG, GIF, TIFF, BMP, PNM and SGI `.rgb`) show as pictures,
fitted to the screen: through kitty's graphics protocol in kitty and
ghostty, iTerm2's in iTerm2 and WezTerm, sixels in ttrt, foot, mlterm, konsole
and Windows Terminal, and in half blocks (two coloured pixels a cell)
everywhere else, including inside tmux. F4 or `h` shows the bytes and F6
or `i` the picture again. Options > Appearance > "Images in the viewer"
picks the way, or Off to always see the bytes; ttrc's settings call it
`viewerImages` (`auto`, `blocks`, `kitty`, `sixel`, `iterm`, `off`).

F4 edits the file in ttre, ttr's editor
(see "The editor (ttre)" below), and Shift+F4 asks for the name of a new
file to edit. A file in an archive is edited as a temporary copy. Options
> Configuration switches F3 and F4 to your `$PAGER` and `$EDITOR` (or
`$VISUAL`, or `vi`) instead.

| Key | Does |
|---|---|
| Up, Down, PgUp, PgDn, Home, End | move in the panel |
| Left, Right | move between columns in the brief listing |
| Enter | open the directory or archive under the cursor (`..` goes up), run a program, or open a file with its association |
| Ctrl+PgUp, Ctrl+PgDn | go up, go into |
| Tab | switch panels |
| Insert, Ctrl+T | mark a file and move down |
| `+`, `-`, `*` | mark files matching a pattern, unmark them, invert the marks (on an empty command line) |
| F1 | help |
| F2 | the user menu |
| F3, F4 | view, edit (Shift+F4: a new file) |
| F5, F6 | copy, rename or move (the marked files or the one under the cursor) |
| F7, F8 | make a directory, delete |
| F9 | the menu bar |
| F10, Esc then 0 | quit (after asking; Options > Configuration can turn the question off) |
| Ctrl+X then c, o, s, d | chmod, chown, symbolic link, compare the panels' directories |
| Ctrl+\ | the directory hotlist |
| Alt+? | find file |
| Alt+T | full or brief listing |
| Alt+. | show or hide files starting with a dot |
| Ctrl+F3, F4, F5, F6 | sort by name, extension, time, size (again: reverse) |
| Ctrl+S, Alt+S | search by typing the start of a name |
| Alt+letter | jump to the next name starting with it |
| Ctrl+O | show the shell; again: back to the panels |
| Alt+Enter | put the file name on the command line |
| Alt+P, Alt+N | previous and next command |
| Ctrl+Up, Ctrl+Down | make the panels shorter or taller, to see the shell's output |
| Ctrl+U | swap the panels |
| Ctrl+R | read the directory again |
| Ctrl+L | redraw the screen |
| Ctrl+Z | suspend to the shell (`fg` comes back) |

Typing goes to the command line under the panels; `cd DIR` there changes
the panel's directory. Esc then a digit works as the function key of that
number on terminals without them. The mouse selects files (double-click
opens), scrolls, opens menus and presses buttons. Copies, moves and
deletes show their progress and can be stopped with Esc; when a file
exists, ttrc asks whether to overwrite it, all of them, none, or only
older ones.

ttrc uses what the terminal offers: true colour and Unicode lines in ttrt
and modern Linux terminals, 256 or 8 colours elsewhere, ASCII frames
where the terminal has no line drawing (xwsh and winterm on a stock
IRIX), and bold and reverse on a vt100. It reads terminfo itself, with no
curses library. Options > Appearance picks the theme (classic, midnight,
dark, mono) and can override what the terminal is taken to be.

ttrc keeps its files in `~/.config/ttrc/` (or `$XDG_CONFIG_HOME/ttrc/`):

| File | Contents |
|---|---|
| `settings` | Options > Configuration and Appearance, and where the panels were |
| `hotlist` | the directory hotlist, one directory per line |
| `menu` | the F2 user menu (below) |
| `assoc` | what Enter does with files (below) |

The user menu has an entry per line, a key and a title, with the
commands under it, indented. Without a `menu` file ttrc offers a few
examples:

```
t Pack the marked files into archive.tar
    tar cf archive.tar %s
c Compare the current file with the other panel's
    diff %f %F | more
```

`assoc` has lines of patterns and a command:

```
*.tar.gz *.tgz: tar tzvf %f | more
*.html: lynx %f
*.c *.h: vi %f
```

In both, `%f` is the file under the cursor, `%F` the other panel's, `%d`
and `%D` the panels' directories, `%s` the marked files (or the one under
the cursor) and `%t` only the marked ones, all quoted for your shell;
`%%` is a percent sign.

### The editor (ttre)

ttre edits one file in a terminal, with the editing, syntax colors and
options of ttrcode's editor and the keys of mc's. ttrc's F4 runs it,
and you can make it your `$EDITOR`:

```sh
ttre notes.txt
ttre +120 src/main.c        # at line 120
export EDITOR=ttre VISUAL=ttre
```

| Key | Does |
|---|---|
| F2, Shift+F2 (Ctrl+S) | save, save under another name |
| F3 | start a block; F3 again marks it (Shift+arrows select too); it stays put while you move |
| F5, F6, F8 | copy the block to the cursor, move it there, delete it |
| Tab, Shift+Tab | indent; with a block or selected lines, indent or outdent them |
| F7, Shift+F7, F4 | search (Match case, Whole words, Backwards), search again, replace all |
| Alt+L (Ctrl+L) | go to a line |
| Ctrl+U (Ctrl+Z), Alt+R | undo, redo |
| Ctrl+Y | delete the line |
| Ctrl+arrows, Home, End | word left or right; Home goes to the first non-blank, then column 0 |
| Ctrl+Insert, Shift+Delete, Shift+Insert | copy, cut, paste |
| F9 | the menu: File, Edit, Search, Options |
| F1 | the keys |
| F10, Esc | quit (asks about unsaved changes) |

The mouse places the cursor, selects by dragging, selects a word with a
double click, and scrolls with the wheel. Undoing back to the saved text
counts as unchanged.

F9 > Options > Editor sets:
- the tab width, how far Tab indents, and whether it inserts spaces;
- whether Enter keeps the indentation;
- line numbers, the current line, and visible spaces and tabs;
- the column marker (80; 0 for none);
- the cursor's shape, blinking and color;
- the colors: ttrc's themes for the bars and dialogs, and the text
  either in the theme's colors or in one of the color schemes ttrcode
  and ttrt use.

They are saved in `~/.config/ttre/settings` (or
`$XDG_CONFIG_HOME/ttre/`). C, C++, Python, shell scripts and makefiles get
syntax colors, as in ttrcode.

### The code editor (ttrcode)

ttrcode is a code editor for X in the manner of Visual Studio 6.0: it
starts at once, never makes you wait while typing, and every key always
does the same thing. Its text is drawn by ttrt's renderer, its editing is
the same core (te) as ttrc's editor, and its terminal pane is a ttrt
terminal.

```sh
ttrcode                     # the current directory as the workspace
ttrcode ~/src/project main.c util.c
ttrcode -fn 9x15 file.txt
ttrcode -renderer gl -dynbg night    # OpenGL, stars behind the terminal pane
```

The window has the workspace tree on the left (Enter or a double click
opens a directory or a file), tabs over the editor, a terminal running
your shell in the workspace below, and the line, column and line ends in
the status bar. The editor takes all the room the side panes leave; drag
the dividers between them to change their size, which ttrcode remembers.
A tab's x button (or a middle click on the tab) closes the file. File >
Open Workspace... switches the tree to another directory (the box opens in
the current workspace: OK keeps it, a directory in the list goes there);
open files stay open, and the terminal's shell changes to the new
directory too if it is at its prompt, not when a command is running. Names
starting with a dot and build output directories (`build`, `build-*`)
are not shown in the tree.

| Key | Does |
|---|---|
| arrows, Home, End, PgUp, PgDn | move (Shift selects); Home goes to the first non-blank, then column 0 |
| Ctrl+Left, Ctrl+Right | a word left or right |
| Ctrl+Home, Ctrl+End | start, end of the file |
| Ctrl+Up, Ctrl+Down | scroll without moving the caret |
| Tab, Shift+Tab | indent; with lines selected, indent or outdent them |
| Ctrl+Z, Ctrl+Y | undo, redo (also Alt+Backspace and Ctrl+Shift+Z) |
| Ctrl+X, Ctrl+C, Ctrl+V | cut, copy, paste (also Shift+Del, Ctrl+Ins, Shift+Ins) |
| Ctrl+A | select all |
| Ctrl+L, Ctrl+Shift+L | cut the line, delete the line |
| Ctrl+Backspace, Ctrl+Del | delete a word left, right |
| Ctrl+F, F3, Shift+F3 | find, find next, find previous (Match case, Whole words) |
| Ctrl+H | replace |
| Ctrl+Shift+F, Ctrl+Shift+H | find, replace in all the workspace's files |
| F4, Shift+F4 | the next, previous Find in Files result |
| Ctrl+G | go to line |
| Ctrl+N, Ctrl+O, Ctrl+S | new, open, save |
| Ctrl+F4 | close the file (asks about unsaved changes) |
| Ctrl+Tab, Ctrl+Shift+Tab | next, previous file |
| Alt+0, Alt+2 | show or hide the workspace, the terminal pane |
| Ctrl+` | to the terminal and back |
| F11 | full screen with only the text (F11 or Esc to come back); on window managers without full-screen support, such as 4Dwm and mwm, the window drops its frame and covers the screen |

With the mouse: click to place the caret, drag to select (past the edge
scrolls), double-click selects a word, triple-click or a click in the line
numbers selects the line, Shift+click extends, the middle button pastes
the X selection, and the wheel scrolls.

In the terminal pane every key goes to the shell except F11, Alt+0,
Alt+2, Ctrl+` and Ctrl+Tab; the menus' Alt+letter keys are off while it
has the focus. When the shell ends, Enter starts a new one.

Files are saved through a new file that replaces the old one, so a full
disk never leaves half a file; the file's permissions are kept, and a
symbolic link is written through. Files with CRLF line ends stay that
way. When a file changes on disk and has no unsaved changes, ttrcode
reloads it when its window gets the focus again.

**Syntax colors** for C, C++, Python, shell scripts and makefiles, chosen
by the file's name (`.c`, `.cpp`, `.h`, `.py`, `.sh`, `Makefile`, ...) or
its `#!` line; the status bar shows the language. The colors come from the
color scheme (`comment`, `keyword`, `string`, `number`, `preprocessor`).

**Colors.** View > Color Scheme colors the editor and the terminal pane:
Light (the default, black on white) and Dark are built in, and Amber,
Classic Blue, Green Screen, Solarized Light and Solarized Dark come in
`share/ttr/schemes/text`. ttrt can use the same schemes. To make your
own, put a file such as `~/.config/ttr/schemes/Mine.scheme` there with
`key: #rrggbb` lines; the keys are listed in
[data/schemes/text/README.md](data/schemes/text/README.md), and anything
left out is derived. View > UI Theme colors the menus, panes and dialogs
with ttr's themes and the 30 IRIX color schemes, at once and without a
restart. Its default, "Same as ttr", follows the theme chosen in ttr's
Settings; any other choice is ttrcode's own.

**Find and replace in files.** Search > Find in Files (Ctrl+Shift+F)
searches every text file of the workspace, or of another folder, for a
text, with Match case, Whole words and file types such as `*.c *.h`.
Hidden files, build directories, binary files and files over 16 MB are
left out. The matches fill the Find Results page of the bottom pane as
`file(line): text`; Enter or a double click opens one with the match
selected, and F4 and Shift+F4 step through them. Replace in Files
(Ctrl+Shift+H) shows how many matches it will replace in how many files
and asks first. Open files are changed in the editor, unsaved changes
included, and Ctrl+Z takes it back. Other files are saved at once,
through a new file that replaces the old one.

**Options.** Tools > Options... sets:
- the font;
- the tab width, how far Tab indents, and whether it inserts spaces;
- whether Enter keeps the indentation of the line above;
- whether to show line numbers, the current line, and spaces and tabs
  (as `·` and `»`);
- a column marker line (column 80 unless you change it; 0 for none);
- the cursor's shape (bar, block or underline), its blinking and its
  color.

**Settings** are saved in `~/.config/ttrcode/settings` (or
`$XDG_CONFIG_HOME/ttrcode/`) as you change them in Tools > Options, the View
menu or by dragging a divider. The same names work as X resources (`TTRcode*tabWidth: 4`), and
the file wins over them:

| Setting | Values | Default |
|---|---|---|
| `colorScheme` | a scheme's name | `Light` |
| `uiTheme` | a theme's name; left out: the same as ttr | |
| `font` | an X font name | the best fixed-width font the server has |
| `tabWidth` | columns a tab character shows as | `8` |
| `indentWidth` | columns Tab indents by with spaces | `4` |
| `insertSpaces` | `true`: Tab inserts spaces; `false`: a tab | `true` |
| `autoIndent` | `true`: Enter keeps the indentation of the line above | `true` |
| `lineNumbers`, `highlightLine`, `showWhitespace` | `true`, `false` | `true`, `true`, `false` |
| `rulerColumn` | the column marker's column; `0`: none | `80` |
| `cursorShape` | `bar`, `block`, `underline` | `bar` |
| `cursorBlink` | `true`, `false` | `true` |
| `cursorColor` | `#rrggbb`; left out: the color scheme's | |
| `treeWidth`, `bottomHeight` | the workspace's width, the terminal pane's height, in pixels | `220`, `220` |
| `renderer` | `auto`, `core`, `soft`, `gl`, as for ttrt (`-renderer`) | `auto` |
| `terminalBackground` | an animated background for the terminal pane, as ttrt's (`-dynbg`) | none |

The plan is in [ttrcode/DESIGN.md](ttrcode/DESIGN.md).

### High-resolution screens

Settings > Appearance > UI scale makes everything ttr draws larger: window
frames, the taskbar, menus, dialogs, notifications and icons. Pick 100,
125, 150, 175, 200, 250 or 300%. The setting is `uiScale` in the settings
file, and any whole number from 100 to 300 works there.

You choose fonts at their normal size, and ttr scales them. X bitmap fonts
exist only in some sizes, so ttr uses the nearest size that is installed.
If text stays small, the fonts are missing: see [Fonts](#fonts).

The scale only applies to ttr. Other programs have their own settings. For
most of them, put this in `~/.Xresources` (192 is 200% of the usual 96):

```
Xft.dpi: 192
Xcursor.size: 48
```

GTK 3 programs also read `GDK_SCALE=2`, and Qt programs read
`QT_SCALE_FACTOR=2`. Set these in `~/.xprofile` or in the script that
starts your session.

### Scroll bars

Settings > Appearance > Scroll bars sets how scroll bars look in ttr's
menus and dialogs:

- **Arrows** at both ends, as Motif usually has them, or **together** at
  the bottom (or right) end, as on NeXT and Mac OS X.
- **Slider grip:** none, a line, ridges (the default, like SGI's) or a dot,
  on scroll bars and on sliders such as the volume control.

These need Motif 2.2 or later (Open Motif 2.3 and Motif 2.5 on Linux have
them). On IRIX, ttr turns on SGI's own enhanced look instead, which has
SGI's scroll bars. Scroll bars and sliders are 17 pixels thick, a little
more than Motif's 11, and grow with the UI scale.

### Fonts

ttr uses the X server's own fonts (core fonts), as mwm does. Menus,
dialogs, window titles and the taskbar therefore all render the same way,
on Linux and on IRIX. Both bitmap fonts and scalable ones work:

- **Bitmap fonts** (the default Helvetica) come in fixed sizes. ttr uses
  the nearest size that is installed.
- **Scalable fonts**, Type1 and TrueType, are drawn by the X server at
  exactly the size asked for, which suits a large UI scale best. They are
  marked with `*` in the font lists on Settings > Appearance.

If a font is not installed, ttr picks the nearest one that is, and says so
on its standard error. With only the X server's built-in `fixed` font,
text cannot get larger with the UI scale.

**What to install.** The classic bitmap fonts have Helvetica up to 34
pixels, which is enough for 250%:

| Distribution | Packages |
|---|---|
| Arch, CachyOS | `xorg-fonts-75dpi xorg-fonts-100dpi xorg-fonts-misc xorg-mkfontscale` (the `dist/arch` package depends on these) |
| Debian, Ubuntu | `xfonts-75dpi xfonts-100dpi xfonts-base` |
| Gentoo | `media-fonts/font-adobe-75dpi media-fonts/font-adobe-100dpi media-fonts/font-misc-misc` |

Good scalable fonts that look like Helvetica are Nimbus Sans (the URW
Type1 fonts: `gsfonts` on Arch, `fonts-urw-base35` on Debian,
`media-fonts/urw-fonts` on Gentoo), Liberation Sans and DejaVu Sans.

**Installed fonts and their index.** The X server never looks at font
files directly. In each font folder it reads a `fonts.dir` file that lists
the fonts there and their names, and a folder without that index is
useless to it. `mkfontscale` and `mkfontdir` write the index. Programs such
as Firefox and GTK or Qt applications do not need it, because they use
fontconfig, which reads the font files itself. That is why a TrueType font
can work in every other program but not in ttr.

When ttr starts, it adds every font folder that has an index to the X
server's font path, like `xset fp+`. It looks under `/usr/share/fonts`,
`/usr/share/X11/fonts`, `/usr/lib/X11/fonts` (IRIX),
`/usr/local/share/fonts`, `~/.local/share/fonts` and `~/.fonts`, and it
reports which folders it added. It also reports the folders that hold
fonts but have no index ("N font folders without fonts.dir"). This
matters because the X server only reads its font folders when it starts.
It leaves out the ones that were missing or empty then, and it never sees
folders nobody told it about, such as `/usr/share/fonts/dejavu`.

To index every font folder once, then use Start > Restart ttr:

```sh
for d in /usr/share/fonts/*/; do sudo mkfontscale "$d"; sudo mkfontdir "$d"; done
```

On Arch, `xorg-mkfontscale` indexes folders by itself when font packages
are installed, but not the ones that were installed before it.

**Font folders** in Settings > Appearance (the `fontPath` setting, folders
separated by colons) is only for fonts outside the folders listed above,
for example `/opt/myfonts`. Those folders need an index too.

**IRIX.** The X server in IRIX 6.5 has Type1 fonts in
`/usr/lib/X11/fonts/Type1`. If your release can also use TrueType fonts,
add their folder under Font folders.

`xset q` shows the font path, and `xlsfonts` lists the fonts (it is in
`xorg-xlsfonts` on Arch).

### Optional helper programs

The notification area uses common tools when they are installed:

- **Volume:** `wpctl` (PipeWire), `pactl` (PulseAudio) or `amixer` (ALSA).
  The volume menu also offers sound panels it finds, such as `pavucontrol`.
- **Removable drives:** `udisksctl` (udisks2), or `pmount`, or `udevil`, to
  mount drives without root. `eject` is used to eject drives.
- **Network:** network configuration programs it finds (for example
  `nm-connection-editor`), for the network menu.

## For developers

- [docs/wm-design.md](docs/wm-design.md) records the design decisions,
  including the C++ dialect: C++98 with no exceptions, RTTI, STL or iostreams.
- [AGENTS.md](AGENTS.md) holds the project principles.
- Builds must stay free of warnings with
  `g++ -std=c++98 -pedantic -Wall -Wextra` and with clang.
- Each object file has an explicit rule in the [Makefile](Makefile), so
  that every make works. A new source file needs an entry in `OBJS` and a
  rule.
- Third-party code lives in `third_party/` as pinned git submodules and is
  never edited in place.
- `tl/` is the terminal emulation library for the coming `ttrt` terminal
  emulator ([tl/DESIGN.md](tl/DESIGN.md)), `tp/` its pty and process
  layer and `tw/` its drawing engine ([tw/DESIGN.md](tw/DESIGN.md)); `tt/`
  puts the three together as a terminal pane any Motif program can embed
  ([tt/DESIGN.md](tt/DESIGN.md)). The application, `src/ttrt/`, is
  described in [ttrt/DESIGN.md](ttrt/DESIGN.md). `make
  tl-test tp-test` builds and runs their tests; `tools/irix-test.sh` does
  the same on the IRIX guest. tp has one implementation file per system:
  Linux and BSD use the default, IRIX builds with
  `TP_SYS=build-irix/tp_sys_irix.o` (the IRIX scripts pass it).
  `tl/unicode_data.cpp` is generated by `tools/gen_unicode.py` from the
  Unicode Character Database.

When you add or change a feature that people use, update this README too.

## License

BSD 3-Clause. See [LICENSE](LICENSE).
