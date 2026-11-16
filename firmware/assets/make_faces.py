"""Generate animated faces (GIFs) for every assistant character, for the XiaoZhi assets partition.

Characters are original designs: Bông (cloud), Bắp (corn), Bơ (avocado), Mochi (rice cake),
Tí (tiny robot). One GIF per character and emotion, named "<character>_<emotion>.gif".
Rendered at 4x and downsampled for smooth edges.

Usage: python make_faces.py OUT_DIR [--size 200] [--preview preview.png] [--only bong,bap]
"""

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

S = 4  # supersampling factor
FRAMES = 8
FRAME_MS = 140

INK = (74, 58, 92)
WHITE = (255, 255, 255)
CHEEK = (255, 158, 181)
HEART = (255, 92, 138)
TEAR = (104, 186, 255)

# emotion -> (halo color, eyes, mouth, extras)
EMOTIONS = {
    "neutral":     ((214, 236, 255), "dot",      "smile_small", []),
    "happy":       ((255, 226, 196), "happy",    "smile",       ["sparkle"]),
    "laughing":    ((255, 214, 170), "happy",    "open",        ["shake"]),
    "funny":       ((255, 240, 168), "happy",    "grin",        ["shake"]),
    "sad":         ((206, 222, 245), "sad",      "frown",       []),
    "angry":       ((255, 196, 190), "angry",    "frown",       ["anger"]),
    "crying":      ((196, 220, 250), "closed",   "wavy",        ["tears"]),
    "loving":      ((255, 214, 228), "heart",    "smile",       ["hearts"]),
    "embarrassed": ((255, 220, 228), "dot",      "wavy",        ["blush", "sweat"]),
    "surprised":   ((236, 226, 255), "wide",     "o",           []),
    "shocked":     ((230, 230, 240), "wide",     "o_big",       ["sweat"]),
    "thinking":    ((214, 245, 232), "look_up",  "flat",        ["dots"]),
    "winking":     ((255, 232, 200), "wink",     "smile",       ["sparkle"]),
    "cool":        ((200, 240, 255), "shades",   "smirk",       []),
    "relaxed":     ((222, 245, 222), "closed",   "smile_small", []),
    "delicious":   ((255, 236, 200), "happy",    "tongue",      []),
    "kissy":       ((255, 222, 236), "closed",   "kiss",        ["hearts"]),
    "confident":   ((255, 238, 190), "happy",    "smirk",       ["sparkle"]),
    "sleepy":      ((226, 222, 250), "closed",   "o_small",     ["zzz"]),
    "silly":       ((255, 236, 214), "wink",     "tongue",      ["shake"]),
    "confused":    ((236, 232, 214), "swirl",    "wavy",        ["question"]),
    "listening":   ((208, 238, 255), "wide",     "smile_small", ["waves"]),
    "waking":      ((255, 240, 205), "wide",     "o",           ["sparkle"]),
}




def lerp(a, b, t):
    return a + (b - a) * t


class Pen:
    def __init__(self, size):
        self.W = size * S
        self.img = Image.new("RGB", (self.W, self.W), WHITE)
        self.d = ImageDraw.Draw(self.img)

    def u(self, v):  # design units (0..200) -> pixels
        return v * self.W / 200

    def circle(self, x, y, r, fill=None, outline=None, width=0):
        u = self.u
        self.d.ellipse([u(x - r), u(y - r), u(x + r), u(y + r)], fill=fill, outline=outline, width=int(u(width)) if width else 0)

    def ellipse(self, x, y, rx, ry, fill=None):
        u = self.u
        self.d.ellipse([u(x - rx), u(y - ry), u(x + rx), u(y + ry)], fill=fill)

    def arc(self, x, y, rx, ry, start, end, width=3.2, fill=INK):
        u = self.u
        self.d.arc([u(x - rx), u(y - ry), u(x + rx), u(y + ry)], start, end, fill=fill, width=int(u(width)))

    def line(self, pts, width=3.2, fill=INK):
        u = self.u
        self.d.line([(u(a), u(b)) for a, b in pts], fill=fill, width=int(u(width)), joint="curve")
        for a, b in (pts[0], pts[-1]):  # round caps
            self.circle(a, b, width / 2, fill=fill)

    def poly(self, pts, fill):
        u = self.u
        self.d.polygon([(u(a), u(b)) for a, b in pts], fill=fill)

    def heart(self, x, y, s, fill=HEART):
        self.circle(x - s * 0.5, y - s * 0.2, s * 0.55, fill=fill)
        self.circle(x + s * 0.5, y - s * 0.2, s * 0.55, fill=fill)
        self.poly([(x - s * 1.02, y), (x + s * 1.02, y), (x, y + s * 1.15)], fill=fill)

    def text(self, x, y, s, size, fill=INK):
        try:
            font = ImageFont.truetype("DejaVuSans-Bold.ttf", int(self.u(size)))
        except OSError:
            font = ImageFont.load_default()
        self.d.text((self.u(x), self.u(y)), s, font=font, fill=fill, anchor="mm")


CLOUD = [  # (dx, dy, r) puffs relative to body center
    (-44, 10, 34), (44, 10, 34), (-22, -18, 38), (24, -14, 36), (0, 14, 46), (-56, 26, 22), (56, 26, 22),
]


def draw_cloud(p, cx, cy, squash=1.0):
    for dx, dy, r in CLOUD:  # outline pass, then white fill on top
        p.ellipse(cx + dx * squash, cy + dy / squash, r + 3.2, r + 3.2, fill=INK)
    for dx, dy, r in CLOUD:
        p.ellipse(cx + dx * squash, cy + dy / squash, r, r, fill=WHITE)


def draw_eye(p, kind, x, y, side, t, blink):
    if blink and kind in ("dot", "wide", "look_up", "sad", "angry"):
        p.arc(x, y + 1, 7, 4, 200, 340)
        return
    if kind == "dot":
        p.ellipse(x, y, 6.5, 8, fill=INK)
        p.circle(x + 2.2, y - 3, 2.2, fill=WHITE)
    elif kind == "wide":
        p.circle(x, y, 10, fill=WHITE, outline=INK, width=2.6)
        p.circle(x, y + 1, 5.5, fill=INK)
        p.circle(x + 2, y - 1.5, 1.8, fill=WHITE)
    elif kind == "look_up":
        p.ellipse(x, y, 6.5, 8, fill=INK)
        p.circle(x + 2.5, y - 4.5, 2.4, fill=WHITE)
    elif kind == "happy":
        p.arc(x, y + 4, 8, 7, 200, 340, width=3.6)
    elif kind == "closed":
        p.arc(x, y - 3, 8, 6, 20, 160, width=3.4)
    elif kind == "sad":
        p.ellipse(x, y + 1, 6, 7, fill=INK)
        p.circle(x + 2, y - 2, 2, fill=WHITE)
        p.line([(x - 8 * side, y - 13), (x + 6 * side, y - 9)], width=2.8)
    elif kind == "angry":
        p.ellipse(x, y + 1, 6, 7, fill=INK)
        p.line([(x - 9 * side, y - 13), (x + 6 * side, y - 7)], width=3.2)
    elif kind == "heart":
        p.heart(x, y - 1, 7.5 + 1.2 * math.sin(t * 2 * math.pi))
    elif kind == "wink":
        if side < 0:
            p.ellipse(x, y, 6.5, 8, fill=INK)
            p.circle(x + 2.2, y - 3, 2.2, fill=WHITE)
        else:
            p.arc(x, y + 4, 8, 7, 200, 340, width=3.6)
    elif kind == "swirl":
        rot = t * 360 * side
        for i in range(3):
            r = 3 + i * 3
            p.arc(x, y, r, r, rot + i * 40, rot + i * 40 + 250, width=2.4)
    elif kind == "shades":
        pass  # drawn once for both eyes in draw_face


def draw_mouth(p, kind, x, y, t):
    if kind == "smile_small":
        p.arc(x, y - 3, 7, 6, 30, 150, width=3.0)
    elif kind == "smile":
        p.arc(x, y - 6, 11, 9, 25, 155, width=3.2)
    elif kind in ("open", "grin"):
        h = 9 + (2.5 * math.sin(t * 4 * math.pi) if kind == "open" else 0)
        p.d.chord([p.u(x - 12), p.u(y - h), p.u(x + 12), p.u(y + h)], 0, 180, fill=INK)
        p.d.chord([p.u(x - 6), p.u(y + h * 0.25), p.u(x + 6), p.u(y + h * 0.95)], 0, 180, fill=(255, 128, 150))
    elif kind == "frown":
        p.arc(x, y + 6, 9, 7, 205, 335, width=3.2)
    elif kind == "wavy":
        p.line([(x - 10, y), (x - 5, y - 3), (x, y), (x + 5, y - 3), (x + 10, y)], width=2.8)
    elif kind == "o":
        p.ellipse(x, y, 5, 6, fill=INK)
    elif kind == "o_big":
        p.ellipse(x, y + 1, 7, 9 + math.sin(t * 2 * math.pi), fill=INK)
    elif kind == "o_small":
        r = 3 + 1.2 * math.sin(t * 2 * math.pi)
        p.ellipse(x, y, r, r * 1.2, fill=INK)
    elif kind == "flat":
        p.line([(x - 6, y), (x + 6, y - 1)], width=3.0)
    elif kind == "smirk":
        p.arc(x + 3, y - 6, 10, 8, 20, 120, width=3.2)
    elif kind == "tongue":
        p.arc(x, y - 5, 10, 8, 25, 155, width=3.2)
        p.ellipse(x + 2, y + 5, 5, 5 + math.sin(t * 2 * math.pi), fill=(255, 128, 150))
    elif kind == "kiss":
        p.arc(x, y - 3, 4, 3.5, 270, 90, width=2.8)
        p.arc(x, y + 3, 4, 3.5, 270, 90, width=2.8)


def draw_face(p, spec, cx, cy, t, blink):
    _, eyes, mouth, extras = spec
    ex, ey = 20, cy + 4
    for side in (-1, 1):
        draw_eye(p, eyes, cx + side * ex, ey, side, t, blink)
    if eyes == "shades":
        for side in (-1, 1):
            p.d.rounded_rectangle([p.u(cx + side * ex - 12), p.u(ey - 8), p.u(cx + side * ex + 12), p.u(ey + 7)], radius=int(p.u(5)), fill=INK)
            p.circle(cx + side * ex - 5, ey - 3, 2.2, fill=(160, 200, 255))
        p.line([(cx - 8, ey - 4), (cx + 8, ey - 4)], width=2.6)
    blush = 7.5 if "blush" in extras or eyes in ("heart",) else 6
    alpha_cheek = CHEEK if "blush" not in extras else (255, 128, 160)
    for side in (-1, 1):
        p.ellipse(cx + side * 33, ey + 12, blush, blush * 0.62, fill=alpha_cheek)
    draw_mouth(p, mouth, cx, ey + 17, t)


def draw_extras(p, extras, cx, cy, t):
    ph = t * 2 * math.pi
    if "sparkle" in extras:
        for (x, y, k) in ((cx + 62, cy - 42, 0), (cx - 66, cy - 36, 0.5)):
            s = 4 + 3 * abs(math.sin(ph + k * math.pi))
            p.poly([(x, y - s * 2), (x + s * 0.5, y - s * 0.5), (x + s * 2, y), (x + s * 0.5, y + s * 0.5),
                    (x, y + s * 2), (x - s * 0.5, y + s * 0.5), (x - s * 2, y), (x - s * 0.5, y - s * 0.5)], fill=(255, 196, 60))
    if "hearts" in extras:
        for k, x0 in enumerate((cx + 58, cx - 60, cx + 30)):
            tt = (t + k / 3) % 1
            p.heart(x0 + 4 * math.sin(tt * 6), cy - 30 - tt * 40, 5 + 2 * (1 - tt), fill=HEART)
    if "tears" in extras:
        for side in (-1, 1):
            tt = (t + (0.5 if side > 0 else 0)) % 1
            x, y = cx + side * 26, cy + 14 + tt * 34
            p.circle(x, y, 4, fill=TEAR)
            p.poly([(x - 3.6, y - 1), (x + 3.6, y - 1), (x, y - 8)], fill=TEAR)
    if "sweat" in extras:
        x, y = cx + 50, cy - 26 + 3 * math.sin(ph)
        p.circle(x, y, 5, fill=TEAR)
        p.poly([(x - 4.6, y - 1), (x + 4.6, y - 1), (x, y - 11)], fill=TEAR)
    if "anger" in extras:
        x, y, s = cx + 52, cy - 38, 9 + 1.5 * math.sin(ph * 2)
        for a in (45, 135, 225, 315):
            r = math.radians(a)
            p.arc(x + math.cos(r) * s * 0.9, y + math.sin(r) * s * 0.9, s * 0.6, s * 0.6, a + 100, a + 260, width=3, fill=(235, 70, 70))
    if "dots" in extras:
        for i in range(3):
            on = (int(t * 4) % 4) > i
            p.circle(cx + 46 + i * 10, cy - 44 - i * 6, 3.2 + i * 0.8, fill=INK if on else (190, 190, 205))
    if "zzz" in extras:
        for k in range(3):
            tt = (t + k / 3) % 1
            p.text(cx + 44 + tt * 22, cy - 32 - tt * 30, "z", 9 + 6 * tt, fill=(120, 110, 190))
    if "waves" in extras:  # sound arcs on both sides: "I'm listening"
        for side in (-1, 1):
            for k in range(3):
                on = ((int(t * 6) + k) % 3) == 0
                r = 10 + k * 8
                x = cx + side * 76
                start = -40 if side > 0 else 140
                p.arc(x, cy + 6, r, r * 1.3, start, start + 80, width=3.4,
                      fill=(70, 150, 230) if on else (160, 200, 240))
    if "question" in extras:
        p.text(cx + 54, cy - 46 + 3 * math.sin(ph), "?", 22, fill=(150, 120, 60))


def draw_corn(p, cx, cy, squash=1.0):
    leaf, leaf_dark = (126, 196, 96), (88, 160, 70)
    for side in (-1, 1):  # husk leaves peeking out behind the cob
        p.ellipse(cx + side * 40, cy + 40, 27, 47, fill=INK)
        p.ellipse(cx + side * 40, cy + 40, 24, 44, fill=leaf_dark)
    for k in (-10, 0, 10):  # corn silk
        p.line([(cx + k * 0.6, cy - 66), (cx + k, cy - 80)], width=3, fill=(176, 120, 60))
    rx, ry = 47 * squash, 68 / squash
    p.ellipse(cx, cy - 2, rx + 3.2, ry + 3.2, fill=INK)
    p.ellipse(cx, cy - 2, rx, ry, fill=(255, 216, 77))
    for row in range(-3, 4):  # kernels, kept away from the face area
        for col in range(-2, 3):
            x, y = cx + col * 17 + (8 if row % 2 else 0), cy - 2 + row * 17
            in_face = abs(y - (cy + 8)) < 30 and abs(x - cx) < 46
            if in_face or ((x - cx) / (rx - 9)) ** 2 + ((y - cy + 2) / (ry - 9)) ** 2 > 1:
                continue
            p.ellipse(x, y, 5.5, 5.5, fill=(255, 232, 130))
    u = p.u  # front husk cup
    p.d.chord([u(cx - 58), u(cy + 4), u(cx + 58), u(cy + 86)], 0, 180, fill=INK)
    p.d.chord([u(cx - 55), u(cy + 6), u(cx + 55), u(cy + 83)], 0, 180, fill=leaf)
    p.line([(cx, cy + 52), (cx, cy + 80)], width=2.4, fill=leaf_dark)


def draw_avocado(p, cx, cy, squash=1.0):
    skin, flesh, flesh_in = (74, 124, 72), (201, 224, 132), (232, 240, 170)
    parts = [(cx, cy - 34, 36), (cx, cy + 20 / squash, 56 * squash)]
    for grow, color in ((3.2, INK), (0, skin), (-7, flesh), (-13, flesh_in)):
        for x, y, r in parts:
            p.ellipse(x, y, r + grow, r + grow, fill=color)
        # bridge between the two circles so the neck is smooth
        p.poly([(cx - 30 - grow, cy - 22), (cx + 30 + grow, cy - 22), (cx + 44 + grow, cy + 2), (cx - 44 - grow, cy + 2)], fill=color)
    p.circle(cx, cy + 50, 21.5, fill=INK)  # the pit
    p.circle(cx, cy + 50, 19, fill=(150, 98, 60))
    p.circle(cx - 6, cy + 44, 5, fill=(186, 134, 92))


def draw_mochi(p, cx, cy, squash=1.0):
    rx, ry = 66 * squash, 50 / squash
    p.ellipse(cx, cy + 10, rx + 3.2, ry + 3.2, fill=INK)
    p.ellipse(cx, cy + 10, rx, ry, fill=(255, 208, 222))
    p.ellipse(cx - 6, cy - 6, rx * 0.72, ry * 0.5, fill=(255, 226, 236))  # powdered top
    for k in range(5):  # tiny sakura on the head
        a = math.radians(k * 72 - 90)
        p.circle(cx + 26 + 6.5 * math.cos(a), cy - 40 + 6.5 * math.sin(a), 5, fill=(255, 150, 180))
    p.circle(cx + 26, cy - 40, 3, fill=(255, 226, 120))


def draw_robot(p, cx, cy, squash=1.0):
    u = p.u
    body, panel = (206, 228, 246), (236, 246, 255)
    bob = 2 * (squash - 1) * 40
    p.line([(cx, cy - 50), (cx, cy - 70 - bob)], width=4.2)  # antenna
    p.circle(cx, cy - 74 - bob, 8.5, fill=INK)
    p.circle(cx, cy - 74 - bob, 6, fill=(255, 120, 120))
    for side in (-1, 1):  # ear bolts
        p.d.rounded_rectangle([u(cx + side * 66 - 9), u(cy - 8), u(cx + side * 66 + 9), u(cy + 24)], radius=int(u(6)), fill=INK)
        p.d.rounded_rectangle([u(cx + side * 66 - 6), u(cy - 5), u(cx + side * 66 + 6), u(cy + 21)], radius=int(u(4)), fill=(150, 186, 220))
    p.d.rounded_rectangle([u(cx - 62), u(cy - 50), u(cx + 62), u(cy + 60)], radius=int(u(30)), fill=INK)
    p.d.rounded_rectangle([u(cx - 59), u(cy - 47), u(cx + 59), u(cy + 57)], radius=int(u(27)), fill=body)
    p.d.rounded_rectangle([u(cx - 48), u(cy - 30), u(cx + 48), u(cy + 42)], radius=int(u(20)), fill=panel)


# character id -> (body drawer, face vertical offset)
CHARACTERS = {
    "bong": (draw_cloud, 0),
    "bap": (draw_corn, -4),
    "bo": (draw_avocado, -8),
    "mochi": (draw_mochi, 4),
    "ti": (draw_robot, -2),
}


def render(character, name, size):
    body, face_dy = CHARACTERS[character]
    spec = EMOTIONS[name]
    halo, _, _, extras = spec
    frames = []
    for i in range(FRAMES):
        t = i / FRAMES
        p = Pen(size)
        p.circle(100, 100, 96, fill=halo)
        bob = 3 * math.sin(t * 2 * math.pi)
        shake = 3 * math.sin(t * 4 * math.pi) if "shake" in extras else 0
        squash = 1 + 0.025 * math.sin(t * 2 * math.pi)
        cx, cy = 100 + shake, 106 + bob
        body(p, cx, cy, squash)
        draw_face(p, spec, cx, cy + face_dy, t, blink=(i == FRAMES - 2))
        draw_extras(p, extras, cx, cy, t)
        img = p.img.resize((size, size), Image.LANCZOS)
        frames.append(img)
    return frames


def save_gif(frames, path):
    pal = [f.convert("P", palette=Image.ADAPTIVE, colors=64, dither=Image.NONE) for f in frames]
    pal[0].save(path, save_all=True, append_images=pal[1:], duration=FRAME_MS, loop=0, optimize=True, disposal=1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--size", type=int, default=200)
    ap.add_argument("--preview", type=Path)
    ap.add_argument("--only", default="", help="comma-separated character ids")
    a = ap.parse_args()
    a.out_dir.mkdir(parents=True, exist_ok=True)
    chosen = [c for c in CHARACTERS if not a.only or c in a.only.split(",")]
    sheet = []
    for character in chosen:
        for name in EMOTIONS:
            frames = render(character, name, a.size)
            save_gif(frames, a.out_dir / f"{character}_{name}.gif")
            if name in ("relaxed", "happy", "listening", "thinking", "loving", "sleepy", "laughing"):
                sheet.append((f"{character} {name}", frames[0]))
    if a.preview:
        cols, cell = 7, a.size + 24
        rows = math.ceil(len(sheet) / cols)
        im = Image.new("RGB", (cols * cell, rows * cell), WHITE)
        d = ImageDraw.Draw(im)
        for k, (name, fr) in enumerate(sheet):
            x, y = (k % cols) * cell, (k // cols) * cell
            im.paste(fr, (x + 12, y))
            d.text((x + cell // 2, y + a.size + 10), name, fill=INK, anchor="mm")
        im.save(a.preview)
    total = sum(f.stat().st_size for f in a.out_dir.glob("*.gif"))
    print(f"{len(chosen)} characters x {len(EMOTIONS)} emotions, {total / 1024:.0f} KiB -> {a.out_dir}")


if __name__ == "__main__":
    main()
