#!/usr/bin/env python3
"""Generate Spanfall LiveArea art procedurally (Pillow). Original art, no external assets.

Writes 8-bit palettized PNGs (PNG colour type 3). The Vita refuses to install a VPK whose
sce_sys PNGs are RGB/RGBA (error 0x8010113D), so every file is quantized and then its IHDR
bytes are re-read and checked:
  sce_sys/icon0.png                        128x128  a well of sand with one colour spanning it
  sce_sys/livearea/contents/bg.png         840x500  sand dunes with falling pieces
  sce_sys/livearea/contents/startup.png    280x158  "Spanfall" plate

The palette and the per-grain shading match src/render.c. save_indexed() and load_font()
are copied from Foldwind's tools/make_sce_sys.py.

Re-runnable: overwrites the files each time.
Usage: python3 tools/make_sce_sys.py  (Windows: py -3.11 tools/make_sce_sys.py)
"""
from pathlib import Path
import math
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
ICON = ROOT / "sce_sys" / "icon0.png"
BG = ROOT / "sce_sys" / "livearea" / "contents" / "bg.png"
STARTUP = ROOT / "sce_sys" / "livearea" / "contents" / "startup.png"

BACK = (28, 24, 32)
WELL = (14, 12, 18)
FRAME = (120, 100, 70)
RED, BLUE, GREEN, YELLOW = (214, 66, 58), (58, 118, 214), (74, 184, 88), (232, 196, 64)
LAYERS = [BLUE, RED, GREEN, YELLOW, RED, BLUE, GREEN]
SHADES = (0.80, 0.90, 1.00, 1.10)

# Tetromino block offsets, spawn orientation (T, L, S, I).
T_PIECE = [(0, 0), (1, 0), (2, 0), (1, 1)]
L_PIECE = [(0, 0), (0, 1), (0, 2), (1, 2)]
S_PIECE = [(1, 0), (2, 0), (0, 1), (1, 1)]
I_PIECE = [(0, 0), (1, 0), (2, 0), (3, 0)]


def shade(col, gx, gy):
    h = ((gx * 73856093) ^ (gy * 19349663)) & 0xFFFFFFFF
    f = SHADES[(h >> 7) & 3]
    return tuple(min(255, int(c * f)) for c in col)


def grain(d, x0, y0, g, gx, gy, col):
    x, y = x0 + gx * g, y0 + gy * g
    d.rectangle([x, y, x + g - 1, y + g - 1], fill=shade(col, gx, gy))


def dunes(d, x0, y0, cols, rows, g, top, span_layer=None):
    """Layered sand filling columns 0..cols-1 from a wavy surface `top(c)` down to the floor.
    If span_layer is set, that layer is drawn flat and lightened: a colour touching both walls."""
    band = max(3, rows // 9)
    for c in range(cols):
        surface = top(c)
        for r in range(max(0, surface), rows):
            depth = r - surface
            wave = int(2.2 * math.sin(c * 0.45) + 1.6 * math.sin(c * 0.19 + 1.3))
            layer = max(0, (depth + wave) // band)
            col = LAYERS[layer % len(LAYERS)]
            if span_layer is not None and r in span_layer:
                col = tuple(min(255, int(v * 0.55 + 255 * 0.45)) for v in YELLOW)
            grain(d, x0, y0, g, c, r, col)


def piece(d, x0, y0, g, blocks, bx, by, block, col):
    for ox, oy in blocks:
        for gy in range(block):
            for gx in range(block):
                grain(d, x0, y0, g, bx + ox * block + gx, by + oy * block + gy, col)


def well(d, x0, y0, w, h, border):
    d.rectangle([x0 - border, y0 - border, x0 + w + border - 1, y0 + h + border - 1], fill=FRAME)
    d.rectangle([x0, y0, x0 + w - 1, y0 + h - 1], fill=WELL)


def load_font(size):
    for name in ("DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                 "LiberationSans-Bold.ttf", "arialbd.ttf", "segoeuib.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default(size=size)


def make_icon():
    img = Image.new("RGB", (128, 128), BACK)
    d = ImageDraw.Draw(img)
    g, cols, rows = 4, 28, 29  # 112 x 116 px of well
    x0, y0 = 8, 8
    well(d, x0, y0, cols * g, rows * g, 4)
    dunes(d, x0, y0, cols, rows, g,
          lambda c: 13 + int(3 * math.sin(c * 0.33) + 2 * math.cos(c * 0.21)),
          span_layer=range(20, 23))
    piece(d, x0, y0, g, T_PIECE, 9, 1, 3, RED)
    return img


def make_bg():
    img = Image.new("RGB", (840, 500), BACK)
    d = ImageDraw.Draw(img)
    g = 5
    cols, rows = 840 // g, 500 // g
    dunes(d, 0, 0, cols, rows, g,
          lambda c: 62 + int(9 * math.sin(c * 0.045) + 5 * math.sin(c * 0.13 + 0.8)))
    piece(d, 0, 0, g, L_PIECE, 22, 10, 4, BLUE)
    piece(d, 0, 0, g, S_PIECE, 70, 24, 4, GREEN)
    piece(d, 0, 0, g, I_PIECE, 118, 6, 4, YELLOW)
    piece(d, 0, 0, g, T_PIECE, 140, 30, 4, RED)
    for i in range(90):  # loose grains still falling
        gx = (i * 37) % cols
        gy = 40 + (i * 53) % 20
        grain(d, 0, 0, g, gx, gy, LAYERS[i % 4])
    return img


def make_startup():
    img = Image.new("RGB", (280, 158), BACK)
    d = ImageDraw.Draw(img)
    g = 4
    cols, rows = 280 // g, 158 // g
    dunes(d, 0, 0, cols, rows, g, lambda c: 29 + int(3 * math.sin(c * 0.2) + 2 * math.sin(c * 0.07)))
    font = load_font(44)
    text = "Spanfall"
    x0, y0, x1, y1 = d.textbbox((0, 0), text, font=font)
    tx, ty = (280 - (x1 - x0)) // 2 - x0, 58 - (y1 - y0) // 2 - y0
    d.text((tx + 2, ty + 3), text, font=font, fill=(10, 8, 14))
    d.text((tx, ty), text, font=font, fill=(244, 200, 90))
    return img


def save_indexed(img, path, size):
    path.parent.mkdir(parents=True, exist_ok=True)
    q = img.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    q.save(path, format="PNG", optimize=True)
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise RuntimeError(f"{path}: not a PNG with a leading IHDR")
    width, height, depth, ctype, _comp, _filt, _il = struct.unpack(">IIBBBBB", data[16:29])
    if (width, height) != size or depth != 8 or ctype != 3:
        raise RuntimeError(f"{path}: IHDR {width}x{height} depth={depth} type={ctype}, want {size} depth 8 type 3")
    print(f"{path.relative_to(ROOT)}: {width}x{height} bit_depth={depth} colour_type={ctype} bytes={len(data)}")


def main():
    save_indexed(make_icon(), ICON, (128, 128))
    save_indexed(make_bg(), BG, (840, 500))
    save_indexed(make_startup(), STARTUP, (280, 158))
    return 0


if __name__ == "__main__":
    sys.exit(main())
