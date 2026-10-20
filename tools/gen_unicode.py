#!/usr/bin/env python3
"""Generate tl/unicode_data.cpp from the Unicode Character Database.

    tools/gen_unicode.py ignore/ucd > tl/unicode_data.cpp

Needs, under the given directory: UnicodeData.txt, EastAsianWidth.txt,
DerivedCoreProperties.txt, auxiliary/GraphemeBreakProperty.txt,
emoji/emoji-data.txt, ReadMe.txt (for the version).

One property byte per code point, in a two-stage table of 128-code-point
blocks with identical blocks shared:

    bits 0-1  width class: 0 zero, 1 narrow, 2 wide, 3 ambiguous
    bits 2-5  grapheme cluster break class (tl::GraphemeClass)
    bits 6-7  Indic conjunct break: 0 none, 1 consonant, 2 linker, 3 extend

The rules match what xterm, kitty and ghostty do rather than wcwidth(3):
Mn, Me, Cf, Default_Ignorable and the conjoining Hangul jamo V/T are
zero width; East Asian W and F and Emoji_Presentation characters are wide;
East Asian A is its own class so a host can treat it as wide for CJK
locales; everything else, including unassigned code points, is narrow.
Controls (Cc) are zero width: the interpreter never prints them.
"""
import os
import re
import sys

MAX_CP = 0x110000
BLOCK = 128

GB_CLASSES = ["Other", "CR", "LF", "Control", "Extend", "ZWJ",
              "Regional_Indicator", "Prepend", "SpacingMark",
              "L", "V", "T", "LV", "LVT", "Extended_Pictographic"]


def ranges(path, field=1):
    """Yield (lo, hi, value) from a UCD file with 'cp[..cp] ; value' lines."""
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = [p.strip() for p in line.split(";")]
            cps = parts[0]
            if ".." in cps:
                lo, hi = (int(x, 16) for x in cps.split(".."))
            else:
                lo = hi = int(cps, 16)
            yield lo, hi, parts[field]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    ucd = sys.argv[1]
    version = "unknown"
    with open(os.path.join(ucd, "ReadMe.txt"), encoding="utf-8") as f:
        m = re.search(r"version (\d+\.\d+\.\d+)", f.read())
        if m:
            version = m.group(1)

    width = bytearray([1]) * MAX_CP
    gb = bytearray(MAX_CP)
    incb = bytearray(MAX_CP)

    # General categories from UnicodeData.txt (ranges via First/Last).
    cat = {}
    first = None
    with open(os.path.join(ucd, "UnicodeData.txt"), encoding="utf-8") as f:
        for line in f:
            parts = line.rstrip("\n").split(";")
            cp = int(parts[0], 16)
            name, gc = parts[1], parts[2]
            if name.endswith(", First>"):
                first = cp
                continue
            if name.endswith(", Last>"):
                for c in range(first, cp + 1):
                    cat[c] = gc
                first = None
                continue
            cat[cp] = gc
    for cp, gc in cat.items():
        if gc in ("Mn", "Me", "Cf", "Cc"):
            width[cp] = 0

    for lo, hi, v in ranges(os.path.join(ucd, "DerivedCoreProperties.txt")):
        if v == "Default_Ignorable_Code_Point":
            for cp in range(lo, hi + 1):
                width[cp] = 0
    # InCB is 'cp ; InCB ; value'
    with open(os.path.join(ucd, "DerivedCoreProperties.txt"), encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            parts = [p.strip() for p in line.split(";")]
            if len(parts) < 3 or parts[1] != "InCB":
                continue
            cps = parts[0]
            if ".." in cps:
                lo, hi = (int(x, 16) for x in cps.split(".."))
            else:
                lo = hi = int(cps, 16)
            val = {"Consonant": 1, "Linker": 2, "Extend": 3}[parts[2]]
            for cp in range(lo, hi + 1):
                incb[cp] = val

    for lo, hi, v in ranges(os.path.join(ucd, "EastAsianWidth.txt")):
        for cp in range(lo, hi + 1):
            if width[cp] == 0:
                continue
            if v in ("W", "F"):
                width[cp] = 2
            elif v == "A":
                width[cp] = 3

    for lo, hi, v in ranges(os.path.join(ucd, "auxiliary", "GraphemeBreakProperty.txt")):
        cls = GB_CLASSES.index(v)
        for cp in range(lo, hi + 1):
            gb[cp] = cls
    for lo, hi, v in ranges(os.path.join(ucd, "emoji", "emoji-data.txt")):
        if v == "Extended_Pictographic":
            for cp in range(lo, hi + 1):
                if gb[cp] == 0:
                    gb[cp] = GB_CLASSES.index("Extended_Pictographic")
        elif v == "Emoji_Presentation":
            for cp in range(lo, hi + 1):
                if width[cp] != 0:
                    width[cp] = 2

    # Conjoining jamo vowels and trailing consonants combine with the
    # leading consonant and take no column of their own.
    for cp in range(0x1160, 0x1200):
        width[cp] = 0
    for cp in range(0xD7B0, 0xD800):
        width[cp] = 0
    # U+00AD SOFT HYPHEN is Cf but xterm and friends draw it.
    width[0xAD] = 1

    props = bytearray(MAX_CP)
    for cp in range(MAX_CP):
        props[cp] = width[cp] | (gb[cp] << 2) | (incb[cp] << 6)

    blocks = []
    index = {}
    stage1 = []
    for b in range(0, MAX_CP, BLOCK):
        blk = bytes(props[b:b + BLOCK])
        if blk not in index:
            index[blk] = len(blocks)
            blocks.append(blk)
        stage1.append(index[blk])
    assert len(blocks) < 65536

    out = sys.stdout
    out.write("/* Generated by tools/gen_unicode.py from the Unicode Character\n"
              " * Database %s. Do not edit; see that script for the layout. */\n"
              "#include \"unicode.h\"\n\nnamespace tl {\n\n" % version)
    out.write("const char* const k_unicode_version = \"%s\";\n\n" % version)
    out.write("const uint16_t k_unicode_stage1[%d] = {\n" % len(stage1))
    for i in range(0, len(stage1), 12):
        out.write("    " + ",".join("%d" % v for v in stage1[i:i + 12]) + ",\n")
    out.write("};\n\n")
    out.write("const uint8_t k_unicode_stage2[%d] = {\n" % (len(blocks) * BLOCK))
    for blk in blocks:
        for i in range(0, BLOCK, 32):
            out.write("    " + ",".join("%d" % v for v in blk[i:i + 32]) + ",\n")
    out.write("};\n\n} /* namespace tl */\n")
    sys.stderr.write("UCD %s: %d blocks, %d bytes of tables\n"
                     % (version, len(blocks), len(stage1) * 2 + len(blocks) * BLOCK))


if __name__ == "__main__":
    main()
