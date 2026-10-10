"""Draw the menu icons (92x64): assets-cg/icon-uns.png and icon-sel.png.

usage: python3 tools/mkicon.py   (needs Pillow and the Impact font of macOS)
"""
import math
from PIL import Image, ImageDraw, ImageFont

FONT = '/System/Library/Fonts/Supplemental/Impact.ttf'


def icon(bg, frame, path):
    im = Image.new('RGB', (92, 64), bg)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 91, 63], outline=frame, width=2)
    d.line([(4, 40), (87, 40)], fill=(150, 150, 160))      # x axis
    d.line([(46, 22), (46, 60)], fill=(150, 150, 160))     # y axis
    for color, f in (((40, 110, 230), lambda x: 10 * math.sin(x / 7)),
                     ((230, 60, 50), lambda x: (x - 46) ** 2 / 120 - 12)):
        pts = [(x, 40 - f(x)) for x in range(4, 88)]
        pts = [(x, y) for x, y in pts if 18 < y < 61]
        d.line(pts, fill=color, width=3)
    font = ImageFont.truetype(FONT, 17)
    w = d.textlength('FastGraph', font=font)
    d.text(((92 - w) / 2, 1), 'FastGraph', font=font, fill=(255, 255, 255) if bg[0] < 128 else (20, 20, 30))
    im.save(path)


icon((30, 34, 48), (90, 95, 110), 'assets-cg/icon-uns.png')
icon((60, 75, 120), (255, 255, 255), 'assets-cg/icon-sel.png')
