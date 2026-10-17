#!/bin/sh
# Run ttr inside a nested X server for testing.
#
#   tools/nested.sh                 Xephyr on :5, 1024x768
#   tools/nested.sh xnest           Xnest instead (no extensions, like old X)
#   DPY=:7 SIZE=800x600 tools/nested.sh
#
# Then start clients with DISPLAY=:5 (for example DISPLAY=:5 xmessage hi).
# Ctrl-C stops ttr and the nested server.

SERVER=${1:-xephyr}
DPY=${DPY:-:5}
SIZE=${SIZE:-1024x768}
TTR=${TTR:-build/ttr}

case $SERVER in
xephyr) Xephyr "$DPY" -screen "$SIZE" -ac -br -noreset & ;;
xnest)  Xnest "$DPY" -geometry "$SIZE" -ac -br -noreset & ;;
*)      echo "usage: $0 [xephyr|xnest]" >&2; exit 2 ;;
esac
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null' EXIT INT TERM

i=0
while ! xdpyinfo -display "$DPY" >/dev/null 2>&1; do
    i=$((i + 1))
    if [ $i -gt 50 ]; then
        echo "$0: nested server did not start" >&2
        exit 1
    fi
    sleep 0.1
done

DISPLAY=$DPY "$TTR"
