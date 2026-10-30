#!/bin/sh
# Runs ttrdb's test scripts (td/test/*.ttrdb) against the test programs.
# Usage: run.sh BUILD_DIR. A script's first line names its program
# ("# program: td_demo"). ttrdb and the programs are copied to a
# temporary directory first: IRIX cannot run them from the NFS share.
B=${1:-build}
T=`cd \`dirname "$0"\` && pwd`
W=/tmp/td-test.$$
rm -rf $W
mkdir -p $W || exit 1
cp $B/ttrdb $B/td_demo $B/td_crash $B/td_shmain $B/td_dlmain $B/td_values $B/libtdshlib.so \
    $W/ || exit 1
chmod +x $W/ttrdb $W/td_demo $W/td_crash $W/td_shmain $W/td_dlmain $W/td_values
# The test shared library is found next to the programs (IRIX reads
# LD_LIBRARYN32_PATH for n32 programs, LD_LIBRARY64_PATH for 64-bit ones).
LD_LIBRARY_PATH=$W
LD_LIBRARYN32_PATH=$W
LD_LIBRARY64_PATH=$W
export LD_LIBRARY_PATH LD_LIBRARYN32_PATH LD_LIBRARY64_PATH
fail=0
ran=0
# The crash test's core dump goes to the temporary directory too.
cd $W || exit 1
for s in $T/*.ttrdb; do
    name=`basename $s .ttrdb`
    prog=`sed -n 's/^# program: *//p' $s | head -1`
    ran=`expr $ran + 1`
    # -d: the sources, wherever the programs were compiled.
    if $W/ttrdb -d $T/progs -x $s $W/$prog > $W/$name.out 2>&1; then
        echo "ok: $name"
    else
        fail=`expr $fail + 1`
        echo "FAIL: $name"
        cat $W/$name.out
    fi
done
cd /
rm -rf $W
echo "td tests: $ran scripts, $fail failed"
[ $fail -eq 0 ]
