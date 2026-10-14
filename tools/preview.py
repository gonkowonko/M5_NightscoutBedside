#!/usr/bin/env python3
"""Render a PNG preview of the clock screen from the generated .vlw fonts.

Mirrors the layout constants in src/display.cpp and reads the VLW files the
same way M5GFX does, so it doubles as a check of tools/make_fonts.py output.

    tools/.venv/bin/python tools/preview.py      # writes docs/preview.png
"""

import math
import os
import struct

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = os.path.join(ROOT, "SD", "fonts")

W, H = 320, 240
PAD_X, PAD_TOP, PAD_BOTTOM = 14, 10, 12
CONTENT_W = W - 2 * PAD_X
TOP_Y, TOP_H = PAD_TOP, 18
STRIP_H = 62
STRIP_Y = H - PAD_BOTTOM - STRIP_H
DIVIDER_Y = STRIP_Y - 1
CLOCK_Y = TOP_Y + TOP_H + 8
CLOCK_H = DIVIDER_Y - 1 - CLOCK_Y
BG_CY, DELTA_CY, AGE_CY = STRIP_H // 2, 22, 45
ARROW_GAP, ARROW_BOX, DOUBLE_GAP = 12, 34, 20

CLOCK, DIM, DIVIDER, GREY = "#F3F4F6", "#9CA3AF", "#262626", "#6B7280"
RED, AMBER, GREEN = "#F87171", "#FBBF24", "#4ADE80"


class Vlw:
    def __init__(self, path):
        data = open(path, "rb").read()
        count, _, size, _, ascent, descent = struct.unpack(">6i", data[:24])
        self.glyphs = {}
        ptr = 24 + count * 28
        max_ascent = abs(ascent)
        for i in range(count):
            code, h, w, adv, dy, dx, _ = struct.unpack(">7i", data[24 + i * 28: 52 + i * 28])
            self.glyphs[chr(code)] = (h, w, adv, dy, dx, data[ptr:ptr + w * h])
            ptr += w * h
            if code > 0x20:
                max_ascent = max(max_ascent, dy)
        self.digit_h = self.glyphs["0"][3]

    def width(self, text):
        return sum(self.glyphs[c][2] for c in text if c in self.glyphs)

    def draw(self, img, text, x, cy, align, color):
        """Baseline-aligned, vertically centred on the digit height (like drawText)."""
        w = self.width(text)
        x -= {"left": 0, "center": w // 2, "right": w}[align]
        baseline = cy + self.digit_h // 2
        col = Image.new("RGB", (1, 1), color).getpixel((0, 0))
        for ch in text:
            h, gw, adv, dy, dx, bmp = self.glyphs[ch]
            if gw and h:
                mask = Image.frombytes("L", (gw, h), bmp)
                img.paste(Image.new("RGB", (gw, h), col), (x + dx, baseline - dy), mask)
            x += adv
        return w


def arrow(draw, cx, cy, angle, color):
    length, head, r = ARROW_BOX - 6, ARROW_BOX * 0.4, 2.5
    a = math.radians(angle)
    hx, hy = cx + math.cos(a) * length / 2, cy + math.sin(a) * length / 2
    tx, ty = cx - math.cos(a) * length / 2, cy - math.sin(a) * length / 2
    segs = [((tx, ty), (hx, hy))]
    for side in (-1, 1):
        b = a + math.pi + side * math.pi / 4
        segs.append(((hx, hy), (hx + math.cos(b) * head, hy + math.sin(b) * head)))
    for p, q in segs:
        draw.line([p, q], fill=color, width=int(r * 2))
        for x, y in (p, q):
            draw.ellipse([x - r, y - r, x + r, y + r], fill=color)


ANGLES = {"DoubleUp": -90, "SingleUp": -90, "FortyFiveUp": -45, "Flat": 0,
          "FortyFiveDown": 45, "SingleDown": 90, "DoubleDown": 90}


def screen(f, date, temp, time, bg=None, delta="", trend=None, color=GREEN, stale=False, age=""):
    img = Image.new("RGB", (W, H), "black")
    d = ImageDraw.Draw(img)
    cy = TOP_Y + TOP_H // 2
    f["text15"].draw(img, date, PAD_X, cy, "left", DIM)
    f["text15"].draw(img, temp, PAD_X + CONTENT_W, cy, "right", DIM)
    f["clock"].draw(img, time, PAD_X + CONTENT_W // 2, CLOCK_Y + CLOCK_H // 2, "center", CLOCK)
    d.line([(PAD_X, DIVIDER_Y), (PAD_X + CONTENT_W - 1, DIVIDER_Y)], fill=DIVIDER)

    y = STRIP_Y
    if bg is None:
        f["bg"].draw(img, "---", PAD_X, y + BG_CY, "left", GREY)
        return img
    color = GREY if stale else color
    w = f["bg"].draw(img, bg, PAD_X, y + BG_CY, "left", color)
    if stale:
        d.rectangle([PAD_X, y + BG_CY - 2, PAD_X + w - 1, y + BG_CY + 1], fill=GREY)
    if trend:
        ax = PAD_X + w + ARROW_GAP + ARROW_BOX / 2
        arrow(d, ax, y + BG_CY, ANGLES[trend], color)
        if trend.startswith("Double"):
            arrow(d, ax + DOUBLE_GAP, y + BG_CY, ANGLES[trend], color)
    delta = delta.replace("-", "−")
    f["delta"].draw(img, delta, PAD_X + CONTENT_W, y + DELTA_CY, "right", color)
    f["text14"].draw(img, age, PAD_X + CONTENT_W, y + AGE_CY, "right", RED if stale else DIM)
    return img


def main():
    f = {n: Vlw(os.path.join(FONTS, n + ".vlw")) for n in ("clock", "bg", "delta", "text15", "text14")}
    shots = [
        screen(f, "Wed 14 Oct", "19°C", "23:47", "5.8", "+0.2", "Flat", GREEN, age="2 min ago"),
        screen(f, "Wed 14 Oct", "19°C", "03:12", "10.4", "+0.6", "FortyFiveUp", AMBER, age="just now"),
        screen(f, "Thu 15 Oct", "18°C", "06:58", "3.6", "-0.3", "DoubleDown", RED, age="4 min ago"),
        screen(f, "Thu 15 Oct", "", "07:30", "6.1", "0.0", "SingleDown", GREEN, stale=True, age="23 min ago"),
    ]
    gap = 12
    sheet = Image.new("RGB", (2 * W + gap, 2 * H + gap), "#444")
    for i, s in enumerate(shots):
        sheet.paste(s, ((i % 2) * (W + gap), (i // 2) * (H + gap)))
    os.makedirs(os.path.join(ROOT, "docs"), exist_ok=True)
    out = os.path.join(ROOT, "docs", "preview.png")
    sheet.save(out)
    print(out)
    # Single screen at 2x (nearest-neighbour keeps the real pixels crisp).
    hero = os.path.join(ROOT, "docs", "clock.png")
    shots[0].resize((W * 2, H * 2), Image.NEAREST).save(hero)
    print(hero)


if __name__ == "__main__":
    main()
