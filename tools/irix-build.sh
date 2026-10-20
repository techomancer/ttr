#!/bin/sh
# Build ttr on the IRIX guest with MIPSpro, from the NFS share (/shared is
# this tree), into build-irix/ so it never collides with the Linux build/.
#
#   tools/irix-build.sh              make all
#   tools/irix-build.sh clean        any make target(s)
#   IRIX_CPPFLAGS=... tools/irix-build.sh
#
# The guest must be up (tools/iris.sh start; tools/iris.sh wait). Binaries in
# /shared cannot be executed there (noexec): copy them to /tmp in the guest.

ROOT=`dirname "$0"`/..
# -no_prelink -ptused: instantiate templates per object instead of through the
# prelinker, whose ii_files/ lock files fail on the NFS share.
CXXFLAGS=${IRIX_CXXFLAGS:-"-n32 -O2 -LANG:exceptions=OFF -no_prelink -ptused"}
CPPFLAGS=${IRIX_CPPFLAGS:-"-Isrc -DTTR_HAVE_XPM -DTTR_HAVE_SVG -DTTR_HAVE_PNG"}
LIBS=${IRIX_LIBS:-"-lXm -lXt -lXpm -lpng -lX11 -lm"}
TARGETS=${*:-all}

exec python3 "$ROOT/tools/irix-sh.py" --timeout 7200 run \
    "cd /shared && make B=build-irix CXX=CC CXXFLAGS='$CXXFLAGS' WARNFLAGS= \
     CPPFLAGS='$CPPFLAGS' DBUS_CFLAGS= LIBS='$LIBS' $TARGETS 2>&1"
