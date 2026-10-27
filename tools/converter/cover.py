"""Imaginea copertei -> bitmap de 1 bpp pentru secțiunea COVR (CONV-6).

Scalată să încapă pe ecranul în portret (240 x 416) fără decupare, cu
contrastul întins, apoi cu dithering prin difuzia erorii Floyd–Steinberg:
fiecare pixel devine negru sau alb, iar eroarea de rotunjire se împinge spre
vecinii încă netratați (7/16 înainte, 3/16 dedesubt în urmă, 5/16 dedesubt,
1/16 dedesubt înainte), deci se păstrează griul mediu pe o zonă. Rândurile
alternează direcția.

Așezarea bitmapului ca în specificație: rânduri completate la octeți întregi,
MSB = pixelul cel mai din stânga, bitul 1 = alb.
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
    """Floyd–Steinberg, în serpentină (un rând din două de la dreapta la stânga,
    ceea ce evită „viermii” diagonali în griurile uniforme). grey: tablou 2-D 0..255.
    Întoarce un tablou bool, True = alb."""
    h, w = grey.shape
    rows = [list(map(float, r)) for r in grey] + [[0.0] * w]   # liste simple: de ~10 ori mai repede decât numpy pe pixel
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
                row[nx] += err * 0.4375          # 7/16 înainte
                below[nx] += err * 0.0625        # 1/16 dedesubt înainte
            if 0 <= px < w:
                below[px] += err * 0.1875        # 3/16 dedesubt în urmă
            below[x] += err * 0.3125             # 5/16 dedesubt
    return out


def to_cover(grey: np.ndarray) -> ibv.Cover:
    white = dither(grey)
    h, w = white.shape
    bits = np.packbits(white, axis=1)      # completează fiecare rând la octeți întregi, MSB primul
    return ibv.Cover(w, h, bits.tobytes())


def to_image(c: ibv.Cover) -> Image.Image:
    """Pentru previzualizări și teste."""
    row = (c.width + 7) // 8
    arr = np.frombuffer(c.bits, dtype=np.uint8).reshape(c.height, row)
    px = np.unpackbits(arr, axis=1)[:, :c.width]
    return Image.fromarray((px * 255).astype(np.uint8), "L")
