To The Roots (ttr)

We are tired of modern UI environments. They are user hostile and lack consistency.
They put glitz over readability and usability.

This is a window manager / mini desktop environment for Linux and IRIX.
X11 first; Wayland later (Linux only).

Design principles:

- Simple C++ that compiles with ancient compilers: MIPSpro 7.4 and
  `g++ -std=c++98 -pedantic`. No C++11, no exceptions, no RTTI, and no STL or
  iostreams in portable code. See docs/wm-design.md §2 for the full rules.
- An mwm-like window manager with a Windows 2000-like taskbar.
- Motif/Xt for every real widget (menus, taskbar, dialogs, config UI). Window
  frames are drawn by our own code with a Motif look.
- Motif 2.1 API is the floor (IRIX 6.5 IRIS IM; Open Motif 2.3 on Linux).
  SGI `Sg*` widgets are IRIX-only and must sit behind `#ifdef` with a fallback.
  `GLwMDrawingArea` is available on both platforms but is never required.
- X11 mode is one monolithic, single-threaded process, the same as mwm.
- Wayland mode (later) is a Linux-only compositor that runs the same Motif shell
  as an X11 client of XWayland, in a separate supervised process.
- Only src/wayland/ may use Linux-only APIs, and only its wlroots glue is
  written in C11. Everything else must build on IRIX.

The design decisions are recorded in docs/wm-design.md.
