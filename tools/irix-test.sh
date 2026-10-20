#!/bin/sh
# Build the tl tests with MIPSpro on the IRIX guest and run them there.
# /shared is noexec, so the binary and the data go to /tmp first.
#
#   tools/irix-test.sh
ROOT=`dirname "$0"`/..
"$ROOT/tools/irix-build.sh" build-irix/tl_test || exit 1
exec python3 "$ROOT/tools/irix-sh.py" --timeout 600 run \
    'rm -rf /tmp/tl_test /tmp/tl_data; mkdir -p /tmp/tl_data' \
    'cp /shared/build-irix/tl_test /tmp/tl_test && chmod a+x /tmp/tl_test' \
    'cp /shared/tl/test/data/* /tmp/tl_data/' \
    '/tmp/tl_test -d /tmp/tl_data'
