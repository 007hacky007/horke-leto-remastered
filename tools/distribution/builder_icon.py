"""Draw the builder's own icon: original 32x32 pixel art (island, sun, wrench), no game pixels.

Writes builder/art/builder-icon.png plus .icns and .ico for packaging.
"""
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'builder/art'

SKY = [(255, 216, 96), (255, 192, 80), (248, 160, 72), (232, 128, 72)]
SEA, SEA2, SAND, SUN = (16, 96, 176), (40, 128, 200), (240, 208, 128), (255, 248, 160)
TRUNK, LEAF, LEAF2 = (128, 80, 32), (24, 136, 40), (64, 184, 56)
STEEL, STEEL2, OUTLINE = (200, 200, 208), (128, 128, 144), (0, 0, 0)


def pixels():
    image = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    d.rounded_rectangle((0, 0, 31, 31), radius=6, fill=SKY[0])
    for i, color in enumerate(SKY):
        d.rectangle((1, 2+i*4, 30, 5+i*4), fill=color)
    d.ellipse((18, 4, 27, 13), fill=SUN)
    for y in range(18, 31):
        d.line((1, y, 30, y), fill=SEA if y % 2 else SEA2)
    d.ellipse((4, 19, 22, 28), fill=SAND)
    d.line((12, 22, 13, 13), fill=TRUNK, width=1)
    d.line((13, 22, 14, 13), fill=TRUNK, width=1)
    for dx, dy in [(-6, 2), (-4, -1), (4, -1), (6, 2), (0, -3)]:
        d.line((13, 12, 13+dx, 12+dy), fill=LEAF, width=2)
    d.point([(11, 11), (15, 11), (13, 10)], fill=LEAF2)
    # Wrench across the lower right corner.
    d.line((19, 29, 28, 20), fill=OUTLINE, width=4)
    d.line((19, 29, 28, 20), fill=STEEL, width=2)
    d.ellipse((25, 16, 31, 22), fill=OUTLINE)
    d.ellipse((26, 17, 30, 21), fill=STEEL)
    d.rectangle((28, 17, 30, 19), fill=SKY[2])
    d.point([(20, 27), (22, 25), (24, 23)], fill=STEEL2)
    d.rounded_rectangle((0, 0, 31, 31), radius=6, outline=OUTLINE)
    return image


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    big = pixels().resize((1024, 1024), Image.NEAREST)
    big.save(OUT/'builder-icon.png')
    big.save(OUT/'builder-icon.icns', format='ICNS', sizes=[(s, s) for s in (16, 32, 64, 128, 256, 512, 1024)])
    big.save(OUT/'builder-icon.ico', format='ICO', sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    print(OUT/'builder-icon.png')


if __name__ == '__main__':
    main()
