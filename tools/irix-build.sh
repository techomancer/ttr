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
# -woff 3970: "conversion from pointer to same-sized integral type", which every
# XtSetArg of a pointer triggers.
CXXFLAGS=${IRIX_CXXFLAGS:-"-n32 -O2 -LANG:exceptions=OFF -no_prelink -ptused -woff 3970"}
# IRIX 6.5's /usr/include/Xm is Motif 1.2; Motif 2.1 (IRIX IM 2.1, libXm.so.2)
# is in /usr/Motif-2.1, which is the ttr floor. No PNG: IRIX 6.5 ships libpng
# 1.2.5 and src/x11/iconload.cpp uses the simplified API of libpng 1.6.
CPPFLAGS=${IRIX_CPPFLAGS:-"-Isrc -I/usr/Motif-2.1/include -DTTR_HAVE_XPM -DTTR_HAVE_SVG"}
LDFLAGS=${IRIX_LDFLAGS:-"-L/usr/Motif-2.1/lib32"}
LIBS=${IRIX_LIBS:-"-lXm -lXt -lXpm -lX11 -lm"}
TARGETS=${*:-all}

exec python3 "$ROOT/tools/irix-sh.py" --timeout 7200 run \
    "cd /shared && make B=build-irix CXX=CC CXXFLAGS='$CXXFLAGS' WARNFLAGS= \
     CPPFLAGS='$CPPFLAGS' DBUS_CFLAGS= LDFLAGS='$LDFLAGS' LIBS='$LIBS' \
     TP_SYS=build-irix/tp_sys_irix.o TF_SYS=build-irix/tf_sys_irix.o CC=cc CFLAGS='-n32 -O2' $TARGETS 2>&1"
