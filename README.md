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
- **UI scale** from 100% to 300%, for high-resolution screens.
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

### Installing

```sh
make
sudo make install           # to /usr; or: sudo make install PREFIX=/usr/local
```

This installs:

| File | What it is |
|---|---|
| `/usr/bin/ttr` | the window manager and desktop |
| `/usr/bin/ttr-session` | starts a ttr session (see below) |
| `/usr/share/xsessions/ttr.desktop` | makes display managers offer "To The Roots" |

`sudo make uninstall` removes them again (with the same `PREFIX`).
`DESTDIR` is supported for packaging. Most display managers only look in
`/usr/share/xsessions`, so keep `PREFIX=/usr` if you log in through one.

On **Arch Linux** (and CachyOS, Manjaro), build a package instead, so that
pacman tracks the files:

```sh
cd dist/arch
makepkg -si
```

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

The Alt+function keys are mwm's and CDE's. The docking keys use the Windows
(Super) key, which CDE never used, so the two sets do not collide. You can
add keys of your own to start programs in Settings > Start Menu > Edit
Shortcuts....

### Settings

Start > Settings... opens the Settings dialog. Pick a category on the left:

- **Windows:** focus, Alt+Tab style, docked width.
- **Desktops:** the number of virtual desktops.
- **Appearance:** UI scale, and fonts for window titles, menus and the
  taskbar.
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

When you add or change a feature that people use, update this README too.

## License

BSD 3-Clause. See [LICENSE](LICENSE).
