#!/usr/bin/env python3
"""Render Cardea's screens at 128x64 and upscale them for the README.

These are mockups, not photographs -- but they are not drawings either. The
layout constants below are copied from views/guard_view.h and the draw code is
a line-for-line port of the C, so a label that runs off the edge here runs off
the edge on the device too. That is the entire point: the last two apps in this
family each shipped one fewer collision because somebody looked at these first.

Font mapping, chosen so the mockup is never narrower than the hardware:

    FontPrimary    helvB08          -> Menlo Bold 10  (6 px/char)
    FontSecondary  haxrcorp4089     -> Menlo 9        (5 px/char, real is ~4)
    FontKeyboard   profont11_mr     -> Menlo 10       (6 px/char, exact)

Anything that fits here fits on the Flipper.
"""
from PIL import Image, ImageDraw, ImageFont
import os

W, H = 128, 64
SCALE = 4
BEZEL = 10

BG = (247, 172, 47)
FG = (36, 26, 12)
CASE = (28, 28, 32)

OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

MENLO = "/System/Library/Fonts/Menlo.ttc"
F_PRIMARY = ImageFont.truetype(MENLO, 10, index=1)
F_SECONDARY = ImageFont.truetype(MENLO, 9, index=0)
F_KEYBOARD = ImageFont.truetype(MENLO, 10, index=0)

# ---------------------------------------------------------------- layout
# Mirrored from views/guard_view.h
G_HDR_BASE = 7
G_RULE_Y = 9
G_RASTER_TOP = 11
G_RASTER_BASE = 32
G_RASTER_H = G_RASTER_BASE - G_RASTER_TOP + 1
G_RASTER_X0 = 3
G_RULE2_Y = 33
G_CHIP_Y = 35
G_CHIP_H = 13
G_CHIP_W = 40
G_CHIP_BASE = G_CHIP_Y + 9
G_TICK_Y = 48
G_BAR_X = 2
G_BAR_W = 124
G_BAR_Y = 50
G_BAR_H = 5
G_VERDICT_BASE = 63
G_PILL_X = 39
G_PILL_W = 30
G_MUTE_X = 72
G_DOTS_X = 85
G_PAGES = 3

# helpers/cdr_detect.h
CDR_RASTER_LEN = 122
CDR_RASTER_MAX_DB = 40
CDR_W_UNSOLICITED, CDR_W_CADENCE, CDR_W_CLONE = 40, 30, 20
CDR_T_ODD, CDR_T_SUSPICIOUS, CDR_T_LIKELY = 14, 32, 60


# ---------------------------------------------------------------- canvas
class Canvas:
    """A deliberately dumb mirror of the Flipper canvas API, so the draw code
    below can be a transliteration of the C rather than a reinterpretation."""

    def __init__(self):
        self.img = Image.new("RGB", (W, H), BG)
        self.d = ImageDraw.Draw(self.img)
        self.font = F_SECONDARY
        self.color = FG

    # --- state ---
    def set_font(self, f):
        self.font = f

    def set_color(self, c):
        self.color = c  # FG, BG or "xor"

    def _ink(self):
        return FG if self.color == FG else BG

    # --- primitives ---
    def draw_dot(self, x, y):
        if 0 <= x < W and 0 <= y < H:
            self.d.point((x, y), self._ink())

    def draw_line(self, x1, y1, x2, y2):
        self.d.line((x1, y1, x2, y2), self._ink())

    def draw_box(self, x, y, w, h):
        self.d.rectangle((x, y, x + w - 1, y + h - 1), fill=self._ink())

    def draw_frame(self, x, y, w, h):
        self.d.rectangle((x, y, x + w - 1, y + h - 1), outline=self._ink())

    def draw_rbox(self, x, y, w, h, r):
        self.d.rounded_rectangle((x, y, x + w - 1, y + h - 1), r, fill=self._ink())

    def draw_rframe(self, x, y, w, h, r):
        self.d.rounded_rectangle((x, y, x + w - 1, y + h - 1), r, outline=self._ink())

    def draw_circle(self, x, y, r):
        self.d.ellipse((x - r, y - r, x + r, y + r), outline=self._ink())

    def draw_disc(self, x, y, r):
        self.d.ellipse((x - r, y - r, x + r, y + r), fill=self._ink())

    # --- text (y is the baseline, as canvas_draw_str) ---
    def string_width(self, s):
        return int(round(self.font.getlength(s)))

    def draw_str(self, x, y, s):
        if self.color == "xor":
            mask = Image.new("1", (W, H), 0)
            ImageDraw.Draw(mask).text((x, y), s, font=self.font, fill=1, anchor="ls")
            inv = Image.new("RGB", (W, H))
            px, ix = self.img.load(), inv.load()
            for yy in range(H):
                for xx in range(W):
                    r, g, b = px[xx, yy]
                    ix[xx, yy] = (255 - r, 255 - g, 255 - b)
            # the amber screen only has two states, so invert means swap
            for yy in range(H):
                for xx in range(W):
                    if mask.getpixel((xx, yy)):
                        px[xx, yy] = BG if px[xx, yy] == FG else FG
            return
        self.d.text((x, y), s, font=self.font, fill=self._ink(), anchor="ls")

    def draw_str_center(self, cx, y, s):
        self.draw_str(cx - self.string_width(s) // 2, y, s)

    def draw_str_right(self, xr, y, s):
        self.draw_str(xr - self.string_width(s), y, s)

    # --- output ---
    def save(self, name, bezel=True):
        img = self.img.resize((W * SCALE, H * SCALE), Image.NEAREST)
        if bezel:
            out = Image.new("RGB", (W * SCALE + BEZEL * 2, H * SCALE + BEZEL * 2), CASE)
            out.paste(img, (BEZEL, BEZEL))
            img = out
        path = os.path.join(OUT, name)
        img.save(path)
        print("wrote", path)
        return img


BANDS = [
    ("315.00", "US / JP"),
    ("390.00", "US / CA"),
    ("433.92", "EU / IN / AU"),
    ("868.35", "EU (newer)"),
    ("915.00", "AU / NZ"),
]
LEVELS = ["QUIET", "ODD TRAFFIC", "SUSPICIOUS", "RELAY LIKELY"]


# ---------------------------------------------------------------- guard
def draw_mute_glyph(c, x, y):
    c.draw_box(x, y + 2, 2, 3)
    for i in range(4):
        c.draw_line(x + 2 + i, y + 3 - i, x + 2 + i, y + 3 + i)
    c.draw_line(x - 1, y + 7, x + 8, y - 1)


def guard_header(c, st):
    if st["camped"]:
        c.draw_box(0, 2, 5, 5)
    else:
        c.draw_frame(0, 2, 5, 5)

    c.set_font(F_SECONDARY)
    c.draw_str(7, G_HDR_BASE, BANDS[st["band"]][0])

    c.set_font(F_KEYBOARD)
    pill = st.get("arm_in") or ("AWAY" if st["armed"] else "HERE")
    if st["armed"]:
        c.draw_rbox(G_PILL_X, 0, G_PILL_W, 9, 2)
        c.set_color(BG)
        c.draw_str_center(G_PILL_X + G_PILL_W // 2, G_HDR_BASE, pill)
        c.set_color(FG)
    else:
        c.draw_rframe(G_PILL_X, 0, G_PILL_W, 9, 2)
        c.draw_str_center(G_PILL_X + G_PILL_W // 2, G_HDR_BASE, pill)

    if st["muted"]:
        draw_mute_glyph(c, G_MUTE_X, 0)

    for i in range(G_PAGES):
        x = G_DOTS_X + i * 4
        if i == st["page"]:
            c.draw_box(x, 4, 3, 3)
        else:
            c.draw_dot(x + 1, 5)

    c.set_font(F_SECONDARY)
    c.draw_str_right(127, G_HDR_BASE, st["clock"])
    c.draw_line(0, G_RULE_Y, 127, G_RULE_Y)


def guard_tag(c, x, text):
    c.set_font(F_KEYBOARD)
    w = c.string_width(text) + 6
    c.set_color(BG)
    c.draw_box(x - 1, G_RASTER_TOP - 1, w + 2, 10)
    c.set_color(FG)
    c.draw_rbox(x, G_RASTER_TOP, w, 9, 2)
    c.set_color(BG)
    c.draw_str(x + 3, G_RASTER_TOP + 7, text)
    c.set_color(FG)


def guard_raster(c, st):
    th = (st["sig_db"] * G_RASTER_H) // CDR_RASTER_MAX_DB
    ty = max(G_RASTER_TOP, G_RASTER_BASE - th)
    for x in range(G_RASTER_X0, 126, 3):
        c.draw_dot(x, ty)

    for i, v in enumerate(st["raster"]):
        if not v:
            continue
        h = max(1, min(G_RASTER_H, (v * G_RASTER_H) // CDR_RASTER_MAX_DB))
        x = G_RASTER_X0 + i
        c.draw_line(x, G_RASTER_BASE, x, G_RASTER_BASE - h + 1)

    c.draw_line(0, G_RULE2_Y, 127, G_RULE2_Y)
    if st.get("beacon"):
        guard_tag(c, G_RASTER_X0, "BEACON")
    if st.get("held"):
        guard_tag(c, 93, "HELD")


def guard_chip(c, x, label, score, ceiling):
    c.draw_rframe(x, G_CHIP_Y, G_CHIP_W, G_CHIP_H, 2)
    if score and ceiling:
        fill = max(3, (score * (G_CHIP_W - 2)) // ceiling)
        c.draw_rbox(x + 1, G_CHIP_Y + 1, fill, G_CHIP_H - 2, 1)
    c.set_font(F_KEYBOARD)
    c.set_color("xor")
    c.draw_str_center(x + G_CHIP_W // 2, G_CHIP_BASE, label)
    c.set_color(FG)


def guard_verdict(c, st):
    guard_chip(c, 2, "UNSOL", st["unsol"], CDR_W_UNSOLICITED)
    guard_chip(c, 44, "RHYTHM", st["cadence"], CDR_W_CADENCE)
    guard_chip(c, 86, "CLONE", st["clone"], CDR_W_CLONE)

    for t in (CDR_T_ODD, CDR_T_SUSPICIOUS, CDR_T_LIKELY):
        x = G_BAR_X + 1 + (t * (G_BAR_W - 2)) // 100
        c.draw_line(x, G_TICK_Y, x, G_TICK_Y + 1)

    c.draw_frame(G_BAR_X, G_BAR_Y, G_BAR_W, G_BAR_H)
    score = st["unsol"] + st["cadence"] + st["clone"]
    if score:
        c.draw_box(G_BAR_X + 1, G_BAR_Y + 1, max(1, (score * (G_BAR_W - 2)) // 100), G_BAR_H - 2)

    c.set_font(F_PRIMARY)
    c.draw_str(2, G_VERDICT_BASE, LEVELS[st["level"]])
    c.draw_str_right(127, G_VERDICT_BASE, str(score))


def guard_detail(c, st):
    c.set_font(F_SECONDARY)
    rows = [
        (18, "Noise floor", st["floor"]),
        (27, "Bursts / win", st["counts"]),
        (36, "Last burst", st["last"]),
        (45, "Poll period", st["period"]),
        (54, "Clone rate", st["clonepct"]),
        (63, "Band held", st["heldrate"]),
    ]
    for y, label, value in rows:
        c.draw_str(2, y, label)
        c.draw_str_right(126, y, value)


def guard_bands(c, st):
    c.set_font(F_KEYBOARD)
    for i, (label, _) in enumerate(BANDS):
        y = 19 + i * 9
        on = st["mask"] & (1 << i)
        if st["band"] == i:
            c.draw_box(0, y - 7, 4, 7)
        if st.get("pinned") == i:
            c.draw_frame(0, y - 7, 4, 7)
        c.draw_str(6, y, label)
        if not on:
            c.draw_line(5, y - 3, 43, y - 3)
            c.draw_str(52, y, "off")
            continue
        c.draw_str(46, y, str(st["floors"][i]))
        c.draw_frame(72, y - 6, 32, 6)
        margin = min(CDR_RASTER_MAX_DB, max(0, st["peaks"][i] - st["floors"][i]))
        w = (margin * 30) // CDR_RASTER_MAX_DB
        if w:
            c.draw_box(73, y - 5, w, 4)
        c.draw_str_right(127, y, str(st["hits"][i]))

    c.set_font(F_SECONDARY)
    c.draw_line(0, 56, 127, 56)
    c.draw_str(2, 63, "Up/Dn pins the receiver")


def alert_banner(c, title, blink_on=True):
    if blink_on:
        c.draw_box(0, 0, 128, 14)
        c.set_color(BG)
    c.set_font(F_PRIMARY)
    c.draw_str(4, 10, title)
    c.set_color(FG)
    if not blink_on:
        c.draw_line(0, 13, 127, 13)


def alert_page_tag(c, page, total=3):
    c.set_color(BG)
    c.draw_box(96, 0, 32, 14)
    c.set_color(FG)
    c.set_font(F_KEYBOARD)
    c.draw_str_right(126, 10, f"{page + 1}/{total}")


# ---------------------------------------------------------------- scenes
def raster_quiet():
    r = [0] * CDR_RASTER_LEN
    for i, v in ((12, 9), (13, 7), (48, 6), (79, 11), (80, 8), (101, 5)):
        r[i] = v
    return r


def raster_attack():
    r = [0] * CDR_RASTER_LEN
    for i, v in ((9, 8), (37, 7), (66, 6)):
        r[i] = v
    # the train: a burst every other column, all the same height
    for i in range(96, 121, 2):
        r[i] = 27
    return r


def screen_guard_quiet():
    c = Canvas()
    st = dict(
        band=2, camped=False, armed=True, muted=False, page=0, clock="01:47",
        arm_in=None, sig_db=12, raster=raster_quiet(),
        unsol=0, cadence=0, clone=0, level=0,
    )
    guard_header(c, st)
    guard_raster(c, st)
    guard_verdict(c, st)
    return c.save("screen_guard_quiet.png")


def screen_guard_alarm():
    c = Canvas()
    st = dict(
        band=2, camped=True, armed=True, muted=False, page=0, clock="02:14",
        arm_in=None, sig_db=12, raster=raster_attack(), held=False,
        unsol=40, cadence=30, clone=20, level=3,
    )
    guard_header(c, st)
    guard_raster(c, st)
    guard_verdict(c, st)
    return c.save("screen_guard_alarm.png")


def screen_guard_beacon():
    c = Canvas()
    r = [0] * CDR_RASTER_LEN
    for i in range(4, 121, 8):
        r[i] = 16
    st = dict(
        band=3, camped=True, armed=True, muted=True, page=0, clock="47:02",
        arm_in=None, sig_db=12, raster=r, beacon=True, held=True,
        unsol=20, cadence=0, clone=0, level=1,
    )
    guard_header(c, st)
    guard_raster(c, st)
    guard_verdict(c, st)
    return c.save("screen_guard_beacon.png")


def screen_detail():
    c = Canvas()
    st = dict(
        band=2, camped=True, armed=True, muted=False, page=1, clock="02:14", arm_in=None,
        floor="-101 dBm", counts="47 / 6", last="41ms +26dB",
        period="218ms +-2%", clonepct="100%", heldrate="no / 998Hz",
    )
    guard_header(c, st)
    guard_detail(c, st)
    return c.save("screen_detail.png")


def screen_bands():
    c = Canvas()
    st = dict(
        band=2, camped=True, armed=True, muted=False, page=2, clock="02:14", arm_in=None,
        mask=0b11101, pinned=5,
        floors=[-104, -99, -101, -97, -103],
        peaks=[-96, -99, -75, -88, -103],
        hits=[2, 0, 41, 3, 0],
    )
    guard_header(c, st)
    guard_bands(c, st)
    return c.save("screen_bands.png")


def screen_alert():
    c = Canvas()
    alert_banner(c, "RELAY LIKELY")
    c.set_font(F_SECONDARY)
    c.draw_str(2, 25, "6 replies on 433.92 MHz")
    c.draw_str(2, 34, "every 218 ms, +-2%")
    c.draw_str(2, 43, "and you were away.")
    c.draw_line(0, 46, 127, 46)
    c.draw_str(2, 54, "Your key is answering")
    c.draw_str(2, 62, "a question nobody asked.")
    alert_page_tag(c, 0)
    return c.save("screen_alert.png")


def screen_alert_why():
    c = Canvas()
    alert_banner(c, "WHY IT FIRED")
    c.set_font(F_SECONDARY)
    names = ["Unsolicited", "Machine rhythm", "Identical frames"]
    got = [40, 30, 20]
    cap = [CDR_W_UNSOLICITED, CDR_W_CADENCE, CDR_W_CLONE]
    for i in range(3):
        y = 24 + i * 10
        c.draw_str(2, y, names[i])
        c.draw_frame(88, y - 6, 38, 6)
        c.draw_box(89, y - 5, max(1, (got[i] * 36) // cap[i]), 4)
    c.draw_line(0, 46, 127, 46)
    c.draw_str(2, 54, "Evidence, not proof --")
    c.draw_str(2, 62, "the reply is not decoded.")
    alert_page_tag(c, 1)
    return c.save("screen_alert_why.png")


def screen_alert_todo():
    c = Canvas()
    alert_banner(c, "WHAT TO DO")
    c.set_font(F_SECONDARY)
    c.draw_str(2, 23, "1 Keys away from doors")
    c.draw_str(2, 31, "  and windows.")
    c.draw_str(2, 40, "2 A metal tin will do.")
    c.draw_str(2, 48, "3 Check the car from")
    c.draw_str(2, 56, "  indoors. Do not go")
    c.draw_str(2, 63, "  out to anyone.")
    alert_page_tag(c, 2)
    return c.save("screen_alert_todo.png")


# ---- splash ----
SP_FX, SP_FY, SP_FW, SP_FH = 49, 2, 30, 44


def screen_splash():
    c = Canvas()
    shut, sealed = 100, True
    c.draw_frame(SP_FX, SP_FY, SP_FW, SP_FH)
    if sealed:
        c.draw_frame(SP_FX - 1, SP_FY - 1, SP_FW + 2, SP_FH + 2)
    for i in range(3):
        c.draw_box(SP_FX - 3, SP_FY + 7 + i * 15, 4, 7)
    c.draw_line(SP_FX - 8, SP_FY + 10, SP_FX - 5, SP_FY + 10)
    c.draw_line(SP_FX - 8, SP_FY + 40, SP_FX - 5, SP_FY + 40)

    pw = 8 + ((SP_FW - 12) * shut) // 100
    flare = 9 - (9 * shut) // 100
    hx, ty, by = SP_FX + 2, SP_FY + 2, SP_FY + SP_FH - 2
    fx = hx + pw
    c.draw_line(hx, ty, fx, ty - flare)
    c.draw_line(hx, by, fx, by + flare)
    c.draw_line(hx, ty, hx, by)
    c.draw_line(fx, ty - flare, fx, by + flare)
    c.draw_line(hx + 4, ty + 5, fx - 4, ty - flare + 5)
    c.draw_line(hx + 4, by - 5, fx - 4, by + flare - 5)
    c.draw_line(hx + 4, ty + 5, hx + 4, by - 5)
    c.draw_line(fx - 4, ty - flare + 5, fx - 4, by + flare - 5)
    c.draw_line(fx - 3, (ty + by) // 2 - 2, fx - 3, (ty + by) // 2 + 2)

    kx = min(28, 8 + (22 * shut) // 100)
    ky = SP_FY + SP_FH // 2
    c.draw_circle(kx, ky, 3)
    c.draw_line(kx + 3, ky, kx + 12, ky)
    c.draw_line(kx + 9, ky, kx + 9, ky + 3)
    c.draw_line(kx + 12, ky, kx + 12, ky + 2)

    c.set_font(F_PRIMARY)
    c.draw_str_center(64, 54, "CARDEA")
    c.set_font(F_SECONDARY)
    c.draw_str_center(64, 63, "relay attack watch")
    return c.save("screen_splash.png")


# ---- menu ----
def screen_menu():
    c = Canvas()
    items = [
        "Guard - watch the car",
        "Learn my key",
        "Last watch report",
        "How relay theft works",
    ]
    c.set_font(F_PRIMARY)
    c.draw_str(4, 12, "Cardea")
    c.set_font(F_SECONDARY)
    for i, it in enumerate(items):
        y = 16 + i * 12
        if i == 0:
            c.draw_rbox(0, y, 128, 12, 3)
            c.set_color(BG)
            c.draw_str(4, y + 9, it)
            c.set_color(FG)
        else:
            c.draw_str(4, y + 9, it)
    return c.save("screen_menu.png")


# ---- learn ----
def screen_learn():
    c = Canvas()
    c.draw_box(0, 0, 128, 13)
    c.set_color(BG)
    c.set_font(F_PRIMARY)
    c.draw_str_center(64, 10, "KEY CAPTURED")
    c.set_color(FG)

    env = [40, 150, 255, 210, 235, 120, 70, 30]
    x0, y0, w, h = 2, 16, 124, 24
    c.draw_frame(x0, y0, w, h)
    inner_w, inner_h, base = w - 2, h - 2, y0 + h - 1
    for i in range(inner_w):
        pos = (i * 7 * 256) // (inner_w - 1)
        b, frac = pos // 256, pos % 256
        if b >= 7:
            b, frac = 6, 256
        v = (env[b] * (256 - frac) + env[b + 1] * frac) // 256
        c.draw_line(x0 + 1 + i, base - 1, x0 + 1 + i, base - max(1, (v * inner_h) // 255))

    c.set_font(F_SECONDARY)
    c.draw_str(2, 50, "433.92  41 ms  -48 dBm")
    c.draw_line(0, 54, 127, 54)
    c.set_font(F_KEYBOARD)
    c.draw_str(2, 63, "OK save")
    c.draw_str(74, 63, "Up retry")
    return c.save("screen_learn.png")


# ---- primer ----
SIN256 = [0, 27, 53, 79, 104, 128, 150, 171, 190, 207, 222, 234, 244, 251, 255, 256]


def isin6(i):
    i = i % 60
    if i <= 15:
        return SIN256[i]
    if i <= 30:
        return SIN256[30 - i]
    if i <= 45:
        return -SIN256[i - 30]
    return -SIN256[60 - i]


def screen_primer():
    c = Canvas()
    c.set_font(F_PRIMARY)
    c.draw_str(2, 9, "Answer travels")
    c.set_font(F_KEYBOARD)
    c.draw_str_right(126, 9, "3/6")
    c.draw_line(0, 11, 127, 11)
    c.draw_line(0, 47, 127, 47)

    # car
    x, y = 2, 41
    c.draw_rframe(x, y - 7, 27, 7, 2)
    c.draw_line(x + 6, y - 7, x + 9, y - 12)
    c.draw_line(x + 9, y - 12, x + 18, y - 12)
    c.draw_line(x + 18, y - 12, x + 21, y - 7)
    c.draw_line(x + 14, y - 12, x + 14, y - 7)
    c.draw_disc(x + 7, y, 2)
    c.draw_disc(x + 20, y, 2)

    # house
    hx = 100
    c.draw_frame(hx, y - 12, 21, 12)
    c.draw_line(hx - 2, y - 12, hx + 10, y - 19)
    c.draw_line(hx + 10, y - 19, hx + 22, y - 12)
    c.draw_frame(hx + 8, y - 6, 5, 6)

    # key inside
    kx, ky = 104, y - 6
    c.draw_circle(kx, ky, 3)
    c.draw_line(kx + 3, ky, kx + 11, ky)

    # the relay link they built, still there, not carrying this
    for dx in range(34, 96, 4):
        c.draw_line(dx, y - 4, dx + 1, y - 4)

    # the reply, travelling on its own
    prev = None
    for xx in range(32, 99):
        yy = 22 + (6 * isin6((xx - 32) * 3)) // 256
        if prev is not None:
            c.draw_line(xx - 1, prev, xx, yy)
        prev = yy
    c.draw_line(32, 22, 36, 19)
    c.draw_line(32, 22, 36, 25)

    c.set_font(F_SECONDARY)
    c.draw_str(2, 55, "Nobody relays the answer.")
    c.draw_str(2, 63, "It reaches the car alone.")
    return c.save("screen_primer.png")


def screen_primer_families():
    c = Canvas()
    c.set_font(F_PRIMARY)
    c.draw_str(2, 9, "Two of three")
    c.set_font(F_KEYBOARD)
    c.draw_str_right(126, 9, "6/6")
    c.draw_line(0, 11, 127, 11)
    c.draw_line(0, 47, 127, 47)

    names = ["UNSOL", "RHYTHM", "CLONE"]
    caps = [CDR_W_UNSOLICITED, CDR_W_CADENCE, CDR_W_CLONE]
    for i in range(3):
        x = 2 + i * 42
        c.draw_rframe(x, 15, 40, 13, 2)
        if i < 2:
            c.draw_rbox(x + 1, 16, 38, 11, 1)
        c.set_font(F_KEYBOARD)
        c.set_color("xor")
        c.draw_str_center(x + 20, 24, names[i])
        c.set_color(FG)

    c.set_font(F_SECONDARY)
    c.draw_str(2, 40, "...one must be RHYTHM.")
    c.draw_str(2, 55, "One burst never counts.")
    c.draw_str(2, 63, "Two families must agree.")
    return c.save("screen_primer_families.png")


# ---- report ----
def screen_report():
    c = Canvas()
    c.draw_box(0, 0, 128, 13)
    c.set_color(BG)
    c.set_font(F_PRIMARY)
    c.draw_str(2, 10, "WATCH REPORT")
    c.set_font(F_KEYBOARD)
    c.draw_str_right(126, 10, "1/2")
    c.set_color(FG)

    c.set_font(F_SECONDARY)
    rows = [
        (22, "Watched for", "07:41:22"),
        (31, "Bursts seen", "312"),
        (40, "Busiest", "433.92 (287)"),
        (49, "Worst", "RELAY LIKELY 90"),
        (58, "  ...at", "03:12:48"),
    ]
    for y, label, value in rows:
        c.draw_str(2, y, label)
        c.draw_str_right(126, y, value)
    return c.save("screen_report.png")


# ---- settings ----
def screen_settings():
    c = Canvas()
    rows = [("315.00 MHz", "On"), ("390.00 MHz", "On"), ("433.92 MHz", "On"), ("868.35 MHz", "On"), ("915.00 MHz", "Off")]
    c.set_font(F_SECONDARY)
    for i, (label, value) in enumerate(rows):
        y = i * 13
        if i == 2:
            c.draw_rbox(0, y, 128, 13, 3)
            c.set_color(BG)
        c.draw_str(4, y + 9, label)
        c.draw_str_right(124, y + 9, f"< {value} >" if i == 2 else value)
        c.set_color(FG)
    return c.save("screen_settings.png")


# ---------------------------------------------------------------- sheet
def contact_sheet(images, cols=3, name="screens.png"):
    pad = 12
    tw = max(i.width for i in images)
    th = max(i.height for i in images)
    rows = (len(images) + cols - 1) // cols
    sheet = Image.new(
        "RGB", (cols * tw + pad * (cols + 1), rows * th + pad * (rows + 1)), (18, 18, 21)
    )
    for n, img in enumerate(images):
        r, col = divmod(n, cols)
        sheet.paste(img, (pad + col * (tw + pad), pad + r * (th + pad)))
    path = os.path.join(OUT, name)
    sheet.save(path)
    print("wrote", path)


if __name__ == "__main__":
    shots = [
        screen_splash(),
        screen_menu(),
        screen_guard_quiet(),
        screen_guard_alarm(),
        screen_alert(),
        screen_alert_why(),
        screen_alert_todo(),
        screen_detail(),
        screen_bands(),
        screen_guard_beacon(),
        screen_learn(),
        screen_primer(),
        screen_primer_families(),
        screen_report(),
        screen_settings(),
    ]
    contact_sheet(shots[:9], cols=3, name="screens.png")
