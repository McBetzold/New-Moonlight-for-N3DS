"""Builds the 3D HOME Menu banner of New Moonlight for N3DS as a glTF scene
(banner.glb), plus the app icon and the flat fallback banner.

    python make_banner.py [--out DIR] [--preview] [--assets DIR]
    python convert.py PATH_TO_PYCGFX banner.glb ../res/banner.cgfx

convert.py turns the scene into CGFX with pycgfx and applies the fixes a
real HOME Menu needs (see its docstring). The logo artwork is drawn by
logo_art.py.

The layout follows banners known to work on the HOME Menu (Pokemon Y's, and
GabeSud0/pokeplatinum-3ds's, found over ~30 hardware tests; build_scene()
has the node tree):
  * The HOME Menu turns every 3D banner one full turn per 10 s about the
    vertical axis. "autorotate" turns the other way at the same rate, so
    the wheel and stars under it stay still; the backdrop, "new" and the
    title are screen-facing billboards whose nodes sit at the origin.
  * Only the area between the HOME Menu's bars is drawn (about y 40 to 212
    of the 240-pixel screen).
  * No material animation (unsafe): the stars twinkle by pulsing in size.
  * No large see-through cards in the middle of the scene (a glow card
    behind the wheel froze the HOME Menu).
  * No two textures of the same pixel size (every banner that froze had a
    pair; check_texture_sizes refuses them), and keep the CGFX near 250 KB.
  * The HOME Menu may keep showing a title's old banner: delete the title
    before installing a new build to see the new banner.

Camera (pycgfx's banner-camera.gltf): position (0, 1, 44.786), looking down
-Z, 30 degree vertical FOV, 5:3, near plane 26.5, so nothing may come closer
than z = 18. Things are placed by their top-screen pixel position and scaled
with their depth, so moving something closer keeps its place on screen.
Larger z = closer to the viewer = further out of the screen in 3D.
"""

import argparse
import io
import json
import math
import os
import random
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import logo_art  # noqa: E402

# the loop: the wheel turns once per loop; the HOME Menu turns the banner
# once per 10 s, so autorotate turns LOOP_SECONDS / 10 times per loop
LOOP_SECONDS = 20.0
# how far each of the two nested wheel nodes turns per loop (they add up)
SPIN_DEG_PER_NODE = 179.9
# parts to leave out (for finding what the HOME Menu chokes on)
OMIT = set()
KEY_STEP = 0.25  # seconds between animation keys (fewer keys = smaller banner)

CAM_POS = (0.0, 1.0, 44.786)
CAM_YFOV = 0.523599
CAM_ASPECT = 5.0 / 3.0

# ---------------------------------------------------------------------------
# small vector / matrix helpers


def v_add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def v_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def v_scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def v_dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def v_norm(a):
    l = math.sqrt(v_dot(a, a)) or 1.0
    return (a[0] / l, a[1] / l, a[2] / l)


def quat_axis(axis, angle):
    s = math.sin(angle / 2)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(angle / 2))


def mat_identity():
    return [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]


def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)]
            for i in range(4)]


def mat_trs(t=(0, 0, 0), q=(0, 0, 0, 1), s=(1, 1, 1)):
    x, y, z, w = q
    r = [
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ]
    return [
        [r[0][0] * s[0], r[0][1] * s[1], r[0][2] * s[2], t[0]],
        [r[1][0] * s[0], r[1][1] * s[1], r[1][2] * s[2], t[1]],
        [r[2][0] * s[0], r[2][1] * s[1], r[2][2] * s[2], t[2]],
        [0, 0, 0, 1],
    ]


def mat_point(m, p):
    return tuple(m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3]
                 for i in range(3))


def mat_dir(m, d):
    return v_norm(tuple(m[i][0] * d[0] + m[i][1] * d[1] + m[i][2] * d[2]
                        for i in range(3)))


# ---------------------------------------------------------------------------
# textures (RGBA4 on the 3DS: quantise to 4 bits per channel with an ordered
# dither here so gradients do not band and the converter's truncation keeps
# exactly these values)

BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def quantize_rgba4(im):
    im = im.convert("RGBA")
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            d = (BAYER4[y & 3][x & 3] + 0.5) / 16.0
            px[x, y] = tuple(
                min(15, int(c / 255.0 * 15 + d)) * 17 for c in px[x, y])
    return im



# The rainbow-road backdrop, drawn in top-screen pixels (at 2x) and then
# mapped onto the backdrop quad. All its settings are here to tweak.
SKY_EDGE, SKY_MID = (7, 11, 31), (27, 20, 70)          # #070B1F, #1B1446
NEBULAE = [  # (centre px, radius px, colour, opacity): wisps near the oval's edge
    ((70, 70), 70, (231, 166, 215), 0.16),     # pink, upper left
    ((345, 70), 60, (184, 166, 232), 0.14),    # lavender, upper right
    ((60, 190), 60, (127, 211, 216), 0.13),    # teal, lower left
    ((340, 195), 70, (231, 166, 215), 0.13),   # pink, lower right
]
# the ribbon: a cubic Bezier in screen px, from behind the wheel, sweeping
# under the title and curving up as it fades into the distance
RIBBON = [(-10, 150), (140, 214), (300, 214), (382, 168)]
RIBBON_WIDTH = (13.0, 2.5)        # px at the start and at the end
RIBBON_OPACITY = 0.0   # no rainbow road in Isaac's latest banner picture
RIBBON_COLOURS = [(226, 52, 52), (244, 140, 40), (246, 214, 52), (90, 190, 80),
                  (60, 182, 196), (60, 120, 220), (140, 80, 200)]
DIM_STARS = 70
TEXT_SHADE = ((292, 128), (100, 52), 0.20)   # darker oval behind the title
BACKDROP_TEX = (128, 64)   # test 7's size (worked on the 3DS)


def _bezier(p, t):
    u = 1 - t
    return tuple(u * u * u * p[0][k] + 3 * u * u * t * p[1][k]
                 + 3 * u * t * t * p[2][k] + t * t * t * p[3][k] for k in range(2))


def draw_backdrop_screen(scale=2):
    """The backdrop as the top screen shows it (400x240 px, drawn at
    `scale` x for smooth edges)."""
    W, H = 400 * scale, 240 * scale
    im = Image.new("RGB", (W, H))
    px = im.load()
    for y in range(H):
        t = min(1.0, abs(y / H - 0.55) * 2.2)
        row = tuple(int(lerp(SKY_MID[i], SKY_EDGE[i], t)) for i in range(3))
        for x in range(W):
            px[x, y] = row
    im = im.convert("RGBA")
    from PIL import ImageDraw, ImageFilter
    for (cx, cy), r, col, op in NEBULAE:
        layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        ImageDraw.Draw(layer).ellipse(
            [(cx - r) * scale, (cy - r * 0.7) * scale,
             (cx + r) * scale, (cy + r * 0.7) * scale],
            fill=col + (int(255 * op),))
        im = Image.alpha_composite(im, layer.filter(ImageFilter.GaussianBlur(r * scale * 0.45)))
    # darker oval behind the title
    (cx, cy), (rx, ry), op = TEXT_SHADE
    shade = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(shade).ellipse([(cx - rx) * scale, (cy - ry) * scale,
                                   (cx + rx) * scale, (cy + ry) * scale],
                                  fill=(0, 0, 8, int(255 * op)))
    im = Image.alpha_composite(im, shade.filter(ImageFilter.GaussianBlur(30 * scale)))
    # the rainbow road: stripes side by side along the curve
    ribbon = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(ribbon)
    steps = 240
    n = len(RIBBON_COLOURS)
    for i in range(steps):
        t0, t1 = i / steps, (i + 1) / steps
        (x0, y0), (x1, y1) = _bezier(RIBBON, t0), _bezier(RIBBON, t1)
        dx, dy = x1 - x0, y1 - y0
        ln = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / ln, dx / ln
        w = lerp(RIBBON_WIDTH[0], RIBBON_WIDTH[1], t0)
        fade = RIBBON_OPACITY * min(1.0, (1.0 - t0) / 0.45)
        for k, col in enumerate(RIBBON_COLOURS):
            off = (k - (n - 1) / 2) * w / n
            pts = [((x0 + nx * off) * scale, (y0 + ny * off) * scale),
                   ((x1 + nx * off) * scale, (y1 + ny * off) * scale)]
            d.line(pts, fill=col + (int(255 * fade),),
                   width=max(1, int(math.ceil(w / n * scale)) + 1))
    im = Image.alpha_composite(im, ribbon.filter(ImageFilter.GaussianBlur(0.6 * scale)))
    # faint far-away stars
    rnd = random.Random(21)
    dots = ImageDraw.Draw(im)
    for _ in range(DIM_STARS):
        x, y = rnd.uniform(0, 400), rnd.uniform(30, 220)
        if 204 < x < 372 and 84 < y < 172:
            continue
        b = rnd.randint(80, 170)
        r = rnd.choice((0.5, 0.5, 0.8)) * scale
        dots.ellipse([x * scale - r, y * scale - r, x * scale + r, y * scale + r],
                     fill=(b, b, min(255, b + 30), 255))
    return im


def make_backdrop():
    """draw_backdrop_screen() mapped onto the backdrop quad (56 x 34 units at
    z = -16, centred on (0, 1)): each texel shows the screen pixel in front
    of it."""
    screen = draw_backdrop_screen(2)
    sp = screen.load()
    tw, th = BACKDROP_TEX
    tex = Image.new("RGBA", (tw, th))
    tp = tex.load()
    z = -16.0
    dist = CAM_POS[2] - z
    tan_y = math.tan(CAM_YFOV / 2)
    for v in range(th):
        wy = 1.0 + 17.0 - (v + 0.5) / th * 34.0
        sy = (1 - (wy - CAM_POS[1]) / (dist * tan_y)) * 120.0
        for u in range(tw):
            wx = -28.0 + (u + 0.5) / tw * 56.0
            sx = (1 + wx / (dist * tan_y * CAM_ASPECT)) * 200.0
            x = min(screen.width - 1, max(0, int(sx * 2)))
            y = min(screen.height - 1, max(0, int(sy * 2)))
            tp[u, v] = sp[x, y]
    return quantize_rgba4(tex)


def make_radial(size, color, max_alpha, power):
    im = Image.new("RGBA", (size, size))
    px = im.load()
    c = (size - 1) / 2.0
    for y in range(size):
        for x in range(size):
            r = math.sqrt((x - c) ** 2 + (y - c) ** 2) / c
            a = max(0.0, 1.0 - r) ** power
            px[x, y] = color + (int(255 * max_alpha * a),)
    return im



def make_sparkle():
    """White four-point sparkle; materials tint it with the logo colours."""
    size = 32
    im = make_radial(size, (255, 255, 255), 1.0, 2.2)
    px = im.load()
    c = size // 2
    for i in range(size):
        f = max(0.0, 1.0 - abs(i - c) / c) ** 2.2
        for (x, y) in ((i, c), (c, i), (i, c - 1), (c - 1, i)):
            r, g, b, a = px[x, y]
            px[x, y] = (r, g, b, min(255, a + int(255 * f)))
    return quantize_rgba4(im)


def lerp(a, b, t):
    return a + (b - a) * t


# ---------------------------------------------------------------------------
# geometry


class Mesh:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.idx = [], [], [], []

    def vert(self, p, n, uv=(0.0, 0.0)):
        self.pos.append(tuple(p))
        self.nrm.append(tuple(n))
        self.uv.append(tuple(uv))
        return len(self.pos) - 1

    def tri(self, a, b, c):
        self.idx += [a, b, c]


def quad_mesh(w, h):
    m = Mesh()
    a = m.vert((-w / 2, -h / 2, 0), (0, 0, 1), (0, 1))
    b = m.vert((w / 2, -h / 2, 0), (0, 0, 1), (1, 1))
    c = m.vert((w / 2, h / 2, 0), (0, 0, 1), (1, 0))
    d = m.vert((-w / 2, h / 2, 0), (0, 0, 1), (0, 0))
    m.tri(a, b, c)
    m.tri(a, c, d)
    return m


def disc_mesh(radius, segments=48):
    """A flat disc facing the viewer, textured edge to edge."""
    m = Mesh()
    c = m.vert((0, 0, 0), (0, 0, 1), (0.5, 0.5))
    rim = []
    for i in range(segments):
        a = 2 * math.pi * i / segments
        x, y = math.cos(a), math.sin(a)
        rim.append(m.vert((radius * x, radius * y, 0), (0, 0, 1),
                          (0.5 + 0.5 * x, 0.5 - 0.5 * y)))
    for i in range(segments):
        m.tri(c, rim[i], rim[(i + 1) % segments])
    return m


# ---------------------------------------------------------------------------
# scene description


def two_pi_t(t):
    return 2 * math.pi * t / LOOP_SECONDS


class Node:
    def __init__(self, name, mesh=None, material=None, t=(0, 0, 0),
                 q=(0, 0, 0, 1), s=(1, 1, 1)):
        self.name, self.mesh, self.material = name, mesh, material
        self.t, self.q, self.s = t, q, s
        self.children = []
        self.anim = {}  # path -> function(time) -> value

    def add(self, child):
        self.children.append(child)
        return child

    def at(self, time):
        t = self.anim["translation"](time) if "translation" in self.anim else self.t
        q = self.anim["rotation"](time) if "rotation" in self.anim else self.q
        s = self.anim["scale"](time) if "scale" in self.anim else self.s
        return mat_trs(t, q, s)


class Material:
    def __init__(self, name, color=(1, 1, 1, 1), texture=None, roughness=1.0,
                 blend=False, color_anim=None, mask=False):
        self.name, self.color, self.texture = name, color, texture
        self.roughness, self.blend, self.color_anim = roughness, blend, color_anim
        self.mask = mask  # alpha test cut-out: drawn with the opaque meshes

    def color_at(self, time):
        return self.color_anim(time) if self.color_anim else self.color




def screen_to_world(sx, sy, z):
    """World x, y at depth z for a point at screen position (sx, sy), both
    -1..1 from the left/bottom edge to the right/top edge."""
    dist = CAM_POS[2] - z
    tan_y = math.tan(CAM_YFOV / 2)
    return (sx * dist * tan_y * CAM_ASPECT, CAM_POS[1] + sy * dist * tan_y)


def world_per_pixel(z):
    """World units per top-screen pixel (240 px tall) at depth z."""
    return 2 * (CAM_POS[2] - z) * math.tan(CAM_YFOV / 2) / 240.0


def px_to_world(px, py, z):
    return screen_to_world(px / 200.0 - 1, 1 - py / 120.0, z)


# Where things sit on the top screen (400x240 px) and how close they are.
# Larger z = closer to the viewer = further out of the screen in 3D. The
# HOME Menu only draws between its bars, about y 40 to 212: keep inside.
# The HOME Menu shows the banner inside an oval (measured from a photo:
# centre (200, 128), half-axes about 195 x 102 px) and only between its bars
# (y 40 to 212). Everything is laid out inside VISIBLE_OVAL with a margin.
VISIBLE_OVAL = ((200, 128), (189, 96))   # 6 px inside the measured oval
WHEEL_PX, WHEEL_R_PX, WHEEL_Z = (110, 128), 72, 10.0
BADGE_Z = 11.0
TITLE_Z = 9.0
# the title card: left edge, centre height and how wide "Moonlight" is
TITLE_LEFT, TITLE_CY, TITLE_WIDTH = 210, 128, 160
VISIBLE_Y = (44, 208)
KEEP_CLEAR = [  # star-free areas: the logo and the title
    ("circle", WHEEL_PX, WHEEL_R_PX + 10),
    ("rect", (204, 80, 372, 176)),
]


def in_oval(px, py, pad=0.0):
    (cx, cy), (ax, ay) = VISIBLE_OVAL
    return ((px - cx) / (ax - pad)) ** 2 + ((py - cy) / (ay - pad)) ** 2 <= 1.0


# the HOME Menu's own spin: one turn per this many seconds (= the loop)
AUTOROTATE_SECONDS = 10.0
# parts drawn as screen-facing billboards by convert.py (their nodes sit at
# the origin; the geometry itself is moved into place)
BILLBOARDS = ("backdrop", "badge", "title")


def offset_mesh(mesh, offset):
    """Moves a mesh's vertices (billboard nodes stay at the origin)."""
    mesh.pos = [v_add(p, offset) for p in mesh.pos]
    return mesh


def build_scene():
    """Laid out like the banners known to work on the HOME Menu (Pokemon Y's,
    and pokeplatinum-3ds's from ~30 hardware tests):
      COMMON (root)
        backdrop      screen billboard, far behind
        autorotate    turns against the HOME Menu's spin (convert.py writes
                      its exact curve), so everything under it stays still
          WheelPosition > wheelspin   the wheel, turning in place
          StarN > TwinkleN            stars, pulsing in size (no material
                                      animation: not safe on the HOME Menu)
        badge         screen billboard ("new" and its dashes)
        title         screen billboard
    """
    mats = {}

    def mat(*a, **k):
        m = Material(*a, **k)
        mats[m.name] = m
        return m

    root = Node("COMMON")

    # backdrop: billboard at the origin, geometry far behind
    bd = mat("backdrop", texture=make_backdrop(), roughness=1.0)
    root.add(Node("backdrop", offset_mesh(quad_mesh(56, 34), (0, 1.0, -16)), bd))

    autorotate = root.add(Node("autorotate"))
    autorotate.anim["rotation"] = lambda tm: (0.0, 0.0, 0.0, 1.0)  # convert.py

    # the wheel: Isaac's artwork (gaps between the spokes cut out with an
    # alpha test), turning in place; convert.py writes the full-turn curve
    k = world_per_pixel(WHEEL_Z)
    wx, wy = px_to_world(WHEEL_PX[0], WHEEL_PX[1], WHEEL_Z)
    wheel_mat = mat("wheel", texture=quantize_rgba4(logo_art.wheel_art(128)),
                    mask=True)
    disc = disc_mesh(WHEEL_R_PX * k)
    # logo_art frames the ring edge at 0.49 of the image: map the disc rim there
    disc.uv = [(0.5 + (u - 0.5) * 0.98, 0.5 + (v - 0.5) * 0.98) for u, v in disc.uv]
    wheel_pos = autorotate.add(Node("WheelPosition", t=(wx, wy, WHEEL_Z)))
    spin = wheel_pos.add(Node("wheelspin", disc, wheel_mat))
    spin.anim["rotation"] = lambda tm: quat_axis(
        (0, 0, 1), -2 * math.pi * tm / LOOP_SECONDS)  # preview; convert.py

    # stars: twinkle by pulsing in size
    star_mat = mat("star", texture=make_sparkle(), blend=True)

    def clear(px, py, pad):
        for kind, a, *rest in KEEP_CLEAR:
            if kind == "circle":
                if math.hypot(px - a[0], py - a[1]) < rest[0] + pad:
                    return False
            elif a[0] - pad < px < a[2] + pad and a[1] - pad < py < a[3] + pad:
                return False
        return True

    rnd = random.Random(5)
    placed = []
    stars = []
    for count, (z0, z1), (s0, s1) in [(5, (-12, -6), (8, 11)),
                                      (4, (-4, 3), (10, 14)),
                                      (5, (5, 15), (13, 19))]:
        n = tries = 0
        while n < count and tries < 3000:
            tries += 1
            px = rnd.uniform(8, 392)
            py = rnd.uniform(VISIBLE_Y[0] + 6, VISIBLE_Y[1] - 6)
            size_px = rnd.uniform(s0, s1)
            if not clear(px, py, size_px / 2) or not in_oval(px, py, size_px):
                continue
            if any(math.hypot(px - qx, py - qy) < 26 for qx, qy in placed):
                continue
            z = rnd.uniform(z0, z1)
            x, y = px_to_world(px, py, z)
            stars.append((z, x, y, size_px * world_per_pixel(z)))
            placed.append((px, py))
            n += 1
    stars.sort()  # back to front: translucent meshes draw in node order
    for i, (z, x, y, size) in enumerate(stars):
        pos = autorotate.add(Node("Star%02d" % i, t=(x, y, z)))
        tw = pos.add(Node("Twinkle%02d" % i, quad_mesh(size, size), star_mat))
        phase, cycles = (i * 0.37) % 1.0, 2 + (i % 3)

        def scale(tm, phase=phase, cycles=cycles):
            s_ = 0.5 + 0.5 * math.sin(
                2 * math.pi * (cycles * tm / LOOP_SECONDS + phase))
            v = 0.15 + 0.85 * s_ * s_
            return (v, v, v)
        tw.anim["scale"] = scale

    # "new" and its dashes, centred on the wheel; billboards at the origin.
    # Textures as in test 7 (worked): backdrop 128x64, wheel 128x128, star
    # 32x32, badge 256x64, title 256x128 (all sizes different)
    img_px = WHEEL_R_PX / 0.49
    kb = world_per_pixel(BADGE_Z)
    bw_px = 0.88 * img_px
    bx, by = px_to_world(WHEEL_PX[0], WHEEL_PX[1], BADGE_Z)
    badge = mat("badge", texture=quantize_rgba4(logo_art.new_badge(256, 64)),
                blend=True)
    root.add(Node("badge", offset_mesh(quad_mesh(bw_px * kb, bw_px * kb / 4),
                                       (bx, by, BADGE_Z)), badge))
    kt = world_per_pixel(TITLE_Z)
    img, frac = logo_art.rainbow_title_block(256, 128)
    w_px = TITLE_WIDTH / frac            # the card, so "Moonlight" is TITLE_WIDTH
    h_px = w_px / 2
    if not (in_oval(TITLE_LEFT + TITLE_WIDTH, TITLE_CY - h_px * 0.42)
            and in_oval(TITLE_LEFT + TITLE_WIDTH * 0.8, TITLE_CY + h_px * 0.42)):
        sys.exit("the title does not fit inside the visible oval")
    title = mat("title", texture=quantize_rgba4(img), blend=True)
    tx, ty = px_to_world(TITLE_LEFT + w_px / 2, TITLE_CY, TITLE_Z)
    root.add(Node("title", offset_mesh(quad_mesh(w_px * kt, h_px * kt),
                                       (tx, ty, TITLE_Z)), title))

    roots = [root]
    if OMIT:
        drop = {"wheel": ("WheelPosition",), "stars": ("Star",),
                "badge": ("badge",), "title": ("title",),
                "backdrop": ("backdrop",)}
        names = tuple(x for o in OMIT for x in drop[o])

        def prune(n):
            n.children = [c for c in n.children if not c.name.startswith(names)]
            for c in n.children:
                prune(c)
        prune(root)
        used = set()
        stack = [root]
        while stack:
            n = stack.pop()
            if n.material is not None:
                used.add(n.material.name)
            stack.extend(n.children)
        mats = {k_: v for k_, v in mats.items() if k_ in used}
    check_texture_sizes(mats)
    return roots, mats


def check_texture_sizes(mats):
    """Every banner that froze or crashed the HOME Menu had two different
    textures of the same pixel size (two 256x128, or two 32x32); every
    banner that worked had all sizes different. The reason is unknown, so
    keep them different."""
    sizes = {}
    for m in mats.values():
        if m.texture is None:
            continue
        sizes.setdefault(m.texture.size, set()).add(id(m.texture))
    dup = [size for size, texs in sizes.items() if len(texs) > 1]
    if dup:
        sys.exit("textures share a size %s; make them different" % dup)


# ---------------------------------------------------------------------------
# glTF writer


def write_glb(roots, mats, path):
    nodes, meshes, accessors, views, images, textures = [], [], [], [], [], []
    materials, mat_index = [], {}
    buf = bytearray()

    def add_view(data, target=None):
        while len(buf) % 4:
            buf.append(0)
        v = {"buffer": 0, "byteOffset": len(buf), "byteLength": len(data)}
        if target:
            v["target"] = target
        buf.extend(data)
        views.append(v)
        return len(views) - 1

    def add_accessor(values, comps, ctype=5126, target=34962, minmax=False):
        fmt = {5126: "f", 5123: "H"}[ctype]
        flat = [c for v in values for c in (v if isinstance(v, tuple) else (v,))]
        data = struct.pack("<%d%s" % (len(flat), fmt), *flat)
        acc = {"bufferView": add_view(data, target), "componentType": ctype,
               "count": len(values),
               "type": {1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4"}[comps]}
        if minmax:
            acc["min"] = [min(v[i] for v in values) for i in range(comps)]
            acc["max"] = [max(v[i] for v in values) for i in range(comps)]
        accessors.append(acc)
        return len(accessors) - 1

    tex_index = {}
    for name, m in mats.items():
        entry = {"name": name, "pbrMetallicRoughness": {
            "baseColorFactor": list(m.color), "metallicFactor": 0.0,
            "roughnessFactor": m.roughness}}
        if m.texture is not None:
            key = id(m.texture)
            if key not in tex_index:
                png = io.BytesIO()
                m.texture.save(png, "PNG")
                images.append({"name": name + "Tex", "mimeType": "image/png",
                               "bufferView": add_view(png.getvalue())})
                textures.append({"source": len(images) - 1, "sampler": 0})
                tex_index[key] = len(textures) - 1
            entry["pbrMetallicRoughness"]["baseColorTexture"] = {
                "index": tex_index[key]}
        if m.blend:
            entry["alphaMode"] = "BLEND"
        elif m.mask:
            entry["alphaMode"] = "MASK"
            entry["alphaCutoff"] = 0.5
        materials.append(entry)
        mat_index[name] = len(materials) - 1

    animated = []

    def add_node(n):
        entry = {"name": n.name}
        if n.t != (0, 0, 0):
            entry["translation"] = list(n.t)
        if n.q != (0, 0, 0, 1):
            entry["rotation"] = list(n.q)
        if n.mesh is not None:
            mh = n.mesh
            attrs = {"POSITION": add_accessor(mh.pos, 3, minmax=True),
                     "NORMAL": add_accessor(mh.nrm, 3)}
            if n.material.texture is not None:
                attrs["TEXCOORD_0"] = add_accessor(mh.uv, 2)
            ind = add_accessor(mh.idx, 1, 5123, 34963)
            meshes.append({"name": n.name, "primitives": [{
                "attributes": attrs, "indices": ind,
                "material": mat_index[n.material.name]}]})
            entry["mesh"] = len(meshes) - 1
        nodes.append(entry)
        me = len(nodes) - 1
        if n.anim:
            animated.append((me, n))
        kids = [add_node(c) for c in n.children]
        if kids:
            entry["children"] = kids
        return me

    scene_nodes = [add_node(r) for r in roots]

    steps = int(round(LOOP_SECONDS / KEY_STEP))
    times = [i * KEY_STEP for i in range(steps + 1)]
    anim = {"name": "Banner", "channels": [], "samplers": []}
    time_acc = add_accessor(times, 1, target=None, minmax=False)
    accessors[time_acc]["min"] = [times[0]]
    accessors[time_acc]["max"] = [times[-1]]
    for node_id, n in animated:
        for p, fn in n.anim.items():
            vals = [tuple(fn(t)) for t in times]
            out = add_accessor(vals, len(vals[0]), target=None)
            anim["samplers"].append({"input": time_acc, "output": out,
                                     "interpolation": "LINEAR"})
            anim["channels"].append({"sampler": len(anim["samplers"]) - 1,
                                     "target": {"node": node_id, "path": p}})
    for name, m in mats.items():
        if m.color_anim:
            vals = [tuple(m.color_anim(t)) for t in times]
            out = add_accessor(vals, 4, target=None)
            anim["samplers"].append({"input": time_acc, "output": out,
                                     "interpolation": "LINEAR"})
            anim["channels"].append({
                "sampler": len(anim["samplers"]) - 1,
                "target": {"path": "pointer", "extensions": {
                    "KHR_animation_pointer": {"pointer":
                        "/materials/%d/pbrMetallicRoughness/baseColorFactor"
                        % mat_index[name]}}}})

    while len(buf) % 4:
        buf.append(0)
    doc = {
        "asset": {"version": "2.0", "generator": "moonlight-N3DS make_banner.py"},
        "scene": 0, "scenes": [{"nodes": scene_nodes}], "nodes": nodes,
        "meshes": meshes, "materials": materials, "accessors": accessors,
        "bufferViews": views, "buffers": [{"byteLength": len(buf)}],
        "images": images, "textures": textures,
        "samplers": [{"magFilter": 9729, "minFilter": 9729,
                      "wrapS": 33071, "wrapT": 33071}],
        "animations": [anim],
    }
    # declaring the pointer extension alone makes pycgfx build a material
    # animation, which the HOME Menu does not handle safely
    if any(c["target"].get("path") == "pointer" for c in anim["channels"]):
        doc["extensionsUsed"] = ["KHR_animation_pointer"]
    js = json.dumps(doc, separators=(",", ":")).encode()
    while len(js) % 4:
        js += b" "
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(buf)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(buf), 0x004E4942) + bytes(buf))


# ---------------------------------------------------------------------------
# preview: a small software renderer that approximates the HOME Menu's
# lighting (light straight into the screen, no ambient, Phong specular)


def render(roots, time, w=400, h=240):
    tan_y = math.tan(CAM_YFOV / 2)
    tan_x = tan_y * CAM_ASPECT
    color = [[(0.0, 0.0, 0.0)] * w for _ in range(h)]
    depth = [[1e9] * w for _ in range(h)]
    light = (0.0, 0.0, 1.0)

    def project(p):
        v = v_sub(p, CAM_POS)
        zc = -v[2]
        return ((v[0] / (zc * tan_x) + 1) / 2 * w,
                (1 - v[1] / (zc * tan_y)) / 2 * h, zc)

    draws = []

    def walk(n, parent):
        m = mat_mul(parent, n.at(time))
        if n.mesh is not None:
            draws.append((n, m))
        for c in n.children:
            walk(c, m)

    for r in roots:
        walk(r, mat_identity())
    draws.sort(key=lambda d: d[0].material.blend)  # opaque first, stable

    for n, m in draws:
        mat = n.material
        base = mat.color_at(time)
        tex = mat.texture.convert("RGBA").load() if mat.texture else None
        tw, th = (mat.texture.size if mat.texture else (1, 1))
        spec_c = 1 - 0.9 * mat.roughness
        spec_p = 4 * 200 / (1 + mat.roughness * 100)
        mh = n.mesh
        wp = [mat_point(m, p) for p in mh.pos]
        wn = [mat_dir(m, d) for d in mh.nrm]
        sp = [project(p) for p in wp]
        for i in range(0, len(mh.idx), 3):
            ia, ib, ic = mh.idx[i:i + 3]
            (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = sp[ia], sp[ib], sp[ic]
            area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
            if area >= 0:  # back face (counter-clockwise front in glTF)
                continue
            minx, maxx = max(0, int(min(x0, x1, x2))), min(w - 1, int(max(x0, x1, x2)) + 1)
            miny, maxy = max(0, int(min(y0, y1, y2))), min(h - 1, int(max(y0, y1, y2)) + 1)
            n_avg = v_norm(v_add(v_add(wn[ia], wn[ib]), wn[ic]))
            diff = max(0.0, v_dot(n_avg, light))
            spec = spec_c * max(0.0, v_dot(n_avg, light)) ** spec_p
            ua, ub, uc = mh.uv[ia], mh.uv[ib], mh.uv[ic]
            for py in range(miny, maxy + 1):
                for px in range(minx, maxx + 1):
                    cx, cy = px + 0.5, py + 0.5
                    w0 = ((x1 - cx) * (y2 - cy) - (x2 - cx) * (y1 - cy)) / area
                    w1 = ((x2 - cx) * (y0 - cy) - (x0 - cx) * (y2 - cy)) / area
                    w2 = 1 - w0 - w1
                    if w0 < 0 or w1 < 0 or w2 < 0:
                        continue
                    iz = w0 / z0 + w1 / z1 + w2 / z2
                    zz = 1 / iz
                    if zz >= depth[py][px]:
                        continue
                    if tex:
                        u = (w0 * ua[0] / z0 + w1 * ub[0] / z1 + w2 * uc[0] / z2) * zz
                        v = (w0 * ua[1] / z0 + w1 * ub[1] / z1 + w2 * uc[1] / z2) * zz
                        t = tex[min(tw - 1, max(0, int(u * tw))),
                                min(th - 1, max(0, int(v * th)))]
                        texel = (t[0] / 255, t[1] / 255, t[2] / 255, t[3] / 255)
                    else:
                        texel = (1, 1, 1, 1)
                    rgb = tuple(min(1.0, texel[k] * base[k] * diff + spec)
                                for k in range(3))
                    a = texel[3] * base[3] if mat.blend else 1.0
                    if mat.mask and texel[3] * base[3] < 0.5:
                        continue
                    if mat.blend:
                        old = color[py][px]
                        color[py][px] = tuple(old[k] * (1 - a) + rgb[k] * a
                                              for k in range(3))
                    else:
                        color[py][px] = rgb
                        depth[py][px] = zz
    im = Image.new("RGB", (w, h))
    im.putdata([tuple(int(c * 255) for c in px) for row in color for px in row])
    return im


def main():
    global LOOP_SECONDS, SPIN_DEG_PER_NODE
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--preview", action="store_true",
                    help="also render preview frames (slow, pure Python)")
    ap.add_argument("--assets", metavar="DIR",
                    help="also write the app icon (ic_moonlight.png, from "
                         "icon_art.png) and the flat fallback banner "
                         "(banner.png, from banner_art.jpg) to DIR")
    ap.add_argument("--loop", type=float, metavar="SECONDS",
                    help="loop length (default %g)" % LOOP_SECONDS)
    ap.add_argument("--spin-deg", type=float, metavar="DEG",
                    help="turn of each of the two wheel nodes per loop "
                         "(default %g)" % SPIN_DEG_PER_NODE)
    ap.add_argument("--omit", default="", metavar="PARTS",
                    help="comma-separated parts to leave out: wheel, halo, "
                         "stars, badge, title, backdrop")
    args = ap.parse_args()
    OMIT.update(p for p in args.omit.split(",") if p)
    if args.loop:
        LOOP_SECONDS = args.loop
    if args.spin_deg is not None:
        SPIN_DEG_PER_NODE = args.spin_deg
    roots, mats = build_scene()
    glb = os.path.join(args.out, "banner.glb")
    write_glb(roots, mats, glb)
    print("wrote", glb)
    if args.assets:
        # Isaac's icon as is, and his banner picture as the flat banner
        icon = Image.open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "icon_art.png")).convert("RGBA")
        flat = Image.open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "banner_art.jpg")).convert("RGB")
        flat = flat.resize((256, 128), Image.LANCZOS)
        for name, im in (("ic_moonlight.png", icon), ("banner.png", flat)):
            p = os.path.join(args.assets, name)
            im.save(p)
            print("wrote", p)
    if args.preview:
        times = [0.0, LOOP_SECONDS * 0.25, LOOP_SECONDS * 0.5, LOOP_SECONDS * 0.75]
        frames = [render(roots, t) for t in times]
        sheet = Image.new("RGB", (800, 480))
        for i, f in enumerate(frames):
            sheet.paste(f, ((i % 2) * 400, (i // 2) * 240))
        p = os.path.join(args.out, "banner-preview.png")
        sheet.save(p)
        print("wrote", p)


if __name__ == "__main__":
    main()
