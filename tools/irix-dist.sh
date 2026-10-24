#!/bin/sh
# Build an IRIX inst package (tardist) of ttr in the guest: tools/irix-dist.sh
#
# Builds ttr, ttrt, ttrc, ttre and ttrcode with MIPSpro (tools/irix-build.sh),
# stages the files under /tmp/ttr-dist/root in the guest, writes the idb (sorted on fields 5
# and 6, as gendist requires), runs gendist with dist/irix/ttr.spec and
# bundles build-irix/ttr-VERSION.tardist. Install it in IRIX with
#   inst -f ttr-VERSION.tardist        (or: inst -a -f DIR -I ttr)
# See the irix-inst-packaging skill for the formats.
ROOT=`dirname "$0"`/..
VERSION=${VERSION:-`date +%Y%m%d`}
SH="python3 $ROOT/tools/irix-sh.py --timeout 1800"

"$ROOT/tools/irix-build.sh" build-irix/ttr build-irix/ttrt build-irix/ttrc \
    build-irix/ttre build-irix/ttrcode || exit 1

# The idb: type mode owner group target source subsystem [attributes].
IDB="$ROOT/build-irix/ttr.idb"
{
    echo "f 0755 root sys usr/bin/ttr usr/bin/ttr ttr.sw.wm"
    echo "f 0755 root sys usr/bin/ttrt usr/bin/ttrt ttr.sw.term"
    echo "f 0755 root sys usr/bin/ttrc usr/bin/ttrc ttr.sw.commander"
    echo "f 0755 root sys usr/bin/ttre usr/bin/ttre ttr.sw.commander"
    echo "f 0755 root sys usr/bin/ttrcode usr/bin/ttrcode ttr.sw.code"
    # Nothing on IRIX is called tc (Linux has iproute2's): a link too.
    echo "l 0777 root sys usr/bin/tc usr/bin/tc ttr.sw.commander symval(ttrc)"
    echo "f 0644 root sys usr/share/ttrc/tc.sh usr/share/ttrc/tc.sh ttr.sw.commander"
    echo "f 0644 root sys usr/share/ttrc/tc.csh usr/share/ttrc/tc.csh ttr.sw.commander"
    for f in "$ROOT"/data/schemes/ttr/* "$ROOT"/data/schemes/irix/*; do
        rel=usr/share/ttr/schemes/`basename \`dirname "$f"\``/`basename "$f"`
        echo "f 0644 root sys $rel $rel ttr.sw.wm"
    done
    for f in "$ROOT"/data/schemes/text/*; do
        rel=usr/share/ttr/schemes/text/`basename "$f"`
        echo "f 0644 root sys $rel $rel ttr.sw.code"
    done
    # The fonts make install copies: TW_FONTS in the Makefile.
    fonts=`make -s -C "$ROOT" tw-fonts`
    for f in $fonts third_party/misc-misc/COPYING; do
        rel=usr/share/ttr/fonts/`basename "$f"`
        echo "f 0644 root sys $rel $rel ttr.sw.term"
    done
    # terminfo is compiled on the target, into the system directory.
    echo "f 0644 root sys usr/share/ttr/ttrt.ti usr/share/ttr/ttrt.ti ttr.sw.term" \
         "exitop(\"TERMINFO=\$rbase/usr/share/lib/terminfo tic \$rbase/usr/share/ttr/ttrt.ti\")" \
         "removeop(\"rm -f \$rbase/usr/share/lib/terminfo/t/ttrt\")"
    echo "f 0644 root sys usr/lib/X11/app-chests/ttrt.chest usr/lib/X11/app-chests/ttrt.chest ttr.sw.term"
} > "$IDB"

sed "s/VERSION/$VERSION/g" "$ROOT/dist/irix/ttr.spec" > "$ROOT/build-irix/ttr.spec"

# Each command does its own cd: irix-sh.py runs long lines from a script,
# where a cd would not carry over to the next command.
IN="cd /tmp/ttr-dist/root &&"
$SH run \
"rm -rf /tmp/ttr-dist && mkdir -p /tmp/ttr-dist/root/usr/bin /tmp/ttr-dist/out" \
"$IN mkdir -p usr/share/ttr/fonts usr/share/ttr/schemes/ttr usr/share/ttr/schemes/irix usr/share/ttr/schemes/text" \
"$IN mkdir -p usr/share/ttrc usr/lib/X11/app-chests" \
"$IN cd /shared/build-irix && cp ttr ttrt ttrc ttre ttrcode /tmp/ttr-dist/root/usr/bin/" \
"$IN strip usr/bin/ttr usr/bin/ttrt usr/bin/ttrc usr/bin/ttre usr/bin/ttrcode && ln -s ttrc usr/bin/tc" \
"$IN /usr/sbin/tag 0x54545252 usr/bin/ttr; /usr/sbin/tag 0x54545254 usr/bin/ttrt" \
"$IN cp /shared/data/ttrc/tc.sh /shared/data/ttrc/tc.csh usr/share/ttrc/" \
"$IN cp /shared/data/schemes/ttr/* usr/share/ttr/schemes/ttr/" \
"$IN cp /shared/data/schemes/irix/* usr/share/ttr/schemes/irix/" \
"$IN cp /shared/data/schemes/text/* usr/share/ttr/schemes/text/" \
"$IN for f in \`awk '\$7 == \"ttr.sw.term\" { print \$5 }' /shared/build-irix/ttr.idb | grep fonts/\`; do cp /shared/third_party/misc-misc/\`basename \$f\` \$f; done" \
"$IN cp /shared/data/ttrt.ti usr/share/ttr/ttrt.ti && cp /shared/dist/irix/ttrt.chest usr/lib/X11/app-chests/" \
"cd /tmp/ttr-dist && sort -k 5,6 /shared/build-irix/ttr.idb -o ttr.idb && /usr/sbin/gendist -sbase root -idb ttr.idb -spec /shared/build-irix/ttr.spec -dist out 2>&1 | tail -5" \
"cd /tmp/ttr-dist/out && ls -l && tar cf /shared/build-irix/ttr-$VERSION.tardist * && ls -l /shared/build-irix/ttr-$VERSION.tardist"
