#!/bin/sh
# Screenshot of the IRIX guest: dump the emulated framebuffer through the
# iris monitor and crop it to the visible 1280x1024.
#
#   tools/irix-shot.sh [out.png]      default: ignore/shots/screen.png
#
# Needs Python with Pillow on the host.
ROOT=`dirname "$0"`/..
OUT=${1:-$ROOT/ignore/shots/screen.png}
python3 "$ROOT/tools/mon.py" "rex fbdump shots" > /dev/null || exit 1
python3 - "$ROOT/ignore/shots/rgb.png" "$OUT" <<'EOF'
import sys
from PIL import Image
Image.open(sys.argv[1]).crop((0, 0, 1280, 1024)).save(sys.argv[2])
print(sys.argv[2])
EOF
