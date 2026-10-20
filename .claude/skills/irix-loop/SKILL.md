---
name: irix-loop
description: Build and test ttr (and the tl terminal library) on IRIX 6.5.22 with MIPSpro 7.4 in the iris Indy emulator kept in ignore/ - boot the guest, build from the NFS-shared tree into build-irix/, run programs and tests in the guest over telnet, drive its X desktop, shut it down cleanly. Use for any IRIX or MIPSpro dialect check, for running ttr or ttrt under IRIX, and for big-endian testing.
---

# IRIX build and test loop

The IRIX machine is a virtual Indy (R4400, 256 MB, IRIX 6.5.22m, MIPSpro
7.4.4m, Motif, libpng, libXpm) run by the iris emulator from `ignore/`.
It is independent of the iris repository's own instance: own ports, own
disk image, own control socket, so both can run at once.

## Pieces

| What | Where |
|---|---|
| Emulator binary and config | `ignore/iris`, `ignore/iris.toml` (telnet 2423, monitor 8889, serial 8980/8981, control socket `/tmp/iris-ttr.sock`) |
| Disk image | `ignore/irix65_8g.raw`, written directly (no overlay): never kill iris while IRIX runs |
| Lifecycle | `tools/iris.sh start | wait | status | stop | sh CMD...` (log: `ignore/iris.log`) |
| Guest shell | `tools/irix-sh.py run 'cmd' ['cmd' ...]`, `push`, `pull`, `wait`, `mount-shared` (telnet, root, no password, `/bin/sh`, cwd `/tmp`, `DISPLAY=:0.0`) |
| File exchange | the ttr tree is the NFS export: guest `/shared` == this directory, read-write, mounted from the guest's `/etc/fstab` at boot. **noexec**: copy binaries to `/tmp` before running them |
| IRIX build | `tools/irix-build.sh [target...]` builds with MIPSpro into `build-irix/` (gitignored, separate from the Linux `build/`) |
| Monitor | `tools/mon.py "status" "cpu regs"` (iris monitor on 127.0.0.1:8889; `help` lists commands) |

`ignore/` is outside version control; the tools and this skill are in the
repository.

## Boot

```
tools/iris.sh start        # background, own session; prints the pid
tools/iris.sh wait         # login prompt answers after ~1 minute
tools/iris.sh status       # pid, listening ports, uname/uptime from the guest
```

The CPU starts by itself and xdm brings up the X desktop (Xsgi on :0) a
little after telnet. `/shared` is mounted by fstab; if it is missing,
`tools/irix-sh.py mount-shared`.

## Build

```
tools/irix-build.sh                 # make all  -> build-irix/ttr
tools/irix-build.sh build-irix/core_str.o
tools/irix-build.sh clean
```

It runs, in the guest, `cd /shared && make B=build-irix CXX=CC ...` with
`-n32 -O2 -LANG:exceptions=OFF -no_prelink -ptused`, `-DTTR_HAVE_XPM
-DTTR_HAVE_SVG -DTTR_HAVE_PNG` and `-lXm -lXt -lXpm -lpng -lX11 -lm`
(override with `IRIX_CXXFLAGS`, `IRIX_CPPFLAGS`, `IRIX_LIBS`). The
`-no_prelink -ptused` pair is required on the NFS share: MIPSpro's template
prelinker cannot create its `ii_files/` lock files there. A single object
takes a few seconds; the whole of ttr takes several minutes.

Since the objects land in `build-irix/` on the host, the result is visible
from Linux right away (`file build-irix/ttr` says MIPS N32 ELF).

## Run

Binaries cannot be executed from `/shared`: copy to `/tmp` first.

```
tools/iris.sh sh 'cp /shared/build-irix/ttr /tmp/ttr && /tmp/ttr; echo rc=$?'
```

ttr has no `-help`; with 4Dwm still running it must report that another
window manager owns the display and exit with status 1, which is a cheap
smoke test of Xt, Motif and the settings code on IRIX.

For X programs, the desktop on `:0.0` is reachable from any telnet session
(`DISPLAY` is preset). The stock session runs `4Dwm`; to try ttr as the
window manager, kill 4Dwm first (`ps -ef | grep 4Dwm`) and start
`/tmp/ttr &` from the telnet session, or run it with `--replace` once that
works on IRIX. Screenshots: iris monitor `shot FILE.png` if the build has
it, else `xwd -root -display :0.0 > /shared/ignore/root.xwd` in the guest
and convert on the host.

The X session may have a screen saver; `/usr/bin/X11/xset s off` once
after login if probes read black.

## Shutdown

```
tools/iris.sh stop         # sync; shutdown -p -g0 -y, then waits for the process to exit
```

The process exits on power-off (about 30 s). Only if it does not, and the
monitor confirms the guest is halted, kill it by pid.

## Gotchas

- **Long command lines hang.** The IRIX tty discards canonical input over
  256 bytes. `tools/irix-sh.py run` stages anything longer in a `/tmp`
  script automatically; when writing guest shell scripts by hand, keep
  lines short or `push` a file.
- The host shell's `cd` is aliased and broken in the Bash tool: use
  absolute paths or `git -C`. Inside `/bin/sh` scripts `cd` is fine.
- `/shared` is noexec; build there, run from `/tmp`.
- Stock IRIX has no bash (`/bin/sh` scripts only), no `pkill`, no `time`
  keyword in sh (`/usr/bin/time` or `timex`), `chmod a+x` not `+x`,
  `rm -f` errors without a file argument.
- MIPSpro is C++98 of 2003 vintage: no `<stdint.h>` (use `<inttypes.h>`),
  declarations before statements in C, the `for`-scope and template
  quirks listed in `docs/wm-design.md` §2.
- Objects written by the guest appear as owned by the host user on the
  host (the NFS server maps root), so `make clean` on either side works.
- `/tmp` in the guest is emptied at boot; `/var/tmp` persists.
- The emulator's `prom.bin` warning at startup is harmless (embedded PROM).
