"""Cuts Fusion Pixel Font 10px (OFL, github.com/TakWolf/fusion-pixel-font) to
the hanzi of GB2312 and CJK punctuation, as the bitmap file the display reads.

Latin text has its own 5x7 font in devices/core/text.cpp; this one only covers
what a Chinese session title needs.

    python3 -I devices/fonts/make_cjk.py <fusion-pixel-10px-proportional-zh_hans.bdf>

cjk10.bin, little-endian:
    "PXF1", u16 count, u8 ascent, u8 descent
    u32 code point x count, sorted
    u32 offset x count, into the glyph data
    per glyph: u8 width, u8 height, i8 x offset, i8 y offset (bottom row from
    the baseline), u8 advance, then `height` rows of ceil(width / 8) bytes, MSB first
"""

import struct
import sys
from pathlib import Path


def wanted():
    cps = set()
    for hi in range(0xB0, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                cps.add(ord(bytes([hi, lo]).decode("gb2312")))
            except UnicodeDecodeError:
                pass
    cps.update(range(0x3000, 0x3040))  # CJK symbols and punctuation
    cps.update(range(0xFF01, 0xFF5F))  # fullwidth forms
    cps.update(range(0x2010, 0x2040))  # dashes, quotes, ellipsis
    return cps


def read_bdf(path):
    glyphs = {}
    ascent = descent = 0
    lines = iter(Path(path).read_text().splitlines())
    for line in lines:
        if line.startswith("FONT_ASCENT"):
            ascent = int(line.split()[1])
        elif line.startswith("FONT_DESCENT"):
            descent = int(line.split()[1])
        elif line.startswith("STARTCHAR"):
            cp = adv = None
            bbx = None
            for line in lines:
                if line.startswith("ENCODING"):
                    cp = int(line.split()[1])
                elif line.startswith("DWIDTH"):
                    adv = int(line.split()[1])
                elif line.startswith("BBX"):
                    bbx = tuple(int(v) for v in line.split()[1:5])
                elif line == "BITMAP":
                    rows = []
                    for line in lines:
                        if line == "ENDCHAR":
                            break
                        rows.append(bytes.fromhex(line))
                    glyphs[cp] = (bbx, adv, rows)
                    break
    return ascent, descent, glyphs


def main():
    ascent, descent, glyphs = read_bdf(sys.argv[1])
    keep = sorted(cp for cp in wanted() if cp in glyphs)
    data = bytearray()
    offsets = []
    for cp in keep:
        (w, h, x, y), adv, rows = glyphs[cp]
        offsets.append(len(data))
        data += struct.pack("<BBbbB", w, h, x, y, adv)
        stride = (w + 7) // 8
        for row in rows:
            data += row[:stride].ljust(stride, b"\0")
    out = Path(__file__).resolve().parent / "cjk10.bin"
    with open(out, "wb") as f:
        f.write(b"PXF1" + struct.pack("<HBB", len(keep), ascent, descent))
        f.write(struct.pack(f"<{len(keep)}I", *keep))
        f.write(struct.pack(f"<{len(keep)}I", *offsets))
        f.write(data)
    print(f"{out.name}: {len(keep)} glyphs, {out.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
