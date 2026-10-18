# Design Document: To The Roots (ttr)
### mwm-style window manager · Windows 2000-style taskbar · Motif widgets · C++ that MIPSpro 7.4 can build

*Status: draft, revision 2. The implementation language changed from Rust to a
conservative C++ subset, and Motif now provides every real widget. This is a
living document.*

---

## 1. Vision

A small, consistent desktop shell in the CDE/mwm tradition: a reparenting window
manager with Motif-style frames, plus a Windows 2000-style taskbar (start menu,
task buttons, notification area, clock). It is built for readability and
predictability, not for visual effects.

It runs in two modes, built from one source tree:

1. **X11 mode**: a native reparenting window manager on Xorg (Linux) and,
   later, on IRIX. It is a single monolithic process, the same as mwm.
2. **Wayland mode** (later, Linux only): a Wayland compositor that hosts
   XWayland. The shell UI (taskbar and menus) is the same Motif code, running
   as an X11 client of XWayland. See §4.

Design goals, in priority order:

- **Builds with old compilers.** The portable code compiles with MIPSpro 7.4,
  with g++/clang in `-std=c++98` mode, and with whatever gcc an older
  distribution ships. This is a firm constraint (§2).
- **Motif for widgets.** Menus, the taskbar, dialogs and the configuration UI
  are Motif/Xt widgets, so they look and behave like a Motif desktop without
  any toolkit of our own.
- **X11-first semantics.** The abstractions follow X11's asynchronous,
  server-authoritative model. Wayland is fitted to that model.
- **Seamless remote X11.** Clients from remote machines (such as IRIX
  workstations over TCP) must feel native. X11 mode gets this from the core
  protocol. Wayland mode gets it from XWayland listening on TCP.
- **User sovereignty.** No permission dialogs. Clients the owner runs are
  trusted, which is X11's trust model, chosen on purpose (§6).
- **IRIX as a real target.** The X11 mode binary must build and run on IRIX
  6.5 with MIPSpro. This limits which dependencies we use and how the code is
  written.

---

## 2. Language and build constraints

### 2.1 The C++ dialect: "MIPSpro C++"

Portable code is written in a conservative subset of C++98. The rule is simple:
**if MIPSpro 7.4 or `g++ -std=c++98 -pedantic` rejects it, or behaves
differently on it, it is not allowed.**

Allowed:
- Classes, single inheritance, abstract interfaces made of pure virtual
  functions.
- Simple templates, used for containers and small helpers only. No template
  metaprogramming, partial specialization tricks, or template template
  parameters.
- Namespaces (one project namespace, `ttr`). `const`, references, `static_cast`
  and `reinterpret_cast`.
- C library headers in the `<string.h>` form, not `<cstring>`, because that form
  is the most reliable across old compilers.

Not allowed:
- **Anything from C++11 or later**: `auto`, `nullptr`, `override`, range-for,
  `enum class`, `static_assert`, lambdas, rvalue references, `>>` closing two
  template lists, `long long` literals without a typedef, `<stdint.h>`.
- **Exceptions.** Build with `-fno-exceptions` (gcc/clang) or
  `-LANG:exceptions=OFF` (MIPSpro). Errors are reported through return values.
- **RTTI and `dynamic_cast`.** Build with `-fno-rtti`. Use explicit type tags
  where they are needed.
- **The standard library (STL, iostreams, `std::string`) in portable code.**
  MIPSpro needs `-LANG:std` for the new-style library, the old and new
  iostreams do not mix well, and SGI STL and libstdc++ differ in small ways.
  We need very few containers, so `core/` provides small ones (`Vector<T>`,
  `String`, `IntMap<T>`) that behave the same on every compiler.
- Reusing a `for`-loop variable name after the loop in the same scope. Older
  compilers disagree about `for`-init scoping, so declare the variable before
  the loop if you need it afterwards.
- Variable-length arrays, designated initializers, `#pragma once`, and other
  compiler extensions.

Portability helpers live in `core/port.h`: fixed-width integer typedefs (from
`<inttypes.h>` on IRIX and `<stdint.h>` elsewhere), a C++98 compile-time assert
(the `typedef char x[cond ? 1 : -1]` trick), `TTR_UNUSED`, and endian
detection.

### 2.2 C for the Wayland glue

wlroots headers are C11 and do not compile cleanly as C++. Translation units
that include wlroots are therefore written in **C** (`*.c`, compiled as C11).
They expose a small `extern "C"` API to the C++ side. This code is Linux-only,
so C11 is acceptable there. The C++ in `wayland/` still follows §2.1 so the
code base reads the same throughout.

### 2.3 Build system

- **Plain `make`** with a single hand-written `Makefile` that also works with
  IRIX `smake` and BSD make. It uses no pattern rules, conditionals,
  functions, `include`, or automatic variables other than `$@`.
- Every object has an explicit rule that lists its headers. Objects go flat
  into `build/` (`src/x11/wm.cpp` becomes `build/x11_wm.o`).
- Defaults are for Linux and gcc. Other platforms override variables on the
  command line (`make CXX=CC CXXFLAGS=... WARNFLAGS= LIBS=...`), as shown at
  the top of the Makefile. There is no CMake, autotools or pkg-config.
- `tools/nested.sh [xephyr|xnest]` runs `build/ttr` in a nested X server for
  testing.
- The `make wayland` target is optional and Linux-only. The default target
  builds the X11 mode binary only.

### 2.4 Dialect enforcement in CI

- `g++ -std=c++98 -pedantic -Werror -fno-exceptions -fno-rtti` and the same
  with clang. This catches most C++11 drift.
- A scheduled **MIPSpro build on emulated IRIX** (our Indy emulator), which
  also runs the unit tests there. Cross-compile on fast hardware where
  possible.
- A **big-endian job** (qemu-system-mips or the IRIX run itself). Pixel formats,
  X11 property parsing and any reinterpret casts are where little-endian
  assumptions hide.

---

## 3. Architecture

### 3.1 Layers

```
            +-----------------------------------------------+
  shell/    |  Motif/Xt shell: taskbar, start menu, window  |  portable C++98
            |  menu, tray, run dialog, config UI            |  + Xt + Motif
            +-------------------+---------------------------+
                                | WmLink (interface, §3.4)
            +-------------------+---------------------------+
  core/     |  window model, focus/stacking policy,         |  portable C++98
            |  ToplevelHandle, Painter, frame layout/theme  |  (no X, no Wayland)
            +-----------+-----------------------+-----------+
                        |                       |
            +-----------+-----------+   +-------+-----------------------+
  x11/      | X11 WM backend:       |   | wayland/: compositor          |  Linux only
            | reparenting, ICCCM,   |   | (wlroots), XWayland xwm,      |  C11 glue +
            | EWMH, Motif hints,    |   | software Painter, WmLink      |  C++98
            | XlibPainter           |   | server                        |
            +-----------------------+   +-------------------------------+
```

The key point, unchanged from the first draft: do **not** abstract "a window"
at the protocol level. On X11 the WM is a privileged client that reparents other
clients' windows. On Wayland the WM *is* the display server, and client
surfaces are textures. Instead we abstract (a) client toplevels as an interface
(`ToplevelHandle`) and (b) drawing our own decorations (`Painter`).

### 3.2 Who draws what

| UI element | Drawn by | Why |
|---|---|---|
| Window frames (border, title bar, buttons) | our own `Painter` code, in the WM or compositor process | Frames must resize in step with the client. mwm also draws frames itself instead of using widgets. |
| Taskbar, start menu, window menu, tray, dialogs, config UI | **Motif widgets** in the shell | These are real widgets with focus, keyboard navigation and menus. Motif already does this well. |

The frame theme (colors, bevel widths, title font) is defined once in `core/`.
Shadow and highlight colors are computed with the same algorithm Motif uses
(`XmGetColors`), reimplemented in `core/`, so frames and Motif widgets match.

### 3.3 `Painter`: drawing our decorations

`Painter` is a small abstract interface: fill a rectangle, draw a Motif bevel
(raised or sunken, with a given thickness), draw clipped text, and draw an
icon or pixmap.

| | X11 mode: `XlibPainter` | Wayland mode: `PixelPainter` |
|---|---|---|
| Target | frame window, through a GC | shm buffer used as a scene texture |
| Text | core X fonts (IRIX); Xft optional on Linux | FreeType + fontconfig (cairo/pango are acceptable, since this path is Linux-only) |
| Colors | `XAllocColor`, so 8-bit PseudoColor visuals work (an Indy is commonly 8-bit) | direct 32-bit ARGB |
| Redraw | `Expose`-driven, drawn by the server | damage-driven |

### 3.4 `ToplevelHandle`: client windows

An interface for any client toplevel: geometry, title, class or app id, states
(maximized, fullscreen, activated, minimized), size hints, and **requests**
(close, move, resize, minimize, activate).

Implementations:

1. **`X11Toplevel`** (X11 mode): SubstructureRedirect on the root window,
   MapRequest and ConfigureRequest handling, `WM_PROTOCOLS`/`WM_DELETE_WINDOW`,
   ICCCM hints, the essential EWMH properties, and `_MOTIF_WM_HINTS`, which is a
   primary decoration hint here rather than a legacy shim.
2. **`XdgToplevel`** (Wayland mode): xdg-shell clients.
3. **`XWaylandToplevel`** (Wayland mode): X11 clients running under XWayland.
   They appear as surfaces in the same scene, so the frame code is identical to
   the code for native clients.

`xdg-decoration` and `_MOTIF_WM_HINTS` both reduce to one property: "does this
client want server-side decorations?"

The ICCCM, EWMH and Motif-hints parsing code in `x11/` is written so that the
XWayland xwm path in Wayland mode can reuse it.

Rules that keep the X11 implementation honest:
- State changes are **asynchronous requests that get a confirmation**, never
  properties you can simply set. The client may override geometry, and the X
  server decides stacking in X11 mode.
- The core makes no frame-perfect atomicity assumptions.
- Grabs are modelled as *"exclusive pointer/keyboard until released"*. On X11
  that is `XGrabPointer`/`XGrabKeyboard`. On Wayland it is input routing,
  because we own the seat.

### 3.5 `WmLink`: the shell ↔ WM boundary

The shell never touches the window model directly. It talks to the WM through
`WmLink`, a message interface:

- **WM → shell:** toplevel added, updated or removed (id, title, icon,
  states), focus changed, output layout changed, show the window menu
  (toplevel, x, y), show the start menu (hotkey), reload config.
- **Shell → WM:** activate, minimize, restore, maximize, close, start a
  keyboard move/resize, register one of our X windows with a role (panel, menu,
  dialog), and grab begin/end (§4.4).

There are two implementations:
- **In-process** (X11 mode): direct calls queued onto the event loop.
- **Socket** (Wayland mode): length-prefixed binary records over a
  `socketpair` that the compositor creates when it spawns the shell. Both ends
  run on the same host, so native byte order is fine. The first message is a
  versioned hello.

Toplevel ids are opaque to the shell. In X11 mode they are X window ids. In
Wayland mode they are compositor-assigned, which is how the X11 taskbar can list
**native Wayland windows**.

---

## 4. Wayland mode and Motif: how they fit

### 4.1 The problem

Motif is Xt/Xlib and can only draw to an X server, so it cannot render to a
Wayland surface. XWayland *is* an X server, though, and the compositor is
already an X client of it: every compositor that supports XWayland runs an
"xwm" that connects to XWayland to manage X windows. So the question is not
*whether* the compositor can use XWayland for our Motif UI. It is **which
process holds the Motif connection**.

### 4.2 Options considered

**A. Motif inside the compositor process, on the main thread.**
Rejected. Xlib and Motif make blocking round-trips all the time
(`XGetWindowProperty`, `XGrabPointer`, `XSync`, and so on). XWayland is itself a
Wayland client of the compositor and sometimes waits on it. If both wait on each
other the session deadlocks.

**B. Motif inside the compositor process, on a separate thread.**
This avoids the deadlock, but:
- An Xlib I/O error (for example when XWayland restarts) calls `exit()` by
  default, which would kill the compositor and, with it, **every Wayland client
  in the session**. Workarounds exist (`XSetIOErrorExitHandler`, `longjmp`), but
  Motif cannot be reliably torn down and re-initialized after the display is
  lost.
- Any crash in Motif or the shell takes down the whole session.

**C. Motif in a separate shell process connected to XWayland.** **Chosen.**
- The shell is an ordinary X11 client of XWayland. Its windows are XWayland
  surfaces that the compositor recognizes and places specially.
- If the shell crashes, the compositor respawns it and no client is lost. If
  XWayland restarts, the compositor restarts it and then the shell.
- The shell binary is **the IRIX-portable X11 mode binary**, run as
  `ttr --shell-only --link-fd N`. Wayland mode adds no second copy of the shell
  code.

**D. Our X11 WM managing XWayland, with native Wayland clients drawn inside
proxy X frame windows.** Rejected. It needs cross-process resize
synchronization between the frame and the client surface for every native
client, and it creates two frame-drawing paths for no visible benefit.

### 4.3 What "monolithic" means now

- **X11 mode:** one process, one Xt application context, single-threaded:
  the WM, frames and Motif shell together, the same as mwm. This is the binary
  that ships on IRIX.
- **Wayland mode:** one *project* and one shell codebase, but two processes:
  `ttr-wayland` (the compositor, Linux-only) supervises XWayland and
  `ttr --shell-only`. The shell code is the same; only the `WmLink`
  implementation differs.

### 4.4 Making Motif behave under the compositor

- **Unspoofable identification.** The shell sends its X window ids over the
  private `socketpair` with a role (panel, menu, dialog). The compositor's xwm
  matches XWayland surfaces against that list. We do not rely on window
  properties, which any client could set.
- **Panel placement.** A panel-role window is placed by the compositor at the
  screen edge, its area is excluded from the work area, and it never takes
  keyboard focus unless the start menu is open.
- **Menus and grabs.** Motif menus are override-redirect windows that call
  `XGrabPointer`/`XGrabKeyboard`. The shell hooks the menu shells'
  popup/popdown callbacks and sends `GRAB_BEGIN(window)` and `GRAB_END`. While
  a grab is active, the compositor routes all pointer and keyboard input to that
  XWayland surface, including clicks outside it. XWayland turns those into
  root-relative events, and Motif sees the click outside and closes the menu,
  exactly as on X11. *This must be verified early (milestone 4).*
- **Window menu from a frame.** A click on a frame's menu button (drawn by the
  compositor) sends `SHOW_WINDOW_MENU(id, x, y)`. The shell posts the Motif menu
  there and sends the chosen action back. The local IPC round-trip is well under
  a millisecond.
- **Coordinates.** Rootless XWayland uses the compositor's global layout
  coordinates, so menu positions line up. HiDPI is handled by running XWayland
  unscaled and choosing larger fonts in the theme (see §9).
- **Launching applications.** The shell has both `DISPLAY` and
  `WAYLAND_DISPLAY` set, so programs it starts use native Wayland when they
  support it.
- **Notification area.** XEmbed is X11, so the Motif tray works unchanged for
  X11 and XWayland applications. StatusNotifierItem (D-Bus) is an optional,
  Linux-only module.

---

## 5. Startup

### 5.1 X11 mode (`ttr`)

1. `XtOpenApplication`, then check that a Motif-capable visual and colormap
   exist.
2. Take over window management: claim the ICCCM `WM_S<n>` selection (`--replace`
   asks the running WM to exit), then select SubstructureRedirect on the root
   window. If another WM holds it, print a clear message and exit.
3. Adopt existing windows (`XQueryTree`, then reparent the viewable ones).
4. Create the Motif shell (taskbar, menus) using the in-process `WmLink`, and set
   the panel strut.
5. Run the Xt main loop. WM events for frame windows and the root window are
   routed through Xt itself (`XtRegisterDrawable`/`XtSetEventDispatcher`,
   available since X11R6), so they are dispatched even inside Motif's nested
   loops. We do not filter events in a custom loop in front of
   `XtDispatchEvent`.
6. Just before the main loop, start the startup programs (§5.10), unless this
   is a restart.

On exit or crash, clients survive: they are in our save-set and are reparented
back to the root window.

Pitfalls of running the shell in the WM's own process, found the hard way:

- **Our own dialogs are not redirected.** The X server only redirects map
  requests from *other* clients, so the shell's Motif dialogs (Run, Exit)
  never produce a MapRequest. The WM adopts any ordinary top-level window
  when its MapNotify arrives on the root window.
- **Event masks are per client.** On our own windows the mask is Xt's, so
  the WM must add `PropertyChangeMask` to it, never replace it. Our own
  windows also report their unmapping to themselves; the WM acts only on the
  copy reported to the frame or root.
- **Post Motif menus on button release.** A menu posted while the button is
  still down closes again when it is released outside the menu. Menu actions
  run from a zero-length timeout, after the menu has popped down and given
  back its grab.
- **Use real timestamps for focus.** Toolkits ignore `WM_TAKE_FOCUS` with
  `CurrentTime`; use the last timestamp Xt processed.

### 5.2 Configuration

Settings are X resources, as in mwm. In increasing order of precedence they
come from app-defaults, `~/.Xresources`, the settings file
`~/.config/ttr/settings` (or `$XDG_CONFIG_HOME/ttr/settings`), and the
command line (`-focus`, `-switch`, `-xrm`). The settings file uses the same
names, one `name: value` per line; it is edited by hand or by the Motif
Settings dialog (Start > Settings...), which keeps comments and unknown lines.
The dialog lists categories on the left (Windows, Desktops, Appearance,
Taskbar, Start Menu) and shows one page at a time on the right, sized to the
largest page so switching does not resize the dialog. OK saves every page
at once and restarts ttr in place, because fonts reach Motif
widgets only when they are created. See [src/x11/config.h](../src/x11/config.h):

| Resource | Values | Default |
|---|---|---|
| `Ttr*focusPolicy` (`-focus`) | `click`, `pointer` | `click` |
| `Ttr*switchStyle` (`-switch`) | `windows` (Alt+Tab popup, most recently used first), `stack` (mwm circulation) | `windows` |
| `Ttr*appSource` | `auto`, `desktop`, `toolchest` | `auto`: toolchest on IRIX, desktop elsewhere |
| `Ttr*toolchestFiles` | colon-separated paths | `~/.auxchestrc:/usr/lib/X11/system.chestrc` |
| `Ttr*terminal` | command prefix for `Terminal=true` entries | `xterm -e` |
| `Ttr*clockFormat` | `strftime` format | `%H:%M` |
| `Ttr*titleFont` | core X font (XLFD) for window titles and Alt+Tab | Helvetica bold 12 |
| `Ttr*menuFont` | for Motif menus and dialogs (`*fontList`) | Helvetica bold 12 |
| `Ttr*taskbarFont` | for task buttons and the clock | Helvetica medium 12 |
| `Ttr*desktops` | number of virtual desktops, 1 to 16 | 4 |
| `Ttr*dockWidth` | `third`, `half`: size of docked and snapped windows | `third` |
| `Ttr*notificationTime` | seconds a notification stays up | 5 |
| `Ttr*notificationLog` | file whose new lines become notifications | none |
| `Ttr*autostartScript` | `true`, `false`: run `autostart.sh` at startup (§5.10) | `true` |
| `Ttr*xdgAutostart` | `true`, `false`: start XDG autostart entries (§5.10) | `false` |
| `Ttr*uiScale` | 100 to 300 (percent): scale of everything ttr draws | 100 |
| `Ttr*fontPath` | folders added to the X server's font path at startup (colon separated) | none |
| `Ttr*scrollArrows` | `ends`, `together`: scroll bar arrows at each end, or both at the bottom/right (Motif 2.2+) | `ends` |
| `Ttr*scrollGrip` | `none`, `line`, `ridges`, `dot`: grip on scroll bar and slider thumbs (Motif 2.2+) | `ridges` |
| `Ttr*theme` | color theme name (§5.11) | `Classic` |
| `Ttr*colorBackground`, `colorForeground`, `colorActive`, `colorActiveText`, `colorInactive`, `colorInactiveText` | the user's colors over the theme | none |

**UI scale.** For high-resolution screens, `uiScale` scales ttr in three
ways, all applied once at startup (changing it restarts ttr):

- **Fonts.** The font settings stay at their 100% size, which is what the
  Settings dialog shows and saves. When loading a font, ttr scales its XLFD
  pixel size ([src/x11/config.cpp](../src/x11/config.cpp)
  `resolve_font`). It asks for that exact size first, which scalable Type1
  and TrueType fonts on the server's font path provide. Otherwise it takes
  the nearest bitmap size of the same family. If the family is missing or
  has nothing near (within 10%), it tries common sans serif families
  (Nimbus Sans, Liberation Sans, DejaVu Sans, ...), then the misc fixed
  fonts, then any font, and finally any weight. Aliases such as `fixed`
  are resolved to their XLFD first. Many systems have no bitmap fonts
  beyond the server's built-in `fixed`. The X server drops font folders
  that are missing or empty when it starts, installing fonts later does
  not bring them back, and distributions put TrueType fonts in folders
  the server never knew about (`/usr/share/fonts/<family>`). So at startup
  ttr adds every folder with a `fonts.dir` under the usual font roots
  (Linux, X11R6, IRIX, the user's) to the end of the server's font path,
  like `xset fp+`. The classic folders come first, so a font name keeps
  matching the font it matched before. If the server refuses the request,
  the folders are added one at a time and the bad one is skipped.
  `fontPath` adds more folders.
- **Scalable fonts.** Type1 and TrueType fonts come through the X server
  too (its FreeType or Type1 rasterizer), so Motif widgets and our drawing
  use the same fonts. That matters on IRIX, whose Motif 2.1 can only use
  core fonts. The Settings dialog marks scalable families (pixel size 0 in
  their XLFDs).
  Client-side Xft fonts are not used: IRIX has no Xft, and core fonts keep
  one code path.
- **Our own drawing.** Pixel sizes in frames, the taskbar, the tray, menus,
  pop-ups and dialog layout are written for 100% and go through `scaled()`.
  Lines and bevels go through `scaled_line()`, which grows more slowly (1
  pixel up to 199%) so they do not get heavy
  ([src/core/scale.h](../src/core/scale.h)).
- **Motif widgets.** They size themselves from their fonts. Their
  pixel-sized resources (shadows, focus highlights, scroll bars, slider
  thickness, combo box arrows) are scaled in the resource database, class
  by class, so widgets that have none, such as labels and managers, do not
  get one. An `XmScale` has its own thickness (`scaleHeight` for a
  horizontal one); its scroll bar's width does not apply.

Scroll bars and sliders are 17 pixels thick at 100% (Motif's default is
11), scaled like the rest.

**Scroll bar looks.** Motif 2.2 added `XmNshowArrows` values (both arrows
at one end with `XmMAX_SIDE`) and `XmNsliderMark` (a grip on the thumb).
ttr sets them through the resource database only when they exist, which
it detects with `#ifdef XmNsliderMark`; older Motif would warn about
values it cannot convert. On IRIX it sets `*sgiMode` and
`*useEnhancedSGILook`, so IRIS IM draws SGI's own scroll bars.

Icons are loaded at the scaled size. When a theme lacks that size, a larger
bitmap is scaled down in preference to a smaller one scaled up. At 100%
nothing changes: the screen is pixel-identical to before the setting
existed.

The start menu reads applications through the `AppSource` interface
([src/core/apps.h](../src/core/apps.h)): `DesktopAppSource` reads XDG
`.desktop` files into Windows-like folders, and `ToolchestAppSource` reads
4Dwm toolchest menus (mwm menu syntax, rooted at `Menu ToolChest`). The
toolchest file locations and syntax details still need to be checked
against a real IRIX system.

Menu entries show an icon and text. Motif 2.1 labels hold either a string or
a pixmap, so each label is drawn by us into one pixmap (icon, then text, in
the menu's own colors) and set with `XmNlabelType: XmPIXMAP`. This costs the
mnemonic underline on those entries.

### 5.3 Virtual desktops

Each window is on one desktop or on all of them (docks and other panels
always are). Switching desktops unmaps the frames of the other desktops'
windows; their clients stay mapped inside, so applications do not see an
iconify. The taskbar and Alt+Tab show only the current desktop, as on
Windows, and focus returns to the most recently used window there.
Activating a window on another desktop (`_NET_ACTIVE_WINDOW`) switches to
it.

| Keys | Action |
|---|---|
| Ctrl+Alt+Left/Right | previous / next desktop (no wrap-around) |
| Alt+Shift+Left/Right, Ctrl+Alt+Shift+Left/Right | move the active window to the previous / next desktop and follow it |

The window menu has "Move to Desktop" (including All Desktops), and the start
menu has "Desktops". A small display names the new desktop for a moment
after each switch; there is deliberately no permanent pager. Desktops are
published through EWMH (`_NET_NUMBER_OF_DESKTOPS`, `_NET_CURRENT_DESKTOP`,
`_NET_WM_DESKTOP`, `_NET_DESKTOP_NAMES`, per-desktop `_NET_WORKAREA`), so
external pagers and `wmctrl` work. Windows keep their `_NET_WM_DESKTOP`, and
a restart keeps the current desktop.

**Taskbar order.** The buttons can be put in any order. Drag one sideways
(past a few pixels, so a wobbly click is still a click), use the window
menu's Taskbar submenu (Move to Start, Move Left, Move Right), or press
Ctrl+Super+Home, Ctrl+Super+Left or Ctrl+Super+Right on the active window.
The keys and menu items are window actions (`ACT_TASK_*`): the WM passes
them to the shell with `ShellLink::move_task()`, so they work the same
over a socket in Wayland mode. A move counts among the current desktop's
buttons. The shell keeps the order in `_TTR_TASK_ORDER` on the root
window, so a restart, which adopts windows in stacking order, puts the
buttons back where they were. New windows go at the end.

The task buttons take their button events with an event handler, not
`XmDrawingArea`'s input callback. Its translations include a press
followed by a release, so Xt holds a press back and then reports only the
release, and a drag never sees its press.

### 5.4 Shortcuts and quick launch

Program shortcuts live in `~/.config/ttr/shortcuts`, in order, one
`[shortcut]` block each with `name`, `command`, optional `icon` (theme name or
file) and `key` (e.g. `Ctrl+Alt+T`, `Super+E`; see
[src/x11/keys.h](../src/x11/keys.h)), and `quicklaunch: true|false`. The
WM grabs the keys; built-in bindings take precedence. Shortcuts marked
quicklaunch appear as flat 16 pixel icons right of Start, between grooves,
with a tooltip (Motif 2.1 has none, so we draw it).

Settings > Edit Shortcuts... edits the list: add from the start menu's
programs (an `XmSelectionDialog`) or by hand, browse for commands and icons
(`XmFileSelectionDialog`), reorder with Move Up / Move Down, with a live
icon preview. OK checks every key, saves, and restarts ttr. The dialog
rewrites the file, so comments in it do not survive a save from there.

The Windows key opens Start when released, and only if no other key was
pressed with it, so Super+key shortcuts work.

### 5.5 Volume

The notification area shows the default output's master volume, through
the sound server's own tools so ttr links no audio library
([src/core/audio.h](../src/core/audio.h)). At start the shell probes, in
order, `pactl info` (PulseAudio, or PipeWire through pipewire-pulse),
`wpctl get-volume` (PipeWire without the Pulse layer) and `amixer get
Master` (ALSA); the first that answers is used, and with none there is no
icon. A socket check alone is not enough: a system can have a `pipewire-0`
socket with audio on real PulseAudio.

Every tool runs asynchronously (`CommandRunner`: output read through an Xt
input, killed after a timeout), so a hung sound server cannot hang the
desktop. Changes are fire and forget, then read back; the volume is also
polled every 3 seconds to follow other programs.

Left click opens a Volume dialog (slider, Mute, a choice of sound panel and
Open); right click a menu with Mute, Volume Control... and every installed
panel; middle click toggles mute; the wheel steps by 5%. Panels are found
on `$PATH`: pavucontrol, pwvucontrol, pavucontrol-qt, GNOME and KDE sound
settings, MATE, Helvum and qpwgraph (PipeWire graphs), alsamixer and
pulsemixer (in the configured terminal), and apanel for IRIX.

IRIX: a libaudio backend (`ALgetparams` / `ALsetparams` on the default
output) behind the same interface, with apanel as the panel.

Our own Motif popups (menus, option menus, combo box lists) all take a
pointer grab. Besides menus the shell reports, the WM treats any of our own
override-redirect shells being mapped as a popup being up, and keeps out of
pointer and key events until it is gone.

### 5.6 Network

A network icon left of the volume shows the state of the interface with
the default route: wired, Wi-Fi signal strength, or offline. Everything is
read from the kernel, no tools are run ([src/core/net.h](../src/core/net.h)):
counters from `/proc/net/dev`, state from `/sys/class/net`, the default
route from `/proc/net/route`, IPv4 by `SIOCGIFCONF`, IPv6 from
`/proc/net/if_inet6`, signal from `/proc/net/wireless`, and SSID and BSSID
by the wireless extensions ioctls (still answered by cfg80211 drivers). It
is polled every two seconds; counters are doubles, exact far past 2^32
without `long long`.

Left click opens a Network dialog: every interface's state, addresses,
packets, bytes and current rates, and on Wi-Fi the network, BSSID and
signal; and a network settings tool to open. Right click lists the same
tools. Tools are found on `$PATH` (nm-connection-editor, GNOME and KDE
network settings, nmtui, cmst, connman-gtk, iwgtk, iwctl, wpa_gui, wicd)
and those whose daemon is running (NetworkManager, connmand, iwd,
wpa_supplicant, wicd) come first. A system configured by scripts simply has
none to offer.

IRIX: another `net_snapshot()` (`SIOCGIFCONF` works there too; counters
from `netstat` or the kernel's interface statistics), no settings tools.

### 5.7 Removable storage

While a USB stick or memory card is present, an icon appears left of the
network icon, like Windows' "Safely Remove Hardware". Either click opens a
menu with a submenu per volume: Mount and Open, Mount, Open, Unmount, and
Eject, which unmounts every volume of the disk and powers it off
(`udisksctl power-off`) or ejects it.

Volumes are found without running anything
([src/core/storage.h](../src/core/storage.h)): disks in `/sys/block` that are
removable or sit on a USB path, their partitions, filesystem type and label
from the udev database (`/run/udev/data/b<major>:<minor>`), and mount points
from `/proc/self/mounts`; polled every two seconds.

ttr never runs as root, so each volume uses the first unprivileged way the
system offers: an `/etc/fstab` entry with `user` or `users` (`mount` is
setuid), udisks2 (`udisksctl`; polkit lets the active session mount, which
on OpenRC needs elogind or ConsoleKit), pmount, or udevil. With none, the
menu says what to install instead of asking for a root password. Commands
run asynchronously; a failure shows the tool's own message. Open uses
`xdg-open`, else a known file manager.

IRIX: another `storage_snapshot()` (removable media there are mostly
CD-ROMs and floppies, mounted by mediad).

### 5.8 Notifications

Notifications pop up at the bottom right, just above the taskbar, sliding
up into place and stacking upward (five at most): a bevelled panel with the
app's icon, the summary in bold and the body wrapped to four lines. A click
dismisses one; otherwise it goes after `notificationTime` seconds or the
app's own timeout; critical ones stay until clicked. The shell asks the WM
to keep them above other windows (`WmLink::set_overlay`).

Sources ([src/shell/notifysrc.h](../src/shell/notifysrc.h)), all feeding
one sink:

- **D-Bus** (Linux, libdbus-1, `TTR_HAVE_DBUS`): the freedesktop.org
  `org.freedesktop.Notifications` service on the session bus: Notify
  (with replaces_id, urgency, timeout, app_icon / image-path),
  CloseNotification, GetCapabilities ("body"), GetServerInformation, and
  the NotificationClosed signal. The connection's descriptor is watched
  from the Xt loop; nothing blocks. If another daemon owns the name, ttr
  leaves it alone.
- **X11**: `ttr -notify SUMMARY [BODY]` appends a record to `_TTR_NOTIFY`
  on the root window, so any X client on any machine can notify, IRIX
  included. Motif claims the root window for its own screen object, so the
  shell watches these through an Xt event dispatcher of its own, chained in
  front of the WM's.
- **Log file**: new lines in `notificationLog` (from start, three at most
  per poll). On IRIX, console and kernel messages go to `/var/adm/SYSLOG`.

### 5.9 Docking and snapping

A window can be docked in one of three columns, left, center or right, of
the full work area height and `dockWidth` (a third or half) of its width;
with halves the center column is the middle half. It can also be snapped
to the top or bottom: full width, the same fraction of the height. Like
maximize, the normal geometry is kept and comes back on Restore.

Dragging a window to a screen edge shows an outline of the target (four
thin windows, so nothing is drawn over other windows) and releasing there
snaps it; Escape cancels. Dragging a docked window out returns it to its
normal size with the same spot of the title bar under the pointer.

| Keys | Action |
|---|---|
| Super+1 / 2 / 3 | dock left / center / right; the same key again restores |
| Super+Left / Right | dock left / right |
| Super+Up / Down | snap top / bottom |
| Alt+F5 (CDE Restore) | undock |

CDE and mwm put window operations on Alt+function keys and never used the
Windows key, so the Super combinations do not collide with them. The window
menu has the same operations under Snap. Docked columns set
`_NET_WM_STATE_MAXIMIZED_VERT`, top and bottom `..._HORZ`, the nearest EWMH
states.

### 5.10 Startup programs

There are three ways to start programs with ttr, and all of them run, in
this order:

1. **XDG autostart** (Linux desktops), the freedesktop.org autostart spec:
   `.desktop` files in `~/.config/autostart`, which override those of the
   same name in `$XDG_CONFIG_DIRS/autostart` (`/etc/xdg/autostart`). Off by
   default (`xdgAutostart`), because turning it on starts whatever the
   distribution put there (PipeWire, PulseAudio, Bluetooth applets) and
   would change an existing setup without asking. An entry is skipped if:
   - it is turned off (`Hidden=true`, `X-GNOME-Autostart-enabled=false`);
   - `OnlyShowIn`/`NotShowIn` leave us out (we match
     `$XDG_CURRENT_DESKTOP` and `TTR`; ttr does not set the variable);
   - its `TryExec` program is missing;
   - its `AutostartCondition` does not hold. Only `if-exists` and
     `unless-exists` can be checked; the GNOME and GSettings conditions
     count as false;
   - it is `X-GNOME-HiddenUnderSystemd`/`X-KDE-HiddenUnderSystemd` and
     systemd is running, because systemd user units start those.

   Settings > XDG Autostart lists every entry with its status. Turning one
   off or on writes the user's copy with or without `Hidden=true`, as other
   desktops do, so the choice holds for any desktop the user runs.
2. **The startup list**, `~/.config/ttr/startup`, edited on Settings >
   Startup (or by hand). It is an ordered list of shell commands, added by
   hand, browsed for, or picked from the start menu's programs. An entry can
   be turned off without deleting it. It is easy to set up and shows at a
   glance what starts.
3. **The startup script**, `~/.config/ttr/autostart.sh`, as in Openbox. It
   runs with `sh` (it need not be executable). It suits anything the list
   cannot do: conditions, environment, `xset`, waiting for one program before
   another. Settings > Startup can turn it off, and "Edit Script..." creates
   a commented example and opens it in `$VISUAL` or `$EDITOR` in the
   configured terminal.

The list and the script work on IRIX too. XDG autostart has nothing to read
there.

Everything is started detached, like the start menu does, and ttr does not
wait for it. Startup programs run when ttr starts, **not** when it restarts
in place: before `execvp`, ttr sets `TTR_RESTARTED=1`. The new image reads
it and clears it at once, so programs it starts do not inherit it. See
[src/core/startup.h](../src/core/startup.h) and
[src/core/xdgautostart.h](../src/core/xdgautostart.h).

### 5.11 Color themes

A theme is a color per role ([src/core/theme.h](../src/core/theme.h)):
background, text, button, text field, list, scroll bar trough and slider,
toggle fill, and the inactive and active frames with their title text.
Theme files are X resource files in the format of IRIX's desktop color
schemes, whose role names they use (`BasicBackground`, `WMActiveBackground`,
...). The 30 IRIX schemes (only their color values, see
[data/schemes/irix/README.md](../data/schemes/irix/README.md)) are themes
as they are. ttr's own themes leave out the gamma prefix and may carry a
`name:`. On Linux the `gamma1_0` values are used, the colors as a stock SGI
screen showed them. On IRIX the values for the system gamma are used
(`/etc/config/system.glGammaVal`, 1.7 by default), because its display
hardware applies that gamma.

Themes are looked up in `~/.config/ttr/themes`, next to an uninstalled
build (`data/schemes`), and in `PREFIX/share/ttr/schemes`. Classic is built
in.

How a theme is applied, once at startup:

- **Motif widgets** get it through the resource database: `*background`
  always, and `*foreground`, `*XmPushButton.background`,
  `*XmTextField.background`, `*XmList.background`, the scroll bar colors and
  the toggle `selectColor` only when the theme sets them. Motif derives the
  shadows, arm and select colors itself, as it does for any background.
- **Our own drawing** (frames, the taskbar and tray, notifications) uses
  `motif_colors()` on the theme's colors, so it matches the widgets.
- **Missing roles** are derived. The specific backgrounds follow the
  background, and text colors follow Motif's brightness rule. A new
  background, such as the user's own, also resets the specific
  backgrounds, so they follow it instead of the theme's.

Precedence, lowest first: the theme, then (with Classic only) mwm's
`Mwm*activeBackground`, `activeForeground`, `background` and `foreground`
for the frames, then the user's colors. An mwm resource only counts if it
was written for mwm. Lines for every program (`*background`) are told
apart by looking the same resource up for a program that does not exist.

The Settings dialog has a Colors page with the theme list, a preview drawn
with `paint_frame()` through an `OffsetPainter`, and the six user colors.
It also has a color picker of our own (RGB sliders, a hex field, basic
colors), since `XmColorSelector` is Motif 2.2 and IRIX has 2.1. The
picker frees the colors it allocated, for 8-bit PseudoColor displays.

### 5.12 Wayland mode (`ttr-wayland`, later)

1. Bring up DRM/KMS, libinput and the scene graph, then create the Wayland
   socket.
2. Start XWayland **eagerly** (not lazily), with `-listen tcp` if remote X
   clients are enabled. Start the xwm.
3. When XWayland is ready, create a `socketpair` and spawn
   `ttr --shell-only --link-fd N` with `DISPLAY` and `WAYLAND_DISPLAY` set.
4. The shell sends its hello and registers its windows. The compositor then
   publishes the toplevel list.
5. Supervision: if the shell dies it is respawned with backoff. If XWayland
   dies, XWayland is restarted, and then the shell.

---

## 6. Wayland protocol surface (Wayland mode)

Our own UI does not use Wayland protocols (frames are scene nodes, and the
shell uses X11 through XWayland). Protocols exist to serve **third-party
clients**.

### 6.1 Table stakes
- `xdg-shell`
- `xdg-decoration` (advertise server-side as preferred)
- `wp-primary-selection`: middle-click paste. XWayland bridges it to the real
  PRIMARY selection.
- `ext-data-control`: clipboard managers.
- `xdg-activation`: focus and raise with a token.

### 6.2 Restoring X11 parity
- `ext-image-copy-capture` (+ `ext-foreign-toplevel-list`): screenshots and
  recording without portals.
- `wp-relative-pointer` + `wp-pointer-constraints`: pointer warp and confine.
- `wp-keyboard-shortcuts-inhibit`: VNC viewers and VMs receive hotkeys.
- `zwp-xwayland-keyboard-grab`: X11 applications that call `XGrabKeyboard`.
- `ext-session-lock`, `ext-idle-notify` + `zwp-idle-inhibit`.

### 6.3 Quality of life
- `wp-viewporter`, `wp-fractional-scale`, `wp-presentation-time`,
  `wp-tearing-control`.
- `zwp-text-input` + `zwp-input-method`: stub the seat plumbing early, because
  it is painful to retrofit.

### 6.4 Explicitly skipped
- `wlr-layer-shell`: our panel goes through XWayland and `WmLink`.
- foreign-toplevel-*management*: we implement only the list, for capture.
- xdg-desktop-portal as a requirement. Later, a thin optional backend may exist
  so browsers can share the screen, and it **auto-approves**.

---

## 7. Security and permission model

- **Policy: any client on our socket gets everything**: screencopy, data
  control, output configuration. There are no dialogs. This is X11's standing
  policy and a coherent single-user trust model.
- **Two exceptions, enforced by logic rather than dialogs:**
  1. **A lock is a lock.** While `ext-session-lock` is active, screencopy and
     input observation return nothing.
  2. **Unconditional escape chord.** A compositor-level secure attention key
     that no `keyboard-shortcuts-inhibit` can capture, so no client can trap
     input.
- The shell's privileged role is granted through the `socketpair` the
  compositor created (§4.4), not through anything a client can claim.
- X11 mode equivalents: screencopy ↔ `XGetImage`/MIT-SHM; idle ↔ the
  XScreenSaver extension; activation ↔ `_NET_ACTIVE_WINDOW`; constraints ↔
  grab + warp.

---

## 8. Dependencies

| Component | Portable (X11 mode, IRIX) | Linux-only extras |
|---|---|---|
| X protocol | Xlib (not xcb: Motif is Xlib-based, and IRIX has no xcb) | — |
| Toolkit | Xt + Motif. Baseline is the **Motif 2.1 API**, which IRIX 6.5 ships (IRIS IM) | Open Motif 2.3 (`x11-libs/motif` on Gentoo). Xft fonts through render tables are optional. |
| GL widget | `GLwMDrawingArea` from SGI's libGLw | `GLwMDrawingArea` from Mesa's libGLw, built against Motif |
| Extensions | SHAPE; Xinerama/RandR optional | RandR 1.2+, XFixes |
| Icon files | libXpm (`TTR_HAVE_XPM`); SVG via vendored nanosvg (`TTR_HAVE_SVG`) | libpng 1.6+ (`TTR_HAVE_PNG`); themes in `$XDG_DATA_DIRS/icons` |

Vendored code lives in `third_party/` as git submodules pinned to a commit
(`git submodule update --init`). It is built with our compiler flags but
without our warning flags, and only through a single wrapper file each:
nanosvg (zlib license) through [src/x11/svg.cpp](../src/x11/svg.cpp),
which also maps the C99 float math nanosvg uses onto C89 math on IRIX.
nanosvg was chosen over lunasvg (needs C++17) and svgpp (needs Boost, and
only parses).
| Wayland mode | — | wlroots, libinput, xkbcommon, FreeType/fontconfig (or cairo/pango), XWayland |

### 8.1 Motif feature rules

- **Motif 2.1 is the floor.** We may use anything in the 2.1 API without guards,
  for example `XmComboBox`, `XmSpinBox`, `XmNotebook`, `XmContainer`, render
  tables and the 2.x drag-and-drop. Features added after 2.1 (in 2.2/2.3) go
  behind `#ifdef` and need a 2.1 fallback.
- **SGI extension widgets are IRIX-only.** These are IRIS IM's `Sg*` widgets
  (`SgThumbWheel`, `SgDropPocket`, `SgFinder` and others) and the Schemes
  look-and-feel. Code that uses them must be behind `#ifdef TTR_HAVE_SGI_WIDGETS`
  and have a plain Motif fallback, or be reimplemented as one of our own small
  widgets in `shell/`, so that Linux builds keep the same features.
- **`GLwMDrawingArea` is portable.** It exists on IRIX and in Mesa's libGLw on
  Linux, so a GL area can be used on both. Nothing in the WM needs GL. It is
  available for optional extras such as a fancy clock or applet. Under Wayland
  mode it runs through XWayland's GLX. The X11 mode binary must never *require*
  it: an 8-bit Indy without suitable GL visuals still has to work.

### 8.2 Source layout

```
src/core/      portable C++98: port.h, containers, geometry, config parser,
               window model, focus/stacking policy, theme + Motif color algorithm,
               Painter and ToplevelHandle interfaces, WmLink messages
src/x11/       portable C++98 + Xlib: X11 WM backend, ICCCM/EWMH/Motif hints,
               XlibPainter
src/shell/     portable C++98 + Xt/Motif: taskbar, start menu, window menu,
               XEmbed tray, run dialog, config UI
src/wayland/   Linux only: compositor (C11 wlroots glue + C++98), xwm integration,
               PixelPainter, WmLink socket server
config/        per-platform make fragments
dist/          installed files: ttr-session, the xsessions entry; per-distribution
               packaging (dist/arch/PKGBUILD)
tools/         developer scripts (nested.sh)
third_party/   pinned git submodules (nanosvg)
```

---

## 9. Milestones

1. **"It's alive"** (X11 mode, Linux): reparent an xterm into a Motif-style
   frame with bevels and a title bar, and post a Motif window menu from the
   frame. **Build it with MIPSpro on emulated IRIX in the same milestone**, so
   dialect drift is caught from the first commit.
2. **Full X11 WM:** ICCCM/EWMH, move/resize, focus policy, `_MOTIF_WM_HINTS`,
   struts, `WM_S<n>` and `--replace`.
3. **Motif shell in-process:** taskbar with task buttons, start menu, clock,
   XEmbed tray, run dialog. **Runs on the Indy.**
4. **Wayland spike:** a minimal wlroots compositor plus XWayland plus
   `ttr --shell-only`, to prove the menu-grab routing in §4.4 before building
   anything else on it.
5. **Wayland mode proper:** scene-graph frames with `PixelPainter`, xdg-shell
   and XWayland toplevels, native Wayland windows in the Motif taskbar.
6. Protocol surface (§6), permission model (§7), XWayland over TCP for remote
   IRIX clients.
7. **Target screenshot:** the same taskbar on Xorg, on IRIX, and on the Wayland
   compositor.

---

## 10. Open questions

- The exact EWMH subset for X11 mode (enough for modern toolkits; scope to be
  decided).
- Which `Sg*` widgets, if any, are worth a portable reimplementation (the thumb
  wheel and drop pocket are the obvious candidates).
- Font story: core fonts everywhere for consistency, or Xft on Linux and core
  fonts on IRIX?
- HiDPI in Wayland mode: Motif does not scale. Is unscaled XWayland with larger
  fonts acceptable?
- Configuration format and how the Motif config UI reloads it live.
- Multi-head policy per backend (RandR, Xinerama, IRIX multi-head, our output
  abstraction).
- Whether to support session management (XSMP). Optional, decide later.
