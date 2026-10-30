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
# is in /usr/Motif-2.1, which is the ttr floor. ti's image libraries: IRIX 6.5
# has libpng 1.2.5, libjpeg 6a and libtiff 3.4 but no giflib (ti's built-in
# decoder reads GIF); stb_image needs C99 mode for <stdint.h>.
CPPFLAGS=${IRIX_CPPFLAGS:-"-Isrc -I/usr/Motif-2.1/include -DTTR_HAVE_XPM -DTTR_HAVE_SVG"}
LDFLAGS=${IRIX_LDFLAGS:-"-L/usr/Motif-2.1/lib32"}
LIBS=${IRIX_LIBS:-"-lXm -lXt -lXpm -lX11 -lm"}
TI_CPPFLAGS=${IRIX_TI_CPPFLAGS:-"-DTTR_HAVE_PNG -DTTR_HAVE_JPEG -DTTR_HAVE_TIFF"}
TI_LIBS=${IRIX_TI_LIBS:-"-lpng -ljpeg -ltiff -lz"}
TARGETS=${*:-all}

exec python3 "$ROOT/tools/irix-sh.py" --timeout 7200 run \
    "cd /shared && make B=build-irix CXX=CC CXXFLAGS='$CXXFLAGS' WARNFLAGS= \
     CPPFLAGS='$CPPFLAGS' DBUS_CFLAGS= LDFLAGS='$LDFLAGS' LIBS='$LIBS' \
     TI_CPPFLAGS='$TI_CPPFLAGS' TI_LIBS='$TI_LIBS' STB_CFLAGS=-c99 \
     TP_SYS=build-irix/tp_sys_irix.o TF_SYS=build-irix/tf_sys_irix.o CC=cc CFLAGS='-n32 -O2' \
     TD_SYS=build-irix/td_target_irix.o LIBDWARF_CFLAGS=-c99 TD_DWARF_FLAGS= \
     TD_TEST_CFLAGS='-n32 -g -O0' TD_TEST_PIC= TD_TEST_SONAME='-soname libtdshlib.so' \
     $TARGETS 2>&1"
