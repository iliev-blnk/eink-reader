"""Cover image -> 1 bpp bitmap for the COVR section (CONV-6).

Scaled to fit the portrait screen (240 x 416) without cropping, contrast
stretched, then dithered with Floyd–Steinberg error diffusion: each pixel is
set to black or white, and the rounding error is pushed to the neighbours
that are not done yet (7/16 ahead, 3/16 below behind, 5/16 below, 1/16 below
ahead), so the average grey over an area is kept. Rows alternate direction.

Bitmap layout as in the spec: rows padded to whole bytes, MSB = leftmost
pixel, bit 1 = white.
"""
from __future__ import annotations

import io

import numpy as np
from PIL import Image, ImageOps

import ibv

SCREEN_W, SCREEN_H = 240, 416


def load(data: bytes, max_w: int = SCREEN_W, max_h: int = SCREEN_H) -> ibv.Cover:
    im = Image.open(io.BytesIO(data))
    im = ImageOps.exif_transpose(im)
    if im.mode in ("RGBA", "LA", "P"):
        im = im.convert("RGBA")
        bg = Image.new("RGBA", im.size, "white")
        im = Image.alpha_composite(bg, im)
    im = im.convert("L")
    scale = min(max_w / im.width, max_h / im.height)
    size = (max(1, round(im.width * scale)), max(1, round(im.height * scale)))
    im = im.resize(size, Image.Resampling.LANCZOS)
    im = ImageOps.autocontrast(im, cutoff=1)
    return to_cover(np.asarray(im, dtype=np.float32))


def dither(grey: np.ndarray) -> np.ndarray:
    """Floyd–Steinberg, serpentine (every other row right to left, which
    avoids the diagonal "worms" in flat greys). grey: 2-D array 0..255.
    Returns a bool array, True = white."""
    h, w = grey.shape
    rows = [list(map(float, r)) for r in grey] + [[0.0] * w]   # plain lists: ~10x faster than numpy per pixel
    out = np.zeros((h, w), dtype=bool)
    for y in range(h):
        row, below, bits = rows[y], rows[y + 1], out[y]
        step = 1 if y % 2 == 0 else -1
        xs = range(w) if step == 1 else range(w - 1, -1, -1)
        for x in xs:
            old = row[x]
            white = old >= 128.0
            if white:
                bits[x] = True
                err = old - 255.0
            else:
                err = old
            nx, px = x + step, x - step
            if 0 <= nx < w:
                row[nx] += err * 0.4375          # 7/16 ahead
                below[nx] += err * 0.0625        # 1/16 below ahead
            if 0 <= px < w:
                below[px] += err * 0.1875        # 3/16 below behind
            below[x] += err * 0.3125             # 5/16 below
    return out


def to_cover(grey: np.ndarray) -> ibv.Cover:
    white = dither(grey)
    h, w = white.shape
    bits = np.packbits(white, axis=1)      # pads each row to whole bytes, MSB first
    return ibv.Cover(w, h, bits.tobytes())


def to_image(c: ibv.Cover) -> Image.Image:
    """For previews and tests."""
    row = (c.width + 7) // 8
    arr = np.frombuffer(c.bits, dtype=np.uint8).reshape(c.height, row)
    px = np.unpackbits(arr, axis=1)[:, :c.width]
    return Image.fromarray((px * 255).astype(np.uint8), "L")
