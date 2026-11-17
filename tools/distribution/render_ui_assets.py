"""Render the builder's original pixel-art SVG icons to PNG for embedding (developer tool, needs PySide6)."""
from pathlib import Path
import sys

from PySide6.QtCore import Qt
from PySide6.QtGui import QGuiApplication, QImage, QPainter
from PySide6.QtSvg import QSvgRenderer

ROOT = Path(__file__).resolve().parents[2]
SOURCE, TARGET = ROOT/'builder/art', ROOT/'builder/assets'
SIZES = {'cdr': [128, 192], 'default': [16, 32]}


def main():
    QGuiApplication(sys.argv[:1] + ['-platform', 'offscreen'])
    TARGET.mkdir(parents=True, exist_ok=True)
    for svg in sorted(SOURCE.glob('*.svg')):
        renderer = QSvgRenderer(str(svg))
        for size in SIZES.get(svg.stem, SIZES['default']):
            image = QImage(size, size, QImage.Format_ARGB32)
            image.fill(Qt.transparent)
            painter = QPainter(image)
            renderer.render(painter)
            painter.end()
            image.save(str(TARGET/f'{svg.stem}-{size}.png'))
    (TARGET/'builder-icon.png').write_bytes((SOURCE/'builder-icon.png').read_bytes())
    print(sorted(p.name for p in TARGET.iterdir()))


if __name__ == '__main__':
    main()
