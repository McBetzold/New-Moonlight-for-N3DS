"""Draws the "New Moonlight for N3DS" logo in pieces with Pillow.

The logo: a wheel (a ring shading through the colours of the rainbow, with
six spokes that fade to white towards the middle), a silver outlined "new"
with three coloured dashes on each side, and the title "Moonlight / for N3DS"
in outlined silver with a red M and a red 3.

The pieces are drawn separately so the banner can spin the wheel behind a
fixed "new". Everything is drawn supersampled and scaled down, with real
transparency around the shapes.
"""

import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

FONT_DIRS = [os.path.join(os.environ.get("WINDIR", "C:\\Windows"), "Fonts"),
             "/usr/share/fonts/truetype/dejavu"]
HEAVY_FONTS = ["seguibl.ttf", "segoeuib.ttf", "arialbd.ttf", "DejaVuSans-Bold.ttf"]

OUTLINE = (78, 78, 82)
SILVER = [(0.0, (252, 252, 252)), (0.42, (214, 214, 218)),
          (0.56, (160, 160, 166)), (1.0, (228, 228, 232))]
RED = [(0.0, (196, 44, 44)), (0.5, (150, 24, 24)), (1.0, (112, 14, 14))]

# ring colours by angle, clockwise from the top (degrees)
RING_STOPS = [(0, (104, 150, 168)), (40, (110, 196, 210)), (75, (176, 222, 226)),
              (100, (240, 206, 84)), (125, (246, 168, 146)), (152, (226, 132, 198)),
              (180, (166, 200, 232)), (212, (80, 168, 222)), (240, (96, 192, 160)),
              (268, (168, 202, 58)), (292, (224, 156, 58)), (318, (220, 64, 64)),
              (344, (150, 116, 140)), (360, (104, 150, 168))]
SPOKE_ANGLES = [0, 45, 135, 180, 225, 315]  # no horizontal spokes: "new" is there
DASHES_LEFT = [(220, 40, 46), (150, 196, 44), (40, 160, 214)]   # top to bottom
DASHES_RIGHT = [(36, 138, 156), (240, 188, 30), (214, 30, 130)]


def find_font(size, names=HEAVY_FONTS):
    for d in FONT_DIRS:
        for n in names:
            p = os.path.join(d, n)
            if os.path.exists(p):
                return ImageFont.truetype(p, size)
    sys.exit("No usable font found (looked for %s)" % ", ".join(names))


def lerp_color(stops, t):
    t = max(stops[0][0], min(stops[-1][0], t))
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        if t <= t1:
            f = (t - t0) / ((t1 - t0) or 1)
            return tuple(int(c0[i] + (c1[i] - c0[i]) * f) for i in range(3))
    return stops[-1][1]


def ring_color(angle_deg):
    return lerp_color(RING_STOPS, angle_deg % 360)


def _gradient_fill(mask, box, stops):
    """An RGBA image the size of mask, coloured by a vertical gradient over
    box (y0, y1) and transparent outside mask."""
    w, h = mask.size
    y0, y1 = box
    grad = Image.new("RGB", (1, h))
    for y in range(h):
        grad.putpixel((0, y), lerp_color(stops, (y - y0) / ((y1 - y0) or 1)))
    out = grad.resize((w, h)).convert("RGBA")
    out.putalpha(mask)
    return out


def metallic_text(text, height_px, red_chars=(), ss=4, outline_frac=0.055):
    """Outlined silver text (with chosen characters in red), cropped tight,
    about height_px tall for capitals."""
    size = int(height_px * ss * 1.32)
    font = find_font(size)
    stroke = max(1, int(size * outline_frac))
    widths = [font.getlength(text[:i]) for i in range(len(text) + 1)]
    asc, desc = font.getmetrics()
    W = int(widths[-1] + 4 * stroke)
    H = asc + desc + 4 * stroke
    ox, oy = 2 * stroke, 2 * stroke
    outline_mask = Image.new("L", (W, H))
    ImageDraw.Draw(outline_mask).text((ox, oy), text, font=font, fill=255,
                                      stroke_width=stroke, stroke_fill=255)
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    edge = Image.new("RGBA", (W, H), OUTLINE + (255,))
    edge.putalpha(outline_mask)
    out = Image.alpha_composite(out, edge)
    # the gradient runs over the capital height so all letters match
    cap = font.getbbox("M")
    band = (oy + cap[1], oy + cap[3])
    for i, ch in enumerate(text):
        if ch == " ":
            continue
        m = Image.new("L", (W, H))
        ImageDraw.Draw(m).text((ox + widths[i], oy), ch, font=font, fill=255)
        out = Image.alpha_composite(
            out, _gradient_fill(m, band, RED if i in red_chars else SILVER))
    out = out.crop(out.getbbox())
    return out.resize((max(1, out.width // ss), max(1, out.height // ss)),
                      Image.LANCZOS)


def wheel(size, ss=4, interior=(255, 255, 255, 255)):
    """The ring and spokes on a white disc; transparent outside the ring."""
    S = size * ss
    c = (S - 1) / 2.0
    R = S * 0.49
    ring_w = S * 0.036
    line = max(1.0, S * 0.005)
    spoke_w = S * 0.045
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    px = im.load()
    spokes = [(math.radians(a), a) for a in SPOKE_ANGLES]
    for y in range(S):
        dy = y - c
        for x in range(S):
            dx = x - c
            r = math.hypot(dx, dy)
            if r > R:
                continue
            ang = (math.degrees(math.atan2(dx, -dy))) % 360
            if r > R - line or (R - ring_w - line < r < R - ring_w):
                px[x, y] = OUTLINE + (255,)
                continue
            if r >= R - ring_w:
                px[x, y] = ring_color(ang) + (255,)
                continue
            col = interior
            # spokes: distance from each spoke line, fading to white inwards
            for rad, deg in spokes:
                sx, sy = math.sin(rad), -math.cos(rad)
                along = dx * sx + dy * sy
                if along <= 0:
                    continue
                across = abs(-dx * sy + dy * sx)
                if across > spoke_w / 2:
                    continue
                t = along / (R - ring_w)            # 0 centre .. 1 ring
                fade = max(0.0, min(1.0, (t - 0.28) / 0.6)) ** 1.2
                base = ring_color(deg)
                lit = tuple(int(255 + (base[i] - 255) * fade) for i in range(3))
                edge = max(0.0, 1 - abs(across / (spoke_w / 2)) ** 6)
                a = 0.5 + 0.5 * edge
                col = tuple(int(col[i] + (lit[i] - col[i]) * a)
                            for i in range(3)) + (255,)
            px[x, y] = col
    return im.resize((size, size), Image.LANCZOS)


def _dash(draw, cx, cy, angle, length, width, color):
    dx, dy = math.cos(angle) * length / 2, math.sin(angle) * length / 2
    draw.line([(cx - dx, cy - dy), (cx + dx, cy + dy)], fill=color + (255,),
              width=int(width))
    r = width / 2
    for (x, y) in ((cx - dx, cy - dy), (cx + dx, cy + dy)):
        draw.ellipse([x - r, y - r, x + r, y + r], fill=color + (255,))


def new_badge(width, height, ss=4):
    """The silver "new" with its six dashes, on transparent, sized to sit
    across a wheel about width / 0.88 wide."""
    W, H = width * ss, height * ss
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    text = metallic_text("new", int(H * 0.36), ss=2, outline_frac=0.06)
    # "new" spans about 62% of the badge, or less if the badge is too short
    # (the banner uses a flatter badge texture to save memory)
    scale = min((W * 0.62) / text.width, (H * 0.92) / text.height)
    text = text.resize((int(text.width * scale), int(text.height * scale)),
                       Image.LANCZOS)
    im.alpha_composite(text, ((W - text.width) // 2, (H - text.height) // 2))
    d = ImageDraw.Draw(im)
    cx, cy = W / 2, H / 2
    length, thick = min(W * 0.065, H * 0.22), min(W * 0.042, H * 0.13)
    rx = W * 0.445
    spread = min(rx * math.sin(math.radians(16)) * 0.9,
                 H / 2 - thick * 0.5 - length * 0.15)
    for side, colors in ((-1, DASHES_LEFT), (1, DASHES_RIGHT)):
        for i, col in enumerate(colors):
            ang = math.radians((i - 1) * 16)          # -16, 0, +16 degrees
            x = cx + side * rx * math.cos(ang)
            y = cy + (i - 1) * spread
            _dash(d, x, y, ang if side > 0 else -ang, length, thick, col)
    return im.resize((width, height), Image.LANCZOS)


# rainbow outline colours, red through magenta, one per letter in turn
RAINBOW = [(222, 62, 52), (238, 128, 40), (236, 192, 40), (140, 196, 60),
           (60, 178, 150), (52, 150, 214), (92, 110, 210), (150, 92, 200),
           (206, 72, 168)]
EDGE_DARK = (24, 22, 40)


def rainbow_text(text, height_px, red_fill=(), ss=4):
    """Silver letters, each outlined in the next rainbow colour (red to
    magenta), with a thin dark edge outside the outline so they read on a
    dark background; letters in red_fill are filled red (the 3 of N3DS)."""
    size = int(height_px * ss * 1.32)
    font = find_font(size)
    colour_w = max(1, int(size * 0.055))
    dark_w = colour_w + max(1, int(size * 0.025))
    widths = [font.getlength(text[:i]) for i in range(len(text) + 1)]
    asc, desc = font.getmetrics()
    W = int(widths[-1] + 4 * dark_w)
    H = asc + desc + 4 * dark_w
    ox, oy = 2 * dark_w, 2 * dark_w
    letters = [i for i, ch in enumerate(text) if ch != " "]
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))

    def layer(i, stroke, colour):
        m = Image.new("L", (W, H))
        ImageDraw.Draw(m).text((ox + widths[i], oy), text[i], font=font,
                               fill=255, stroke_width=stroke, stroke_fill=255)
        c = Image.new("RGBA", (W, H), colour + (255,))
        c.putalpha(m)
        return c

    for i in letters:                       # dark edge under everything
        out = Image.alpha_composite(out, layer(i, dark_w, EDGE_DARK))
    for n, i in enumerate(letters):         # rainbow outlines
        col = RAINBOW[min(len(RAINBOW) - 1, n * len(RAINBOW) // len(letters))]
        if i in red_fill:
            col = (120, 16, 16)
        out = Image.alpha_composite(out, layer(i, colour_w, col))
    cap = font.getbbox("M")
    band = (oy + cap[1], oy + cap[3])
    for i in letters:                       # silver (or red) fill
        m = Image.new("L", (W, H))
        ImageDraw.Draw(m).text((ox + widths[i], oy), text[i], font=font, fill=255)
        out = Image.alpha_composite(
            out, _gradient_fill(m, band, RED if i in red_fill else SILVER))
    out = out.crop(out.getbbox())
    return out.resize((max(1, out.width // ss), max(1, out.height // ss)),
                      Image.LANCZOS)


def text_card(img, width, height, align="left"):
    """img scaled to fit width x height (keeping its shape), on transparent."""
    s = min(width * 0.97 / img.width, height * 0.92 / img.height)
    img = img.resize((max(1, int(img.width * s)), max(1, int(img.height * s))),
                     Image.LANCZOS)
    card = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    x = int(width * 0.015) if align == "left" else (width - img.width) // 2
    card.alpha_composite(img, (x, (height - img.height) // 2))
    return card


def rainbow_title_block(width, height):
    """"Moonlight" over "for N3DS" in rainbow-outlined silver, left aligned,
    both lines at the same scale with "Moonlight" 98% of the width.
    Returns the image and the fraction of its width "Moonlight" fills."""
    line1 = rainbow_text("Moonlight", 96)
    line2 = rainbow_text("for N3DS", 96, red_fill=(5,))
    gap = 0.12 * line1.height
    s = min(width * 0.98 / line1.width,
            height * 0.96 / (line1.height + gap + line2.height))
    line1 = line1.resize((int(line1.width * s), int(line1.height * s)), Image.LANCZOS)
    line2 = line2.resize((int(line2.width * s), int(line2.height * s)), Image.LANCZOS)
    im = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    top = int((height - line1.height - gap * s - line2.height) / 2)
    im.alpha_composite(line1, (0, top))
    im.alpha_composite(line2, (0, int(top + line1.height + gap * s)))
    return im, line1.width / width


def title_block(width, height, ss=2):
    """"Moonlight" over "for N3DS", left aligned, on transparent."""
    W, H = width * ss, height * ss
    line1 = metallic_text("Moonlight", int(H * 0.30), red_chars=(0,), ss=2)
    line2 = metallic_text("for N3DS", int(H * 0.30), red_chars=(5,), ss=2)
    s = min((W * 0.98) / line1.width, (H * 0.47) / line1.height)
    line1 = line1.resize((int(line1.width * s), int(line1.height * s)), Image.LANCZOS)
    line2 = line2.resize((int(line2.width * s), int(line2.height * s)), Image.LANCZOS)
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gap = int(H * 0.06)
    top = (H - line1.height - line2.height - gap) // 2
    im.alpha_composite(line1, (0, top))
    im.alpha_composite(line2, (0, top + line1.height + gap))
    return im.resize((width, height), Image.LANCZOS)


# Isaac's wheel artwork (1024x1024): eight spokes and a rainbow ring on a
# starry background. Measured: centre (512, 512), ring from radius 401 to
# 443.5 (outlines included), spokes 43 px wide, meeting in a light hub.
WHEEL_ART = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "wheel_art.jpg")
_ART_C, _RING_IN, _RING_OUT, _SPOKE_HALF, _HUB = 512.0, 400.5, 443.5, 21.5, 40.0


_wheel_art_cache = {}


def wheel_art(size, ring_only=False):
    """The artwork wheel with everything but the ring and spokes (or only
    the ring) made transparent, framed like wheel() (ring edge at 0.49 of
    the image)."""
    if ring_only not in _wheel_art_cache:
        _wheel_art_cache[ring_only] = _cut_out_wheel(ring_only)
    return _wheel_art_cache[ring_only].resize((size, size), Image.LANCZOS)


# where the spokes end and how wide they are, as fractions of the ring's
# outer radius (for drawing spokes as geometry)
SPOKE_END = _RING_IN / (_RING_OUT + 0.5)
SPOKE_WIDTH = 2 * _SPOKE_HALF / (_RING_OUT + 0.5)


def _cut_out_wheel(ring_only=False):
    src = Image.open(WHEEL_ART).convert("RGB")
    n = src.width
    mask = Image.new("L", (n, n))
    mp = mask.load()
    spokes = [(math.cos(math.radians(a)), math.sin(math.radians(a)))
              for a in range(0, 180, 45)]  # each line covers both directions
    for y in range(n):
        dy = y - _ART_C
        for x in range(n):
            dx = x - _ART_C
            r = math.hypot(dx, dy)
            # coverage with a one-pixel soft edge
            cov = 0.0
            if r <= _RING_OUT + 0.5:
                edge_out = min(1.0, _RING_OUT + 0.5 - r)
                ring = min(edge_out, max(0.0, min(1.0, r - _RING_IN + 0.5)))
                spoke = 0.0
                for (cx, cy) in spokes:
                    across = abs(dx * cy - dy * cx)
                    spoke = max(spoke, min(1.0, max(0.0, _SPOKE_HALF + 0.5 - across)))
                hub = max(0.0, min(1.0, _HUB + 0.5 - r))
                cov = ring if ring_only else max(ring, min(edge_out, max(spoke, hub)))
            mp[x, y] = int(255 * cov)
    im = src.convert("RGBA")
    im.putalpha(mask)
    half = (_RING_OUT + 0.5) / 0.98
    box = tuple(int(round(v)) for v in (_ART_C - half, _ART_C - half,
                                        _ART_C + half, _ART_C + half))
    return im.crop(box)


def logo_mark(size):
    """Wheel with the "new" badge on top (the icon artwork)."""
    im = wheel_art(size)
    bw = int(size * 0.88)
    bh = int(bw / 2)
    badge = new_badge(bw, bh)
    im.alpha_composite(badge, ((size - bw) // 2, int(size * 0.47 - bh / 2)))
    return im


def make_icon(size=48):
    """48x48 HOME Menu icon: the logo mark on a soft white tile."""
    big = size * 8
    tile = Image.new("RGBA", (big, big))
    px = tile.load()
    for y in range(big):
        v = int(255 - 22 * (y / big) ** 1.5)
        for x in range(big):
            px[x, y] = (v, v, min(255, v + 4), 255)
    mark = logo_mark(int(big * 0.94))
    tile.alpha_composite(mark, ((big - mark.width) // 2, (big - mark.height) // 2))
    return tile.resize((size, size), Image.LANCZOS).convert("RGB")


def make_flat_banner(w=256, h=128):
    """The old-style flat banner (used if BANNER_IMAGE is set to a .png)."""
    im = Image.new("RGBA", (w, h), (246, 248, 251, 255))
    mark = logo_mark(int(h * 0.92))
    im.alpha_composite(mark, (int(h * 0.04), (h - mark.height) // 2))
    tb = title_block(int(w - h - 4), int(h * 0.62))
    im.alpha_composite(tb, (h + 2, (h - tb.height) // 2))
    return im.convert("RGB")
