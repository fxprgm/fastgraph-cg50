"""Turn a BDF bitmap font into an fxconv font grid (printable ASCII, 16 glyphs per row).

usage: python3 tools/bdf2png.py <font.bdf> <out.png>
Prints the cell size for fxconv-metadata.txt (grid.size). Black glyphs on white.
"""
import sys
from PIL import Image


def parse(path):
    glyphs, font_bb, cur = {}, None, None
    lines = open(path, encoding='latin-1').read().splitlines()
    i = 0
    while i < len(lines):
        p = lines[i].split()
        if not p:
            i += 1
            continue
        if p[0] == 'FONTBOUNDINGBOX':
            font_bb = tuple(map(int, p[1:5]))
        elif p[0] == 'STARTCHAR':
            cur = {}
        elif p[0] == 'ENCODING':
            cur['code'] = int(p[1])
        elif p[0] == 'BBX':
            cur['bbx'] = tuple(map(int, p[1:5]))
        elif p[0] == 'BITMAP':
            w, h, _, _ = cur['bbx']
            rows = [int(lines[i + 1 + r], 16) for r in range(h)]
            nbits = ((w + 7) // 8) * 8
            cur['rows'] = rows, nbits
            glyphs[cur['code']] = cur
            i += h
        i += 1
    return font_bb, glyphs


def main(bdf, out):
    (fw, fh, fx, fy), glyphs = parse(bdf)
    cols, n = 16, 95
    img = Image.new('RGB', (cols * fw, ((n + cols - 1) // cols) * fh), 'white')
    for k in range(n):
        g = glyphs.get(32 + k)
        if not g:
            continue
        w, h, ox, oy = g['bbx']
        rows, nbits = g['rows']
        x0 = (k % cols) * fw + (ox - fx)
        y0 = (k // cols) * fh + (fh + fy) - (h + oy)
        for r, bits in enumerate(rows):
            for c in range(w):
                if bits >> (nbits - 1 - c) & 1:
                    img.putpixel((x0 + c, y0 + r), (0, 0, 0))
    img.save(out)
    print(f'{out}: grid.size: {fw}x{fh}')


if __name__ == '__main__':
    main(*sys.argv[1:])
