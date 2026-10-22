#!/bin/sh
# Rebuild tf's archive test fixtures in tf/test/data (run on Linux, which
# has every tool; the fixtures are checked in so IRIX runs the same tests).
set -e
ROOT=`dirname "$0"`/..
OUT="$ROOT/tf/test/data"
W=`mktemp -d`
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/t/dir/sub"
printf 'hello\n' > "$W/t/hello.txt"
printf 'deep\n' > "$W/t/dir/sub/deep.txt"
ln -s hello.txt "$W/t/link"
LONG=a_name_that_is_longer_than_the_hundred_characters_a_plain_tar_header_can_hold_so_it_needs_an_extension.txt
printf 'long\n' > "$W/t/$LONG"
# 70 KB of varied bytes: more than one 64 KB chunk.
python3 -c "import random; random.seed(7); open('$W/t/big.bin','wb').write(bytes(random.randrange(256) for _ in range(70000)))"
touch -d '2020-02-02 12:00:00' "$W/t/hello.txt"
python3 - "$W" <<'PY'
import sys, tarfile, zipfile, os
w = sys.argv[1]
os.chdir(w)
for fmt, name in ((tarfile.GNU_FORMAT, 't.tar'), (tarfile.PAX_FORMAT, 't_pax.tar')):
    with tarfile.open(name, 'w', format=fmt) as t:
        t.add('t', recursive=True)
# Names that must not escape: ../ and absolute paths.
with tarfile.open('evil.tar', 'w', format=tarfile.GNU_FORMAT) as t:
    for n in ('../escape.txt', '/abs.txt', 'ok/../../up.txt', 'fine.txt'):
        data = b'x\n'
        ti = tarfile.TarInfo(n); ti.size = len(data)
        import io; t.addfile(ti, io.BytesIO(data))
PY
cd "$W"
gzip -9 -c t.tar > t.tar.gz
bzip2 -9 -c t.tar > t.tar.bz2
compress -c t.tar > t.tar.Z
zip -q -r -y t.zip t
zip -q -r -y -0 t_stored.zip t
gzip -9 -c t/hello.txt > hello.txt.gz
bzip2 -9 -c t/hello.txt > hello.txt.bz2
compress -c t/hello.txt > hello.txt.Z
head -c 3000 t.tar.gz > truncated.tar.gz
cp t.tar t_pax.tar t.tar.gz t.tar.bz2 t.tar.Z t.zip t_stored.zip hello.txt.gz \
   hello.txt.bz2 hello.txt.Z evil.tar truncated.tar.gz "$OUT/"
ls -l "$OUT"
