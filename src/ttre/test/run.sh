#!/bin/sh
# Runs ttre's screen tests (*.drive, see src/ttrc/test/drive.cpp): each
# edits a fresh note.txt; NAME.expect, if there, is what the file must
# hold afterwards. Usage: run.sh BUILD_DIR
B=${1:-build}
T=`dirname "$0"`
W=/tmp/ttre-test.$$
mkdir -p $W || exit 1
fail=0
ran=0
for d in $T/*.drive; do
    name=`basename $d .drive`
    rm -rf $W/.config $W/made.txt
    printf 'line one\nline two\n' > $W/note.txt
    HOME=$W XDG_CONFIG_HOME=$W/.config TERM=xterm LANG=C \
        $B/ttrc_drive -s 80x20 $B/ttre $W/note.txt < $d > $W/out 2>&1
    rc=$?
    if [ $rc -eq 0 ] && [ -f $T/$name.expect ] && ! cmp -s $T/$name.expect $W/note.txt; then
        echo "note.txt differs from $name.expect:" >> $W/out
        cat $W/note.txt >> $W/out
        rc=1
    fi
    ran=`expr $ran + 1`
    if [ $rc -ne 0 ]; then
        fail=`expr $fail + 1`
        echo "FAIL: $name"
        cat $W/out
    else
        echo "ok: $name"
    fi
done
rm -rf $W
echo "ttre tests: $ran runs, $fail failed"
[ $fail -eq 0 ]
