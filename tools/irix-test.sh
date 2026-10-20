#!/bin/sh
# Build the tl tests with MIPSpro on the IRIX guest and run them there.
# /shared is noexec, so the binary is copied to /tmp; the test data and
# the libvterm fixtures are read from /shared directly.
#
#   tools/irix-test.sh
ROOT=`dirname "$0"`/..
"$ROOT/tools/irix-build.sh" build-irix/tl_test || exit 1
exec python3 "$ROOT/tools/irix-sh.py" --timeout 600 run \
    'cp /shared/build-irix/tl_test /tmp/tl_test && chmod a+x /tmp/tl_test' \
    '/tmp/tl_test -d /shared/tl/test/data -v /shared/third_party/libvterm/t'
