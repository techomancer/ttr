# To The Roots (ttr)

A window manager and small desktop for X11, in the style of the late 1990s:
mwm-like window frames, a Windows 2000-like taskbar, and Motif for every
menu and dialog.

Modern desktops tend to put looks ahead of readability and consistency.
ttr goes the other way. Every window has a frame, buttons look like buttons,
menus look like menus, and things stay where you put them. It is one small
program, written in plain C++98, so that it also builds with old compilers
such as MIPSpro 7.4 on IRIX.

**Status:** usable day to day on Linux under X11. IRIX is a goal: the code
follows its compiler and Motif limits, but it has not been built on IRIX
yet. A Wayland mode is planned for later (Linux only).

## Features

- **Window frames** in the Motif style: title bar, window menu, minimize and
  maximize buttons, resize handles. Double-click the window menu button to
  close a window, or the title bar to maximize it.
- **Focus** by click or by following the mouse.
- **Alt+Tab** either Windows-style (a list of icons, most recently used
  first) or mwm/CDE-style (cycles through the stack).
- **Taskbar** with a Start menu, a task button per window, a quick launch
  bar, a notification area (system tray) and a clock.
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
- **Settings dialog** (Start > Settings...) for all of the above, stored in
  a plain text file you can also edit by hand.

## Building

### What you need

- A C++98 compiler: gcc, clang, or MIPSpro on IRIX.
- Motif 2.1 or later (Open Motif 2.3 on Linux), Xt and Xlib.
- libXpm and libpng, for icons.
- libdbus-1, for desktop notifications. This is optional (see below).
- `make`: GNU make, BSD make and IRIX smake all work.

Package names on some distributions:

| Distribution | Packages |
|---|---|
| Gentoo | `x11-libs/motif x11-libs/libXt x11-libs/libXpm media-libs/libpng sys-apps/dbus` |
| Debian, Ubuntu | `libmotif-dev libxt-dev libxpm-dev libpng-dev libdbus-1-dev` |
| Arch, CachyOS | `openmotif libxt libxpm libpng dbus` |

### Build

SVG icon support comes from [nanosvg](https://github.com/memononen/nanosvg),
which is a git submodule:

```sh
git clone --recursive <repository URL>
cd ttr
make
```

In a checkout made without `--recursive`, run
`git submodule update --init` first. The program is `build/ttr`.

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
leave out `TTR_HAVE_PNG`, `TTR_HAVE_XPM` or `TTR_HAVE_SVG` (and the
matching library) to drop an icon format. The top of the
[Makefile](Makefile) has an example command line for IRIX with MIPSpro.

There is no `make install` yet. Copy `build/ttr` to a directory on your
`PATH` if you want it there.

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

Start ttr from `~/.xinitrc` (for `startx`) or `~/.xsession` (for display
managers that offer it):

```sh
#!/bin/sh
exec ttr
```

If you use a display manager that lists sessions from
`/usr/share/xsessions`, add a file such as `ttr.desktop` there:

```ini
[Desktop Entry]
Name=To The Roots
Comment=mwm-style window manager with a taskbar
Exec=ttr
Type=Application
```

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

The Alt+function keys are mwm's and CDE's. The docking keys use the Windows
(Super) key, which CDE never used, so the two sets do not collide. You can
add keys of your own to start programs in Settings > Start Menu > Edit
Shortcuts....

### Settings

Start > Settings... opens the Settings dialog. Pick a category on the left:

- **Windows:** focus, Alt+Tab style, docked width.
- **Desktops:** the number of virtual desktops.
- **Appearance:** fonts for window titles, menus and the taskbar.
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

All of them are plain text. The Settings dialog keeps comments and lines it
does not know in `settings`. It rewrites `shortcuts` and `startup` when it
saves them, so comments added to those two by hand are lost. The settings are also X resources (for example `Ttr*focusPolicy: pointer`
in `~/.Xresources`). The command line overrides the settings file, and the
settings file overrides `~/.Xresources`.
[docs/wm-design.md §5.2](docs/wm-design.md) lists every setting.

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

When you add or change a feature that people use, update this README too.

## License

BSD 3-Clause. See [LICENSE](LICENSE).
