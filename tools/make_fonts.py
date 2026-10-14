#!/usr/bin/env python3
"""Generate the smooth .vlw fonts used by the bedside clock.

Renders Barlow Condensed SemiBold (numbers) and Barlow Medium (small text)
into the VLW anti-aliased font format read by M5GFX / LovyanGFX / TFT_eSPI.

Usage (from the repo root):
    python3 -m venv tools/.venv
    tools/.venv/bin/pip install pillow
    tools/.venv/bin/python tools/make_fonts.py          # writes fonts/*.vlw

The fonts are embedded in the firmware (board_build.embed_files in
platformio.ini), so rebuild and flash after regenerating them.

The Barlow fonts are (c) The Barlow Project Authors, SIL Open Font License 1.1
(see tools/ttf/OFL.txt). If the .ttf files are missing they are downloaded
from the google/fonts repository.
"""

import argparse
import os
import struct
import sys
import urllib.request

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TTF_DIR = os.path.join(ROOT, "tools", "ttf")
OUT_DIR = os.path.join(ROOT, "fonts")

TTF_URLS = {
    "BarlowCondensed-SemiBold.ttf":
        "https://raw.githubusercontent.com/google/fonts/main/ofl/barlowcondensed/BarlowCondensed-SemiBold.ttf",
    "Barlow-Medium.ttf":
        "https://raw.githubusercontent.com/google/fonts/main/ofl/barlow/Barlow-Medium.ttf",
}

NUM_TTF = "BarlowCondensed-SemiBold.ttf"
TXT_TTF = "Barlow-Medium.ttf"

ASCII = "".join(chr(c) for c in range(0x20, 0x7F))
MINUS = "−"  # typographic minus, used for negative deltas

# Clock box from the layout spec (320x240, 14px side padding).
CLOCK_MAX_W = 320 - 2 * 14
CLOCK_MAX_DIGIT_H = 124
# M5GFX copies one whole glyph onto the stack while drawing it; keep glyphs
# comfortably below the loop task stack configured in main.cpp.
MAX_GLYPH_BYTES = 14000

# name -> (ttf, size in px or None for auto, characters)
FONTS = {
    "clock": (NUM_TTF, None, "0123456789:-"),
    "bg":    (NUM_TTF, 58, "0123456789.-"),
    "delta": (NUM_TTF, 28, "0123456789.+-" + MINUS),
    "text18": (TXT_TTF, 18, ASCII + "°…"),
    "text14": (TXT_TTF, 14, ASCII + "°…"),
}


def ttf_path(name):
    path = os.path.join(TTF_DIR, name)
    if not os.path.exists(path):
        os.makedirs(TTF_DIR, exist_ok=True)
        print(f"downloading {name}")
        urllib.request.urlretrieve(TTF_URLS[name], path)
    return path


def glyph(font, ch):
    """Return (bitmap bytes, width, height, xAdvance, dY, dX) for one character."""
    advance = int(round(font.getlength(ch)))
    if ch == " ":
        return b"", 0, 0, advance, 0, 0
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
    w, h = x1 - x0, y1 - y0
    if w <= 0 or h <= 0:
        return b"", 0, 0, advance, 0, 0
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
    return img.tobytes(), w, h, advance, -y0, x0


def write_vlw(path, font, size, chars):
    ascent, descent = font.getmetrics()
    glyphs = [(ord(c),) + glyph(font, c) for c in sorted(set(chars), key=ord)]
    with open(path, "wb") as f:
        f.write(struct.pack(">6i", len(glyphs), 11, size, 0, ascent, descent))
        for code, _bmp, w, h, adv, dy, dx in glyphs:
            if w > 255 or adv > 255 or not -128 <= dx <= 127:
                sys.exit(f"{path}: glyph U+{code:04X} too large for VLW ({w}px)")
            f.write(struct.pack(">7i", code, h, w, adv, dy, dx, 0))
        for g in glyphs:
            f.write(g[1])
    biggest = max(g[2] * g[3] for g in glyphs)
    if biggest > MAX_GLYPH_BYTES:
        sys.exit(f"{path}: largest glyph is {biggest} bytes (> {MAX_GLYPH_BYTES})")
    return os.path.getsize(path), biggest


def clock_size():
    """Largest size where 'HH:MM' fits the width and digits fit the height."""
    ttf = ttf_path(NUM_TTF)
    best = None
    for size in range(60, 260):
        font = ImageFont.truetype(ttf, size)
        digit_w = max(font.getlength(d) for d in "0123456789")
        width = 4 * digit_w + font.getlength(":")
        x0, y0, x1, y1 = font.getbbox("0", anchor="ls")
        if width > CLOCK_MAX_W or (y1 - y0) > CLOCK_MAX_DIGIT_H:
            break
        best = (size, width, y1 - y0)
    return best


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=OUT_DIR, help="output directory (default fonts/)")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    csize, cwidth, cheight = clock_size()
    print(f"clock: {csize}px font -> 'HH:MM' {cwidth:.0f}px wide, digits {cheight}px tall")

    for name, (ttf, size, chars) in FONTS.items():
        size = size or csize
        font = ImageFont.truetype(ttf_path(ttf), size)
        out = os.path.join(args.out, name + ".vlw")
        nbytes, biggest = write_vlw(out, font, size, chars)
        print(f"{out}: {size}px, {len(set(chars))} glyphs, {nbytes} bytes "
              f"(largest glyph {biggest} bytes)")


if __name__ == "__main__":
    main()
