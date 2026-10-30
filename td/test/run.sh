#!/bin/sh
# Runs ttrdb's test scripts (td/test/*.ttrdb) against the test programs.
# Usage: run.sh BUILD_DIR. A script's first line names its program
# ("# program: td_demo"). ttrdb and the programs are copied to a
# temporary directory first: IRIX cannot run them from the NFS share.
B=${1:-build}
T=`dirname "$0"`
W=/tmp/td-test.$$
rm -rf $W
mkdir -p $W || exit 1
cp $B/ttrdb $B/td_demo $B/td_crash $W/ || exit 1
chmod +x $W/ttrdb $W/td_demo $W/td_crash
fail=0
ran=0
for s in $T/*.ttrdb; do
    name=`basename $s .ttrdb`
    prog=`sed -n 's/^# program: *//p' $s | head -1`
    ran=`expr $ran + 1`
    if $W/ttrdb -x $s $W/$prog > $W/$name.out 2>&1; then
        echo "ok: $name"
    else
        fail=`expr $fail + 1`
        echo "FAIL: $name"
        cat $W/$name.out
    fi
done
rm -rf $W
echo "td tests: $ran scripts, $fail failed"
[ $fail -eq 0 ]
