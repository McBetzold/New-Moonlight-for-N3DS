"""Builds the 3D HOME Menu banner of New Moonlight for N3DS as a glTF scene
(banner.glb), plus the app icon and the flat fallback banner.

The scene is converted to CGFX with pycgfx (https://github.com/skyfloogle/pycgfx)
and packed into the CIA by bannertool (see README.md next to this file). The
logo artwork itself is drawn by logo_art.py.

    python make_banner.py [--out DIR] [--preview] [--assets DIR]

Scene (glTF units, camera from pycgfx's banner-camera.gltf: position
(0, 1, 44.786), looking down -Z, 30 degree vertical FOV, 5:3, near 26.5,
so nothing may come closer than z = 18):
  * a night-sky backdrop far behind everything (z = -16)
  * 14 stars that twinkle, from deep behind the screen (z = -12) to well in
    front of it (z = 15), kept out of the logo and title areas
  * the wheel out of the screen (z = 10): Isaac's ring (wheel_art.jpg, ring
    only) standing still, with spokes that turn by flipbook
  * the silver "new" and its dashes just in front of it (z = 11)
  * "Moonlight / for N3DS" popping out at z = 9
  Things are placed by their top-screen pixel position and scaled with
  their depth, so moving something closer keeps its place on screen.

Animation notes (what the 3DS HOME Menu actually does):
  * NO NODE (BONE) ANIMATION. On the 3DS any animated node turned the whole
    banner about a vertical axis, so it spun like a turntable and vanished
    while facing away, whatever node the animation was meant for. Nothing in
    the scene moves; everything is placed with static positions.
  * Material colour animation works (KHR_animation_pointer on
    baseColorFactor): the stars twinkle with it, and the spokes turn by
    flipbook with it (SPOKE_FRAMES copies, each turned a little further,
    fading in and out in turn, with their own key times).
  * all animations share one loop, so every periodic motion completes a
    whole number of cycles in LOOP_SECONDS
  * translucent meshes are drawn in node order, so they are sorted back to
    front by depth, and pycgfx moves every part without a BLEND mesh ahead
    of the BLEND ones; a banner that needed that reordering crashed the
    HOME Menu, so check_node_order() refuses such a scene
  * the HOME Menu light shines straight into the screen and there is no
    ambient light: faces turned away from the viewer go dark

TEXTURE SIZES: no two textures may have the same pixel size
(check_texture_sizes). Every banner that froze or crashed the HOME Menu had
a pair of equal-sized textures; every one that worked did not.

BANNER BUDGET: keep banner.cgfx near 250 KB or less. A 458 KB banner (big
textures: a 256x256 wheel plus two 256x128 images) froze the whole 3DS when
the app was selected in the HOME Menu, although pycgfx only warns above
512 KiB; 220-230 KB banners worked. Textures are RGBA4, 2 bytes per pixel,
and dominate the size, so keep them small (the wheel is 128x128, the
backdrop 128x64, the badge 256x64).
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

LOOP_SECONDS = 6.0
# spoke flipbook: frames per 45 degrees, and seconds per 45 degrees
SPOKE_FRAMES = 6
SPOKE_CYCLE = 1.0
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


def quantize_rgba4(im, dither=True):
    im = im.convert("RGBA")
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            d = (BAYER4[y & 3][x & 3] + 0.5) / 16.0 if dither else 0.5
            px[x, y] = tuple(
                min(15, int(c / 255.0 * 15 + d)) * 17 for c in px[x, y])
    return im



def make_backdrop():
    """Night sky: deep navy at the top to dark blue at the bottom, with a
    faint glow behind the wheel."""
    w, h = 64, 32  # a smooth gradient: low resolution is enough
    im = Image.new("RGB", (w, h))
    px = im.load()
    top, bottom = (5, 8, 22), (22, 36, 72)
    for y in range(h):
        t = (y / (h - 1)) ** 1.4
        for x in range(w):
            dx, dy = (x - 0.26 * w) / (0.33 * w), (y - 0.48 * h) / (0.46 * h)
            glow = max(0.0, 1.0 - math.sqrt(dx * dx + dy * dy)) ** 2
            px[x, y] = tuple(int(min(255, top[i] + (bottom[i] - top[i]) * t
                                     + glow * (18, 28, 52)[i])) for i in range(3))
    # no dither: on a texture this small the dots would show as blocks; the
    # GPU's bilinear filtering smooths the 16-level steps instead
    return quantize_rgba4(im, dither=False)


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


def spokes_mesh(radius, angle_deg):
    """Eight spokes as thin quads from the centre to under the ring, turned
    by angle_deg. v runs along a spoke (0 at the centre), u across it."""
    m = Mesh()
    length = radius * (logo_art.SPOKE_END + 0.02)
    half = radius * logo_art.SPOKE_WIDTH / 2
    for i in range(8):
        a = math.radians(angle_deg + 45 * i + 90)
        dx, dy = math.cos(a), math.sin(a)      # along the spoke
        nx, ny = -dy, dx                       # across it
        p = [(nx * half * s_ + dx * l, ny * half * s_ + dy * l, 0)
             for s_, l in ((-1, 0), (1, 0), (1, length), (-1, length))]
        uv = [(0, 0), (1, 0), (1, 1), (0, 1)]
        idx = [m.vert(p[j], (0, 0, 1), uv[j]) for j in range(4)]
        # counter-clockwise as seen from the camera (front facing)
        m.tri(idx[0], idx[2], idx[1])
        m.tri(idx[0], idx[3], idx[2])
    return m


def make_spoke_texture():
    """16x64: across the spoke a grey outline and a white middle; along it
    bright white at the hub to a soft silver-blue at the ring."""
    w, h = 16, 64
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for y in range(h):
        t = y / (h - 1)                         # 0 at the hub, 1 at the ring
        inner = tuple(int(255 + (c - 255) * t) for c in (196, 212, 236))
        for x in range(w):
            edge = abs((x + 0.5) / w - 0.5) * 2  # 0 middle .. 1 edge
            if edge > 0.8:
                c = (120, 124, 132)
            else:
                c = inner
            px[x, y] = c + (255,)
    return quantize_rgba4(im)


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
        self.anim_times = None  # own key times for color_anim (else shared)

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
# Larger z = closer to the viewer = further out of the screen in 3D.
WHEEL_PX, WHEEL_R_PX, WHEEL_Z = (102, 120), 90, 10.0
BADGE_Z = 11.0
TITLE_PX, TITLE_W_PX, TITLE_Z = (300, 120), 190, 9.0
KEEP_CLEAR = [  # sparkle-free areas: the logo and the title
    ("circle", WHEEL_PX, WHEEL_R_PX + 10),
    ("rect", (202, 68, 398, 172)),
]
# star tints: mostly white, some faintly blue or warm
SPARKLE_COLORS = [(1.0, 1.0, 1.0), (0.82, 0.9, 1.0), (1.0, 0.95, 0.82),
                  (1.0, 1.0, 1.0), (0.78, 0.86, 1.0), (1.0, 0.92, 0.88)]


def build_scene():
    roots = []
    mats = {}

    def mat(*a, **k):
        m = Material(*a, **k)
        mats[m.name] = m
        return m

    # backdrop, far behind (opaque)
    bd = mat("Backdrop", texture=make_backdrop(), roughness=1.0)
    roots.append(Node("Backdrop", quad_mesh(56, 34), bd, t=(0, 1.0, -16)))

    # translucent pieces, all sorted back to front below
    blended = []

    # the wheel. Nothing in the banner moves through space: on the 3DS any
    # node (bone) animation turned the WHOLE banner about a vertical axis.
    # The ring is Isaac's artwork (ring only), standing still. The spokes
    # turn by flipbook: SPOKE_FRAMES copies, each turned a little further,
    # fade in and out one after another (material colour animation, which
    # the HOME Menu plays correctly). Eight spokes look the same every 45
    # degrees, so the frames cover 45 degrees and repeat seamlessly.
    k = world_per_pixel(WHEEL_Z)
    wx, wy = px_to_world(WHEEL_PX[0], WHEEL_PX[1], WHEEL_Z)
    radius = WHEEL_R_PX * k
    ring_tex = logo_art.wheel_art(128, ring_only=True)  # small: see BANNER BUDGET
    ring_mat = mat("Ring", texture=quantize_rgba4(ring_tex), mask=True)
    ring = disc_mesh(radius)
    # logo_art frames the ring edge at 0.49 of the image: map the disc rim there
    ring.uv = [(0.5 + (u - 0.5) * 0.98, 0.5 + (v - 0.5) * 0.98) for u, v in ring.uv]
    roots.append(Node("Ring", ring, ring_mat, t=(wx, wy, WHEEL_Z)))

    spoke_tex = make_spoke_texture()
    frame_len = SPOKE_CYCLE / SPOKE_FRAMES
    fade = frame_len / 3
    for f in range(SPOKE_FRAMES):
        start = f * frame_len

        def alpha(tm, start=start):
            # trapezoid: fade in around `start`, out around `start + frame_len`
            x = (tm - start + fade / 2) % SPOKE_CYCLE
            if x < fade:
                return x / fade
            if x < frame_len:
                return 1.0
            if x < frame_len + fade:
                return 1.0 - (x - frame_len) / fade
            return 0.0
        m = mat("Spokes%d" % f, texture=spoke_tex, blend=True,
                color_anim=lambda tm, alpha=alpha: (1.0, 1.0, 1.0, alpha(tm)))
        # keys exactly at the corners of the trapezoid in every cycle
        keys = {0.0, LOOP_SECONDS}
        c = 0.0
        while c < LOOP_SECONDS + SPOKE_CYCLE:
            for off in (-fade / 2, fade / 2, frame_len - fade / 2,
                        frame_len + fade / 2):
                t = c + start + off
                if 0.0 <= t <= LOOP_SECONDS:
                    keys.add(round(t, 6))
            c += SPOKE_CYCLE
        m.anim_times = sorted(keys)
        angle = -45.0 * f / SPOKE_FRAMES  # clockwise
        blended.append((WHEEL_Z - 0.05,
                        Node("Spokes%d" % f, spokes_mesh(radius, angle), m,
                             t=(wx, wy, WHEEL_Z - 0.05))))

    # (no glow behind the wheel: its 32x32 texture matched the star's size,
    # see check_texture_sizes)

    # stars, all twinkling, from deep in the sky to out in front of the screen
    sparkle = make_sparkle()
    twinklers = []
    for i in range(len(SPARKLE_COLORS)):
        color = SPARKLE_COLORS[i]
        phase = i / float(len(SPARKLE_COLORS))
        cycles = 2 + (i % 3)

        # brightness and opacity together, (b, b, b, b), as in the banners
        # that worked on the 3DS
        def anim(tm, phase=phase, cycles=cycles):
            s = 0.5 + 0.5 * math.sin(
                2 * math.pi * (cycles * tm / LOOP_SECONDS + phase))
            b = 0.08 + 0.92 * s * s
            return (b, b, b, b)
        twinklers.append(mat("Star%d" % i, texture=sparkle, blend=True,
                             color_anim=anim))

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
    # (count, depth range, apparent size in px)
    for count, (z0, z1), (s0, s1) in [(5, (-12, -6), (8, 11)),
                                      (4, (-4, 3), (10, 14)),
                                      (5, (5, 15), (13, 19))]:
        n = tries = 0
        while n < count and tries < 3000:
            tries += 1
            px, py = rnd.uniform(8, 392), rnd.uniform(8, 232)
            size_px = rnd.uniform(s0, s1)
            if not clear(px, py, size_px / 2):
                continue
            if any(math.hypot(px - qx, py - qy) < 24 for qx, qy in placed):
                continue
            z = rnd.uniform(z0, z1)
            x, y = px_to_world(px, py, z)
            size = size_px * world_per_pixel(z)
            m = twinklers[len(placed) % len(twinklers)]
            blended.append((z, Node("Sparkle%02d" % len(placed),
                                    quad_mesh(size, size), m, t=(x, y, z))))
            placed.append((px, py))
            n += 1

    # "new" and its dashes, fixed in front of the spinning wheel (same layout
    # as logo_art.logo_mark: 0.88 of the wheel image wide, centre 3% above)
    img_px = WHEEL_R_PX / 0.49
    kb = world_per_pixel(BADGE_Z)
    bw_px = 0.88 * img_px
    bx, by = px_to_world(WHEEL_PX[0], WHEEL_PX[1] - 0.03 * img_px, BADGE_Z)
    badge = mat("Badge", texture=quantize_rgba4(logo_art.new_badge(256, 64)),
                blend=True)
    blended.append((BADGE_Z, Node("Badge", quad_mesh(bw_px * kb, bw_px * kb / 4),
                                  badge, t=(bx, by, BADGE_Z))))

    kt = world_per_pixel(TITLE_Z)
    tx, ty = px_to_world(TITLE_PX[0], TITLE_PX[1], TITLE_Z)
    title = mat("Title", texture=quantize_rgba4(logo_art.title_block(256, 128)),
                blend=True)
    blended.append((TITLE_Z, Node("Title", quad_mesh(TITLE_W_PX * kt,
                                                     TITLE_W_PX * kt / 2),
                                  title, t=(tx, ty, TITLE_Z))))

    # pycgfx draws translucent meshes in node order: back to front
    blended.sort(key=lambda b: b[0])
    roots += [n for _, n in blended]
    if OMIT:
        prefixes = {"wheel": ("Ring", "Spokes"), "halo": "Halo", "stars": "Sparkle",
                    "badge": "Badge", "title": "Title", "backdrop": "Backdrop"}
        drop = tuple(x for o in OMIT for x in (prefixes[o] if isinstance(prefixes[o], tuple) else (prefixes[o],)))
        roots = [n for n in roots if not n.name.startswith(drop)]
        used = set()
        stack = list(roots)
        while stack:
            n = stack.pop()
            if n.material is not None:
                used.add(n.material.name)
            stack.extend(n.children)
        mats = {k: v for k, v in mats.items() if k in used}
    check_node_order(roots)
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


def check_node_order(roots):
    """pycgfx moves every part without a BLEND mesh ahead of the BLEND ones.
    The banner that did need reordering crashed the HOME Menu, so the scene
    must already be in that order."""
    seen_blend = False
    stack = list(reversed(roots))
    while stack:
        n = stack.pop()
        blend = n.mesh is not None and n.material.blend
        if seen_blend and not blend:
            sys.exit("node order: %s (not BLEND) comes after a BLEND mesh" % n.name)
        seen_blend = seen_blend or blend
        stack.extend(reversed(n.children))


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
            m_times, m_acc = times, time_acc
            if m.anim_times:
                m_times = m.anim_times
                m_acc = add_accessor(m_times, 1, target=None)
                accessors[m_acc]["min"] = [m_times[0]]
                accessors[m_acc]["max"] = [m_times[-1]]
            vals = [tuple(m.color_anim(t)) for t in m_times]
            out = add_accessor(vals, 4, target=None)
            anim["samplers"].append({"input": m_acc, "output": out,
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
        "extensionsUsed": ["KHR_animation_pointer"],
        "scene": 0, "scenes": [{"nodes": scene_nodes}], "nodes": nodes,
        "meshes": meshes, "materials": materials, "accessors": accessors,
        "bufferViews": views, "buffers": [{"byteLength": len(buf)}],
        "images": images, "textures": textures,
        "samplers": [{"magFilter": 9729, "minFilter": 9729,
                      "wrapS": 33071, "wrapT": 33071}],
        "animations": [anim],
    }
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
    global LOOP_SECONDS
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--preview", action="store_true",
                    help="also render preview frames (slow, pure Python)")
    ap.add_argument("--assets", metavar="DIR",
                    help="also write the app icon (ic_moonlight.png) and the "
                         "flat fallback banner (banner.png) to DIR")
    ap.add_argument("--loop", type=float, metavar="SECONDS",
                    help="loop length (default %g)" % LOOP_SECONDS)
    ap.add_argument("--omit", default="", metavar="PARTS",
                    help="comma-separated parts to leave out: wheel, halo, "
                         "stars, badge, title, backdrop")
    args = ap.parse_args()
    OMIT.update(p for p in args.omit.split(",") if p)
    if args.loop:
        LOOP_SECONDS = args.loop
    roots, mats = build_scene()
    glb = os.path.join(args.out, "banner.glb")
    write_glb(roots, mats, glb)
    print("wrote", glb)
    if args.assets:
        for name, im in (("ic_moonlight.png", logo_art.make_icon(48)),
                         ("banner.png", logo_art.make_flat_banner())):
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
