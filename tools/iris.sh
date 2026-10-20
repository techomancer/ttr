#!/bin/sh
# Start, stop and query the IRIX test machine: the iris emulator (ignore/iris,
# config ignore/iris.toml, Indy R4400 with IRIX 6.5.22 and MIPSpro 7.4 on
# ignore/irix65_8g.raw). The ttr tree is exported over NFS and mounted at
# /shared in the guest (guest /etc/fstab). ignore/ is not under version control.
#
#   tools/iris.sh start      boot it (background, log in ignore/iris.log)
#   tools/iris.sh wait       block until IRIX answers on telnet
#   tools/iris.sh status     process, ports, guest telnet
#   tools/iris.sh stop       clean IRIX shutdown, then wait for the process to exit
#   tools/iris.sh sh CMD...  run shell commands in the guest (tools/irix-sh.py run)
#
# Host ports (ignore/iris.toml): telnet 2423, monitor 8889, serial 8980/8981,
# control socket /tmp/iris-ttr.sock. The disk image is written directly, so
# never kill the process while IRIX is running: use stop.

ROOT=`dirname "$0"`/..
IRIS_DIR=`readlink -f "$ROOT/ignore"`
LOG=$IRIS_DIR/iris.log
SH="python3 $ROOT/tools/irix-sh.py"

# The instance is the iris process whose working directory is ignore/.
find_pid() {
    PID=
    for p in `pgrep -x iris`; do
        if [ "`readlink -f /proc/$p/cwd`" = "$IRIS_DIR" ]; then
            PID=$p
            return 0
        fi
    done
    return 1
}

case "$1" in
start)
    if find_pid; then
        echo "iris is already running (pid $PID)"
        exit 0
    fi
    [ -x "$IRIS_DIR/iris" ] || { echo "$IRIS_DIR/iris: not found" >&2; exit 1; }
    # setsid -f: own session, so it survives the shell that started it (the
    # Bash tool kills its process group when the command returns).
    (cd "$IRIS_DIR" && setsid -f ./iris < /dev/null > iris.log 2>&1)
    sleep 2
    if find_pid; then
        echo "iris started (pid $PID), log $LOG; now: tools/iris.sh wait"
    else
        echo "iris did not start:" >&2
        tail -20 "$LOG" >&2
        exit 1
    fi
    ;;
wait)
    $SH wait --timeout "${2:-900}"
    ;;
status)
    if find_pid; then
        echo "iris running, pid $PID"
    else
        echo "iris not running"
    fi
    ss -ltn 2>/dev/null | grep -E ':(2423|8889|8980|8981) ' | sed 's/^/  /'
    $SH --connect-timeout 3 --login-timeout 20 run 'uname -R; uptime' 2>/dev/null \
        || echo "  guest telnet not answering"
    ;;
stop)
    if ! find_pid; then
        echo "iris not running"
        exit 0
    fi
    echo "shutting IRIX down..."
    # /etc/autologin.on is one-shot (clogin deletes it when it logs root in
    # on the desktop); re-create it so the next boot lands on a desktop too.
    $SH --timeout 60 run 'touch /etc/autologin.on; sync; sync; shutdown -p -g0 -y' >/dev/null 2>&1
    i=0
    while kill -0 "$PID" 2>/dev/null; do
        i=`expr $i + 1`
        if [ $i -gt 90 ]; then
            echo "iris (pid $PID) still running after power-off; check $LOG" >&2
            exit 1
        fi
        sleep 2
    done
    echo "iris exited"
    ;;
sh)
    shift
    exec $SH run "$@"
    ;;
*)
    echo "usage: $0 start|wait|status|stop|sh CMD..." >&2
    exit 2
    ;;
esac
