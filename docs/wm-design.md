# Design Document: Integrated Wayland Compositor / X11 Window Manager
### Motif decorations · integrated taskbar, launcher, notification area · IRIX-portable core

*Status: draft — captures initial architecture discussion. Living document.*

---

## 1. Vision

A single integrated desktop shell in the CDE/Motif tradition, running as:

1. **A Wayland compositor** on modern Linux (with XWayland for X11 clients), and
2. **A native X11 reparenting window manager** — same binary family, second backend — running on Xorg today and, eventually, on IRIX.

Design goals, in priority order:

- **X11-first semantics.** The abstractions are designed around X11's asynchronous, server-authoritative model; Wayland fits into that shape, not the reverse.
- **Seamless remote X11.** Clients from remote machines (IRIX workstations over TCP) must feel native. In X11 mode this is free (core protocol). In Wayland mode, XWayland listens on TCP.
- **In-process chrome.** Decorations, taskbar, launcher/start menu, and notification area are compositor-internal — scene nodes / owned X windows, not separate shell clients. No layer-shell, no foreign-toplevel-management plumbing for our own UI.
- **User sovereignty.** No permission dialogs. The machine belongs to its owner; clients the owner runs are trusted. X11's trust model, deliberately chosen.
- **IRIX as a real target.** The portable core (widgets + X11 backend) must remain buildable for IRIX once a Rust toolchain exists there. This constrains dependencies, not ambition.

---

## 2. Architecture

Three layers. The key insight: do **not** abstract "a window" at the protocol level. On X11 a WM is a privileged client reparenting other clients' windows; on Wayland the WM *is* the display server and client surfaces are textures. There is no common "bare window." Instead we abstract (a) client toplevels as an interface, and (b) "a rectangle we own and can paint."

### 2.1 Widget / chrome layer (portable core)

- Titlebars, Motif bevels, buttons, taskbar, start menu/launcher, notification area, window menus.
- Written against **cairo** for rendering (image-buffer target on Wayland, xlib/xcb target on X11) and **pango** for text.
- Consumes an abstract input event stream and a damage/redraw callback.
- **This code never knows which backend it is running under.**
- Taskbar reconfiguration, layout, etc. all live here — in-process, simple, dies and restarts with the compositor.

### 2.2 `FrameHost` — "a rectangle I own"

The honest low-level abstraction: a paintable, input-receiving surface controlled by us.

| Concern | Wayland impl | X11 impl |
|---|---|---|
| Backing | scene-graph node with shm-buffer texture | real X window (frame window, dock window) |
| Painting | cairo → shm buffer → texture upload | cairo-xcb; server draws, Expose-driven |
| Input | scene hit-testing; we own the seat | core events on our windows |
| Taskbar reservation | our own layout policy | `_NET_WM_STRUT_PARTIAL` |
| Compositing | yes (we are the compositor) | none — server-side drawing, which is exactly why remote clients feel native |

On the Wayland side this corresponds to `wlr_scene`-style nodes (or the Smithay equivalent). Our own UI never goes through a window protocol — no round-trips, no self-client weirdness. Input dispatch is scene hit-testing: frame node → we handle it; surface content → forward to client.

### 2.3 `ToplevelHandle` — client windows

Interface representing any client toplevel: geometry, title, app id, states (maximized / fullscreen / activated / minimized), size hints, and **requests** (close, move, resize, minimize, activate).

Three implementations:

1. **`XdgToplevel`** — xdg-shell clients.
2. **`XWaylandToplevel`** — X11 clients under XWayland; appear as surfaces in the same scene, so decoration code is byte-identical.
3. **`X11Toplevel`** — native reparenting WM path: SubstructureRedirect on root, MapRequest / ConfigureRequest handling, `WM_PROTOCOLS` / `WM_DELETE_WINDOW`, ICCCM hints, EWMH essentials, and `_MOTIF_WM_HINTS` — which here is the *primary* decoration hint source, not a legacy shim. `xdg-decoration` and `_MOTIF_WM_HINTS` both collapse into one "wants server-side decorations?" property.

**X11-first interface rules** (so the X11 impl is never a lie):

- State changes are **asynchronous requests with confirmation**, never settable properties. Geometry may be overridden by the client; stacking is server-authoritative in X11 mode.
- No frame-perfect atomicity assumptions leak into the core.
- Grab semantics are abstracted as *"exclusive pointer/keyboard until released"*: realized as `XGrabPointer`/`XGrabKeyboard` on X11, and as plain input routing on Wayland (we own the seat). Menus and interactive move/resize are built on this.

---

## 3. Wayland protocol surface

Because chrome is in-process, most "advanced features" are just our own code. Protocols exist to serve **third-party clients**.

### 3.1 Table stakes
- `xdg-shell`
- `xdg-decoration` (advertise server-side preferred)
- `wp-primary-selection` — middle-click paste; XWayland bridges to real PRIMARY
- `ext-data-control` — clipboard managers
- `xdg-activation` — focus/raise with token (`wmctrl`-style workflows)

### 3.2 X11-parity restoration package
- `ext-image-copy-capture` (+ `ext-foreign-toplevel-list` for per-window capture) — screenshots/recording without portals; grim/slurp/OBS-native-capture work directly
- `wp-relative-pointer` + `wp-pointer-constraints` — pointer warp/confine for games
- `wp-keyboard-shortcuts-inhibit` — VNC viewers / VMs receive hotkeys
- `zwp-xwayland-keyboard-grab` — X11 apps doing `XGrabKeyboard` (VirtualBox, x11vnc, old games)
- `ext-session-lock` — locking done right
- `ext-idle-notify` + `zwp-idle-inhibit` — blanking timers and inhibition

### 3.3 Quality of life
- `wp-viewporter`, `wp-fractional-scale`, `wp-presentation-time`
- `wp-tearing-control` — restores the *option* of tearing for latency
- Gamma / color temperature: in-process preferred; `wlr-gamma-control` optional
- Output config: in-process xrandr-equivalent UI preferred; `wlr-output-management` optional
- `zwp-text-input` + `zwp-input-method` — stub the seat plumbing early; miserable to retrofit

### 3.4 Explicitly skipped
- `wlr-layer-shell` (chrome is in-process)
- foreign-toplevel-*management* (list only, for capture)
- xdg-desktop-portal as a requirement — later, a thin optional backend exists solely so browsers can screen-share, and it **auto-approves**.

---

## 4. Security & permission model

- **Policy: any client on our socket gets everything** — screencopy, data-control, output config. No dialogs, no dbus detours. This is X11's standing policy and a coherent single-user trust model; the portal model targets a Flatpak-shaped threat model this environment doesn't have.
- **Two carve-outs (logic, not dialogs):**
  1. **A lock is a lock.** While `ext-session-lock` is active, screencopy and input observation return nothing.
  2. **Unconditional escape chord.** A compositor-level SAK that no `keyboard-shortcuts-inhibit` can capture, so no client can ever trap input.
- Utilities (screenshot, color picker, window list) are either in-process hotkey functions or dumb clients speaking our protocols directly.
- X11-mode semantic twins: screencopy ↔ XGetImage/XShm; idle ↔ XScreenSaver ext; activation ↔ `_NET_ACTIVE_WINDOW`; constraints ↔ grab+warp. These live as capabilities on seat/output abstractions, not Wayland-side warts.

---

## 5. Implementation language: Rust

**Decision: Rust**, with the IRIX port promoted from "hedge" to **subproject** (see §6).

Rationale:
- Smithay provides a Rust-native compositor foundation.
- Memory safety matters for a long-running process parsing hostile client input all day.
- MIPSPro 7.4 C++ is rejected: partial C++98, temperamental templates; it would infect the entire codebase to serve one target that mogrix's modern GCC makes unnecessary anyway.
- Enthusiasm is the project's critical resource, and Rust is where it is.

**Portability discipline for the core crates:**
- Stable channel only; no exotic nightly features in portable code.
- Minimal proc-macro and dependency trees; no heavy async runtime (no tokio) in the core.
- FFI surface limited to **libc + xcb + cairo (+ pango)** — all of which existed in nekoware-era IRIX.
- **Physical workspace split, enforced in CI:**
  - `core-widgets` — no Linux-isms, no Wayland
  - `backend-x11` — no Linux-isms beyond xcb/cairo
  - `backend-wayland` — DRM/KMS/libinput/GBM/Smithay; Linux-only, and *stays that way* (this half has no IRIX analog and never needs porting)
- **Big-endian correctness from day one**: BE cross-check / qemu-system-mips job in CI. Pixel formats, X11 wire structs, and byte transmutes are where LE assumptions hide. Find them on the Ryzen, not on the Octane.

---

## 6. IRIX port plan

Only `core-widgets` + `backend-x11` go to IRIX. The Wayland backend is Linux plumbing forever.

### 6.1 Toolchain
- No `mips-sgi-irix` rustc target exists. LLVM's MIPS ISA support is fine; the gap is the triple: n32/n64 ABI details, IRIX ELF quirks, what `rld` expects from dynamic objects, TLS story (or absence).
- **Route: native LLVM target** (preferred over `rustc_codegen_gcc` — one compiler's output to debug, not two compilers' disagreements). mogrix's toolchain work is the reference and resource base.
- Characterization: grind, not research — "make the linker and runtime agree with a 2006 OS."

### 6.2 std port
- `sys`/PAL is a finite catalog: threads (IRIX pthreads, decent), files, sockets, time, env, process spawn. Each item: read the man page, write the shim, discover the one cursed detail.
- Known cursed details to expect: `stat64`/LFS story, pthread stack-size and TLS quirks, `dlopen` tolerance, signals on a pre-POSIX-2008 kernel.
- **Unwinding is the boss fight**: libunwind doesn't know IRIX. Bring-up mode is `panic=abort`; full unwinding is a later trophy.
- Milestone ladder: `no_std + alloc` first (widget core could theoretically already run), then full `std`.

### 6.3 CI
- **Our Indy emulator runs CI.** Cross-compile on fast hardware; execute the std test suite nightly on emulated IRIX; bisect against reproducible machine state. Better infrastructure than some tier-2 Rust targets have — most exotic ports die from untestability, not difficulty.

---

## 7. Milestones

1. **"It's alive":** X11 backend on Gentoo/Xorg — reparent an xterm into a Motif frame. Bevels, titlebar, the works. Exercises the exact codepath IRIX will someday run.
2. Full X11 WM: ICCCM/EWMH, move/resize, focus policy, `_MOTIF_WM_HINTS`, struts, taskbar + launcher + notification area in-process.
3. Wayland backend via Smithay: scene graph, xdg-shell, XWayland; identical chrome.
4. Protocol surface of §3; permission model of §4; XWayland TCP for remote IRIX clients.
5. Toolchain campaign (§6) with the WM as its concrete payload.
6. **Target screenshot: Motif bevels on the Indy, drawn by rustc's output.**

---

## 8. Open questions

- Exact EWMH subset for the native X11 WM (enough for modern toolkits; scope TBD).
- Notification area: XEmbed systray (X11 legacy) vs StatusNotifier/dbus vs both.
- Config format and in-process reconfiguration UI for taskbar/launcher.
- Multi-head policy details per backend (RandR vs our output abstraction).
- Session management (XSMP on X11 side?) — optional, decide later.
