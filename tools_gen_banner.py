#!/usr/bin/env python3
"""Repo branding for Cardea: the README banner and the GitHub social preview.

The motif is the thing the app is actually looking at. Down the bottom of both
images runs a burst raster: ragged bars, the way a street looks, and then a
picket of identical bars at a perfectly even spacing -- a rhythm no thumb can
keep. That picket is the whole product in one picture.
"""
from PIL import Image, ImageDraw, ImageFont
import math
import os
import random

OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

INK = (10, 11, 14)
INK_2 = (18, 20, 26)
AMBER = (245, 166, 35)
AMBER_DIM = (138, 96, 26)
ALERT = (232, 78, 52)
PAPER = (238, 240, 244)
MUTED = (138, 146, 160)

AVENIR = "/System/Library/Fonts/Avenir Next.ttc"
MENLO = "/System/Library/Fonts/Menlo.ttc"


# Avenir Next.ttc face order, confirmed by getname(): 0 Bold, 2 Demi Bold,
# 5 Medium, 7 Regular, 8 Heavy. The odd indices are the italics, and picking
# one by accident is exactly the kind of thing a banner ships with.
AVENIR_HEAVY, AVENIR_BOLD, AVENIR_MEDIUM = 8, 0, 5


def avenir(size, face=AVENIR_BOLD):
    return ImageFont.truetype(AVENIR, size, index=face)


def mono(size, bold=False):
    return ImageFont.truetype(MENLO, size, index=1 if bold else 0)


def vertical_wash(w, h):
    """A quiet gradient, so the flat black does not read as a placeholder."""
    img = Image.new("RGB", (w, h), INK)
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(INK[i] + (INK_2[i] - INK[i]) * (t ** 0.7)) for i in range(3))
        d.line((0, y, w, y), c)
    return img


def draw_raster(d, x0, x1, base, height, seed=7):
    """Ragged street traffic, then the picket. Bars are drawn from a fixed seed
    so the banner is reproducible rather than different every run."""
    rng = random.Random(seed)
    bar_w = 4
    gap = 3
    pitch = bar_w + gap

    picket_start = x0 + int((x1 - x0) * 0.62)
    x = x0
    while x < x1:
        if x < picket_start:
            # street: mostly silence, the odd short burst
            if rng.random() < 0.16:
                h = rng.randint(int(height * 0.08), int(height * 0.42))
                col = AMBER_DIM
            else:
                h = 0
                col = AMBER_DIM
            step = pitch
        else:
            # the train: identical bars, evenly spaced
            n = (x - picket_start) // (pitch * 2)
            h = height if ((x - picket_start) // pitch) % 2 == 0 else 0
            col = ALERT
            step = pitch
            del n
        if h:
            d.rectangle((x, base - h, x + bar_w - 1, base), fill=col)
        x += step

    # the evidence threshold, the line a burst must clear to count at all
    ty = base - int(height * 0.30)
    for dx in range(x0, x1, 9):
        d.line((dx, ty, dx + 4, ty), fill=(74, 80, 92), width=2)


def draw_door(d, cx, cy, w, h, stroke):
    """The emblem: a door shut on its hinge, with a key left outside."""
    fx, fy = cx - w // 2, cy - h // 2
    d.rectangle((fx, fy, fx + w, fy + h), outline=AMBER, width=stroke)
    d.rectangle(
        (fx + stroke * 2, fy + stroke * 2, fx + w - stroke * 2, fy + h - stroke * 2),
        outline=AMBER,
        width=max(1, stroke // 2),
    )
    # hinge knuckles on the jamb
    kn = max(6, w // 9)
    for i in range(3):
        y = fy + h // 6 + i * (h // 3)
        d.rectangle((fx - kn, y, fx + stroke, y + kn * 3 // 2), fill=AMBER)
    # handle
    hx = fx + w - stroke * 5
    d.rounded_rectangle(
        (hx - stroke, cy - h // 12, hx + stroke, cy + h // 12), stroke, fill=AMBER
    )
    # the key, kept outside
    r = max(7, w // 10)
    kx = fx - kn - r * 5
    d.ellipse((kx - r, cy - r, kx + r, cy + r), outline=MUTED, width=stroke)
    d.line((kx + r, cy, kx + r * 4, cy), fill=MUTED, width=stroke)
    d.line((kx + r * 3, cy, kx + r * 3, cy + r), fill=MUTED, width=stroke)
    d.line((kx + r * 4, cy, kx + r * 4, cy + r * 2 // 3), fill=MUTED, width=stroke)


def chip(d, x, y, text, font, filled, color=AMBER):
    pad_x, pad_y = 14, 8
    tw = int(d.textlength(text, font=font))
    box = (x, y, x + tw + pad_x * 2, y + font.size + pad_y * 2)
    if filled:
        d.rounded_rectangle(box, 8, fill=color)
        d.text((x + pad_x, y + pad_y), text, font=font, fill=INK)
    else:
        d.rounded_rectangle(box, 8, outline=color, width=2)
        d.text((x + pad_x, y + pad_y), text, font=font, fill=color)
    return box[2] - box[0]


def banner():
    W, H = 1280, 440
    img = vertical_wash(W, H)
    d = ImageDraw.Draw(img)

    f_title = avenir(112, AVENIR_HEAVY)
    f_tag = avenir(30, AVENIR_MEDIUM)
    f_hook = avenir(34, AVENIR_BOLD)
    f_chip = mono(20, bold=True)
    f_foot = mono(19)

    x = 72
    d.text((x, 74), "CARDEA", font=f_title, fill=AMBER)
    d.text((x + 4, 200), "relay attack watch for the Flipper Zero", font=f_tag, fill=MUTED)

    d.text((x + 4, 250), "They never needed your key.", font=f_hook, fill=PAPER)
    d.text((x + 4, 292), "Only its voice.", font=f_hook, fill=ALERT)

    cx = x + 4
    for label, filled in (("UNSOL", True), ("RHYTHM", True), ("CLONE", False)):
        cx += chip(d, cx, 352, label, f_chip, filled) + 12

    draw_door(d, 1050, 190, 190, 250, 6)

    d.text((x + 4, 408), "listen only  -  never transmits", font=f_foot, fill=(96, 102, 116))
    draw_raster(d, 560, W - 72, 396, 66, seed=11)

    path = os.path.join(OUT, "banner.png")
    img.save(path)
    print("wrote", path, img.size)


def social():
    """GitHub social preview. 1280x640, and everything important stays inside
    the middle band, because the card is cropped hard on small screens."""
    W, H = 1280, 640
    img = vertical_wash(W, H)
    d = ImageDraw.Draw(img)

    f_title = avenir(150, AVENIR_HEAVY)
    f_tag = avenir(38, AVENIR_MEDIUM)
    f_hook = avenir(42, AVENIR_BOLD)
    f_chip = mono(24, bold=True)

    title = "CARDEA"
    tw = int(d.textlength(title, font=f_title))
    d.text(((W - tw) // 2, 132), title, font=f_title, fill=AMBER)

    sub = "relay attack watch for the Flipper Zero"
    sw = int(d.textlength(sub, font=f_tag))
    d.text(((W - sw) // 2, 300), sub, font=f_tag, fill=MUTED)

    hook = "A key answering a question nobody asked."
    hw = int(d.textlength(hook, font=f_hook))
    d.text(((W - hw) // 2, 362), hook, font=f_hook, fill=PAPER)

    labels = [("UNSOL", True), ("RHYTHM", True), ("CLONE", False)]
    widths = []
    for label, _ in labels:
        widths.append(int(d.textlength(label, font=f_chip)) + 28)
    total = sum(widths) + 12 * (len(labels) - 1)
    cx = (W - total) // 2
    for (label, filled), w in zip(labels, widths):
        chip(d, cx, 442, label, f_chip, filled)
        cx += w + 12

    draw_raster(d, 96, W - 96, H - 40, 86, seed=11)

    path = os.path.join(OUT, "social-preview.png")
    img.save(path)
    print("wrote", path, img.size)


if __name__ == "__main__":
    banner()
    social()
