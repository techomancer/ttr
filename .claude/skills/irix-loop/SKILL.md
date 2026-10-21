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
| tl and tp tests on IRIX | `tools/irix-test.sh` builds `build-irix/tl_test` and `build-irix/tp_test` (with `TP_SYS=build-irix/tp_sys_irix.o`), copies them to `/tmp` and runs both |
| Disassembly | `spimdisasm elfObjDisasm BIN OUT --abi N32 --arch-level MIPS3 --compiler IDO --split-functions OUT/funcs --dyn-syms`, then m2c's `irixn32` branch (`python3 m2c.py -t mips-ido-c++ FUNC.s`) on single functions; export the branch with `git -C ~/gits/m2c archive github/irixn32` rather than switching the checkout. xwsh's output is in `ignore/xwsh/` |
| Monitor | `tools/mon.py "status" "cpu regs"` (iris monitor on 127.0.0.1:8889; `help` lists commands) |
| Screenshot | `xwd -root -display localhost:100 -silent \| magick xwd:- shot.png` (through X: correct colours in every visual). `tools/irix-shot.sh` (monitor `rex fbdump`) composes 8-bit colormap mode wrongly (everything dark blue); use its `ci.png` for structure only |
| Guest X server from the host | `ignore/iris.toml` forwards host port 6100 to the guest's 6000, so `DISPLAY=localhost:100` on the host is the Indy's Xsgi. After each boot run `tools/irix-sh.py run '/usr/bin/X11/xhost +'` once the desktop is up. Linux builds (ttrt) can then be tested on the real 8-bit Xsgi without touching the host desktop |
| Typing, clicking, resizing | `build/xtype -d localhost:100 @x,y 'text' +Return +shift+Prior %900x560` (`tools/xtype.c`, XTest: click at root x,y (`@x,y,3` button 3, `^@x,y,3` with Ctrl held), type text, press keys, resize the focused top-level window). Build: `cc -o build/xtype tools/xtype.c -lXtst -lXext -lX11`. 4Dwm is click-to-type: click into a window before typing |

`ignore/` is outside version control; the tools and this skill are in the
repository.

## Boot

```
tools/iris.sh start        # background, own session; prints the pid
tools/iris.sh wait         # login prompt answers after ~1 minute
tools/iris.sh status       # pid, listening ports, uname/uptime from the guest
```

The CPU starts by itself and xdm brings up X (Xsgi on :0) a little after
telnet. The login greeter (clogin) grabs the server, so X clients started
from telnet hang until someone is logged in. Root autologin is set up:
`/etc/autologin` holds `root` and the one-shot flag `/etc/autologin.on`
makes clogin log root in at the next boot; clogin deletes the flag, and
`tools/iris.sh stop` re-creates it before shutdown. The desktop (4Dwm,
toolchest) is up about a minute after telnet; check with
`ps -ef | grep 4Dwm`. If the greeter is showing instead, `touch
/etc/autologin.on` and kill clogin; xdm restarts it and it logs in.

`/shared` is mounted by fstab; if it is missing, `tools/irix-sh.py
mount-shared`.

## Build

```
tools/irix-build.sh                 # make all  -> build-irix/ttr
tools/irix-build.sh build-irix/core_str.o
tools/irix-build.sh clean
```

It runs, in the guest, `cd /shared && make B=build-irix CXX=CC ...` with
`-n32 -O2 -LANG:exceptions=OFF -no_prelink -ptused -woff 3970`,
`-I/usr/Motif-2.1/include -DTTR_HAVE_XPM -DTTR_HAVE_SVG`,
`-L/usr/Motif-2.1/lib32` and `-lXm -lXt -lXpm -lX11 -lm` (override with
`IRIX_CXXFLAGS`, `IRIX_CPPFLAGS`, `IRIX_LDFLAGS`, `IRIX_LIBS`).
`/usr/include/Xm` is Motif 1.2 on IRIX 6.5; Motif 2.1 (`motif21_dev`,
libXm.so.2) is under `/usr/Motif-2.1` and is what ttr needs. libpng is
1.2.5 there, too old for the simplified API in `src/x11/iconload.cpp`, so
PNG stays off until that loader has a classic-API path. `-woff 3970`
silences the one warning every `XtSetArg` of a pointer raises. The
`-no_prelink -ptused` pair is required on the NFS share: MIPSpro's template
prelinker cannot create its `ii_files/` lock files there. A single object
takes a few seconds; the whole of ttr takes several minutes.

Since the objects land in `build-irix/` on the host, the result is visible
from Linux right away (`file build-irix/ttr` says MIPS N32 ELF).

## Run

Binaries cannot be executed from `/shared`: copy to `/tmp` first.

```
tools/iris.sh sh 'cp /shared/build-irix/ttr /tmp/ttr && chmod a+x /tmp/ttr && /tmp/ttr; echo rc=$?'
```

The NFS copy loses the execute bit, hence the `chmod`.

ttr has no `-help`; with 4Dwm still running it must report that another
window manager owns the display and exit with status 1, which is a cheap
smoke test of Xt, Motif and the settings code on IRIX.

For X programs, the desktop on `:0.0` is reachable from any telnet session
(`DISPLAY` is preset). The stock session runs `4Dwm`. To run ttr as the
window manager (verified 2026-10-19: frames, title bars and the taskbar
appear around the toolchest and winterm):

```
tools/iris.sh sh 'p=`ps -ef | grep "[4]Dwm" | awk "{print \$2}"`; kill $p; sleep 3; (/tmp/ttr > /tmp/ttr.log 2>&1 &); sleep 15; ps -ef | grep "[t]tr"; cat /tmp/ttr.log'
tools/irix-shot.sh ignore/shots/ttr.png
```

(`awk "{print \$2}"` needs the escaped dollar inside the double quotes
of the irix-sh argument; `$2` unescaped is expanded by the guest shell.)
Start long-running X clients with `nohup cmd &`: a plain background job
of the telnet session gets SIGHUP when the session ends (ttrt did).

Xsgi runs an 8-bit PseudoColor default visual (`-pseudomap 4sight`).
The "dark blue screen" seen in early ttr screenshots was the fbdump
screenshot path, not ttr: take screenshots with `xwd` (above).

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
- Never run two guest builds at once (for example a background
  `tools/irix-test.sh` and another build): they share `build-irix/` and
  leave empty or mixed objects ("ld32: file is empty", or a test binary
  linked from objects of different source states). After an overlap,
  `rm -f build-irix/tl_*` (or the affected objects) and rebuild.
- The emulator's `prom.bin` warning at startup is harmless (embedded PROM).
- `ignore/iris.toml` is read at start: a change (ports, `jitv2` threads,
  RAM) needs `tools/iris.sh stop` and `start`.
- A full ttr build takes about 6 minutes of guest time; one object a few
  seconds. The Linux host shell helper's per-command timeout is 300 s by
  default (`--timeout`), `tools/irix-build.sh` uses 7200.
