#!/bin/sh
# Runs ttrc's screen tests (*.drive, see drive.cpp) under every shell this
# system has. Usage: run.sh BUILD_DIR
B=${1:-build}
T=`dirname "$0"`
W=/tmp/ttrc-test.$$
mkdir -p $W || exit 1
printf 'line one\nline two\n' > $W/note.txt
# zsh's first-run wizard would wait for an answer.
: > $W/.zshrc
fail=0
ran=0
for sh in /bin/sh /bin/bash /bin/zsh /bin/ksh /usr/bin/ksh /bin/csh /usr/bin/tcsh /bin/tcsh \
          /bin/jsh /usr/gnu/bin/bash; do
    [ -x $sh ] || continue
    for d in $T/*.drive; do
        name=`basename $d .drive`
        SHELL=$sh HOME=$W TERM=xterm LANG=C $B/ttrc_drive -s 80x20 $B/ttrc $W /tmp \
            < $d > $W/out 2>&1
        rc=$?
        ran=`expr $ran + 1`
        if [ $rc -ne 0 ]; then
            fail=`expr $fail + 1`
            echo "FAIL: $name with $sh"
            cat $W/out
        else
            echo "ok: $name with $sh"
        fi
    done
done
rm -rf $W
echo "ttrc tests: $ran runs, $fail failed"
[ $fail -eq 0 ]
