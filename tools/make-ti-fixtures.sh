#!/bin/sh
# Writes ti's test images (ti/test/data) with Python's PIL and
# ImageMagick. ti/test/test_ti.cpp knows the patterns: colour
# (x*36, y*60, 255-x*20-y*10), alpha 255-(x+y)*20, grey x*30+y*12; 7x5.
ROOT=`dirname "$0"`/..
OUT="$ROOT/ti/test/data"
mkdir -p "$OUT" || exit 1
python3 - "$OUT" <<'PY'
import sys, struct
from PIL import Image
out = sys.argv[1]
W, H = 7, 5
def col(x, y): return (x*36, y*60, 255-x*20-y*10)
def alpha(x, y): return 255-(x+y)*20
def grey(x, y): return x*30+y*12
rgb = Image.new('RGB', (W, H)); rgba = Image.new('RGBA', (W, H)); g = Image.new('L', (W, H))
for y in range(H):
    for x in range(W):
        rgb.putpixel((x, y), col(x, y))
        rgba.putpixel((x, y), col(x, y) + (alpha(x, y),))
        g.putpixel((x, y), grey(x, y))
rgb.save(out + '/rgb.png')
rgba.save(out + '/rgba.png')
g.save(out + '/grey.png')
# Palette with one transparent entry: pixel (0,0).
pal = rgb.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
pal.save(out + '/pal.png', transparency=pal.getpixel((0, 0)))
pal.save(out + '/pal.gif', transparency=pal.getpixel((0, 0)), interlace=False)
pal.save(out + '/pal_interlaced.gif', transparency=pal.getpixel((0, 0)), interlace=True)
# 16 bits a sample.
Image.new('I;16', (W, H)).save(out + '/blank16.png')
rgb.save(out + '/rgb.jpg', quality=98, subsampling=0)
rgb.save(out + '/rgb_progressive.jpg', quality=98, subsampling=0, progressive=True)
g.save(out + '/grey.jpg', quality=98)
rgb.convert('CMYK').save(out + '/cmyk.jpg', quality=98)
rgb.save(out + '/rgb.tif')
rgba.save(out + '/rgba_lzw.tif', compression='tiff_lzw')
rgb.save(out + '/rgb.bmp')
rgb.save(out + '/rgb.ppm')
g.save(out + '/grey.pgm')
rgb.save(out + '/rgb.sgi')
# SGI with RLE, written by hand (PIL writes it uncompressed): RGBA.
chans = [[[ (col(x, y) + (alpha(x, y),))[c] for x in range(W)] for y in range(H - 1, -1, -1)]
         for c in range(4)]
rows = []
for c in range(4):
    for r in chans[c]:
        b = b''
        # A literal run of 3, then a repeat run for the rest, then 0.
        b += bytes([0x80 | 3]) + bytes(r[:3])
        rest = r[3:]
        for v in rest:
            b += bytes([1, v])
        b += b'\0'
        rows.append(b)
hdr = struct.pack('>hbbHHHH', 474, 1, 1, 3, W, H, 4) + struct.pack('>ii', 0, 255)
hdr += b'\0' * 4 + b'rle'.ljust(80, b'\0') + struct.pack('>i', 0)
hdr = hdr.ljust(512, b'\0')
n = H * 4
off = 512 + n * 8
starts, lens, data = [], [], b''
for b in rows:
    starts.append(off + len(data)); lens.append(len(b)); data += b
f = open(out + '/rgba_rle.sgi', 'wb')
f.write(hdr + b''.join(struct.pack('>I', s) for s in starts) +
        b''.join(struct.pack('>I', l) for l in lens) + data)
f.close()
# Rows over 8 KB in one uncompressed strip: libtiff 3.4 divides by zero
# chopping it unless told not to.
Image.new('RGB', (3000, 2), (10, 20, 30)).save(out + '/wide.tif')
# Cut short.
d = open(out + '/rgb.png', 'rb').read()
open(out + '/short.png', 'wb').write(d[:len(d) // 2])
PY
magick "$OUT/rgb.png" -interlace PNG "$OUT/rgb_interlaced.png"
magick "$OUT/rgb.png" -endian MSB -compress None "$OUT/rgb_msb.tif"
ls -l "$OUT"
