#!/usr/bin/env python3
"""Convert ttr's icons (data/icons/*.svg) to IRIX desktop icons (.fti).

    tools/svg2fti.py data/icons/ttrt.svg > dist/irix/ttrt.fti

Only what our icons use: <rect> elements filled with a #rrggbb colour of
their own or of the <g> around them. Write icons with plain rectangles for
this; anything else is reported and left out.

FTI draws in a 100 x 100 box with y up. Its colours are the 16 of IRIX's
standard colormap, color(0) to color(15), and 50/50 mixes of two of them,
color(-(16 * a + b)) for a > b; each rectangle gets the nearest.
"""
import sys
import xml.etree.ElementTree as ET

SVG = '{http://www.w3.org/2000/svg}'

# IRIX's standard colormap, entries 0 to 15.
BASE = [
    (0, 0, 0), (255, 0, 0), (0, 255, 0), (255, 255, 0),
    (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255),
    (85, 85, 85), (198, 113, 113), (113, 198, 113), (142, 142, 56),
    (113, 113, 198), (142, 56, 142), (56, 142, 142), (170, 170, 170),
]


def palette():
    pal = [(i, c) for i, c in enumerate(BASE)]
    for a in range(16):
        for b in range(a):
            mix = tuple((BASE[a][k] + BASE[b][k]) / 2.0 for k in range(3))
            pal.append((-(16 * a + b), mix))
    return pal


PALETTE = palette()


def nearest(rgb):
    best, best_d = 0, None
    for index, c in PALETTE:
        d = sum((rgb[k] - c[k]) ** 2 for k in range(3))
        if best_d is None or d < best_d:
            best, best_d = index, d
    return best


def parse_colour(s):
    s = s.strip()
    if len(s) == 7 and s[0] == '#':
        return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))
    if len(s) == 4 and s[0] == '#':
        return tuple(int(s[i] * 2, 16) for i in (1, 2, 3))
    return None


def shapes(el, fill, out, size):
    for child in el:
        tag = child.tag.replace(SVG, '')
        f = child.get('fill', fill)
        if tag == 'g':
            shapes(child, f, out, size)
        elif tag == 'rect':
            rgb = parse_colour(f or '')
            if rgb is None:
                sys.stderr.write('svg2fti: a rect without a #rrggbb fill, left out\n')
                continue
            x, y = float(child.get('x', 0)), float(child.get('y', 0))
            w, h = float(child.get('width')), float(child.get('height'))
            out.append((nearest(rgb), x, y, w, h))
        elif tag not in ('title', 'desc', 'metadata', 'defs'):
            sys.stderr.write('svg2fti: <%s> is not supported, left out\n' % tag)


def main():
    if len(sys.argv) != 2:
        sys.stderr.write('usage: svg2fti.py icon.svg > icon.fti\n')
        return 2
    root = ET.parse(sys.argv[1]).getroot()
    view = root.get('viewBox')
    size = float(view.split()[3]) if view else float(root.get('height', 48))
    out = []
    shapes(root, root.get('fill'), out, size)
    k = 100.0 / size
    sys.stdout.write('# Made from %s by tools/svg2fti.py; edit the SVG.\n'
                     % sys.argv[1].split('/')[-1])
    for colour, x, y, w, h in out:
        x0, x1 = x * k, (x + w) * k
        y0, y1 = 100.0 - (y + h) * k, 100.0 - y * k
        sys.stdout.write('color(%d);\nbgnpolygon();\n' % colour)
        for vx, vy in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            sys.stdout.write('\tvertex(%.2f, %.2f);\n' % (vx, vy))
        sys.stdout.write('endpolygon();\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
