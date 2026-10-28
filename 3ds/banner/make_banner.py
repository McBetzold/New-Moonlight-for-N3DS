"""Builds Moonlight's 3D HOME Menu banner as a glTF scene (banner.glb).

The scene is converted to CGFX with pycgfx (https://github.com/skyfloogle/pycgfx)
and packed into the CIA by bannertool (see README.md next to this file).

    python make_banner.py [--out DIR] [--preview]

Scene (glTF units, camera from pycgfx's banner-camera.gltf: position
(0, 1, 44.786), looking down -Z, 30 degree vertical FOV, 5:3, near 26.5):
  * a night-sky backdrop far behind everything (z = -16)
  * distant stars, a few twinkling, plus some closer ones for 3D depth
  * the Moonlight logo: eight separate 3D wedges with real gaps, turning
    like a wheel while it bobs and sways (z = 4, closest to the viewer)
  * a soft glow behind the logo
  * "Moonlight" / "Game Streaming" floating at z = 2

Animation notes (pycgfx limitations):
  * every rotation is stored as Euler angles, and the middle (Y) angle is
    limited to +-90 degrees, so each animated node turns around one axis
    only, nested: bob (translation) > sway (Y) > tilt (X) > spin (Z)
  * the logo looks the same after every 45 degrees, so spinning exactly 90
    degrees per loop repeats seamlessly with no angle wrap
  * all animations share one loop, so every periodic motion completes a
    whole number of cycles in LOOP_SECONDS
  * the HOME Menu light shines straight into the screen and there is no
    ambient light: faces turned away from the viewer go dark
"""

import argparse
import io
import json
import math
import os
import random
import struct
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

LOOP_SECONDS = 6.0
KEY_STEP = 0.125  # seconds between animation keys

CAM_POS = (0.0, 1.0, 44.786)
CAM_YFOV = 0.523599
CAM_ASPECT = 5.0 / 3.0

FONT_DIRS = [os.path.join(os.environ.get("WINDIR", "C:\\Windows"), "Fonts"),
             "/usr/share/fonts/truetype/dejavu"]
TITLE_FONTS = ["seguisb.ttf", "segoeuib.ttf", "DejaVuSans-Bold.ttf"]
SUB_FONTS = ["segoeui.ttf", "DejaVuSans.ttf"]

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


def find_font(names, size):
    for d in FONT_DIRS:
        for n in names:
            p = os.path.join(d, n)
            if os.path.exists(p):
                return ImageFont.truetype(p, size)
    sys.exit("No usable font found (looked for %s)" % ", ".join(names))


def lerp(a, b, t):
    return a + (b - a) * t


def make_backdrop():
    w, h = 256, 128
    im = Image.new("RGB", (w, h))
    px = im.load()
    top = (6, 9, 24)
    bottom = (24, 40, 78)
    rnd = random.Random(7)
    for y in range(h):
        t = (y / (h - 1)) ** 1.4
        for x in range(w):
            # soft bluish glow behind the logo (left third, a bit above centre)
            dx, dy = (x - 70) / 90.0, (y - 58) / 60.0
            glow = max(0.0, 1.0 - math.sqrt(dx * dx + dy * dy)) ** 2
            c = [lerp(top[i], bottom[i], t) + glow * (20, 34, 60)[i]
                 for i in range(3)]
            px[x, y] = tuple(int(min(255, v)) for v in c)
    # faint far-away stars baked into the sky
    for _ in range(70):
        x, y = rnd.randrange(w), rnd.randrange(h - 20)
        b = rnd.randrange(90, 200)
        px[x, y] = (b, b, min(255, b + 30))
    return quantize_rgba4(im)


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


def make_star():
    size = 32
    im = make_radial(size, (235, 240, 255), 1.0, 2.6)
    px = im.load()
    c = size // 2
    # four-point glint
    for i in range(size):
        f = max(0.0, 1.0 - abs(i - c) / c) ** 3
        for (x, y) in ((i, c), (c, i), (i, c - 1), (c - 1, i)):
            r, g, b, a = px[x, y]
            px[x, y] = (r, g, b, min(255, a + int(200 * f)))
    return quantize_rgba4(im)


def make_text(text, w, h, font_names, color, shadow=True):
    s = 4  # supersample
    big = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
    size = h * s
    while True:
        font = find_font(font_names, size)
        box = font.getbbox(text)
        if box[2] - box[0] <= w * s * 0.94 and box[3] - box[1] <= h * s * 0.80:
            break
        size -= 4
    tw, th = box[2] - box[0], box[3] - box[1]
    pos = ((w * s - tw) // 2 - box[0], (h * s - th) // 2 - box[1])
    if shadow:
        sh = Image.new("RGBA", big.size, (0, 0, 0, 0))
        ImageDraw.Draw(sh).text((pos[0] + s * 2, pos[1] + s * 2), text,
                                font=font, fill=(0, 0, 0, 170))
        big = Image.alpha_composite(big, sh.filter(ImageFilter.GaussianBlur(s * 2)))
    ImageDraw.Draw(big).text(pos, text, font=font, fill=color + (255,))
    return quantize_rgba4(big.resize((w, h), Image.LANCZOS))


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


def logo_mesh(radius, gap, depth, arc_steps=8):
    """Eight wedges of a disc, separated by straight gaps of width `gap`."""
    m = Mesh()
    zf, zb = depth / 2, -depth / 2
    half = math.pi / 8  # half of a 45 degree wedge
    apex_dist = (gap / 2) / math.sin(half)
    edge = math.asin((gap / 2) / radius)
    for k in range(8):
        mid = k * 2 * half + half
        apex = (apex_dist * math.cos(mid), apex_dist * math.sin(mid))
        a0, a1 = mid - half + edge, mid + half - edge
        arc = [(radius * math.cos(a0 + (a1 - a0) * i / arc_steps),
                radius * math.sin(a0 + (a1 - a0) * i / arc_steps))
               for i in range(arc_steps + 1)]
        # front and back faces (fans from the apex)
        fa = m.vert((apex[0], apex[1], zf), (0, 0, 1))
        fr = [m.vert((x, y, zf), (0, 0, 1)) for x, y in arc]
        for i in range(arc_steps):
            m.tri(fa, fr[i], fr[i + 1])
        ba = m.vert((apex[0], apex[1], zb), (0, 0, -1))
        br = [m.vert((x, y, zb), (0, 0, -1)) for x, y in arc]
        for i in range(arc_steps):
            m.tri(ba, br[i + 1], br[i])
        # curved outer wall, smooth normals
        for i in range(arc_steps):
            (x0, y0), (x1, y1) = arc[i], arc[i + 1]
            n0 = (x0 / radius, y0 / radius, 0)
            n1 = (x1 / radius, y1 / radius, 0)
            p = m.vert((x0, y0, zf), n0)
            q = m.vert((x1, y1, zf), n1)
            r = m.vert((x1, y1, zb), n1)
            s = m.vert((x0, y0, zb), n0)
            m.tri(p, s, r)
            m.tri(p, r, q)
        # the two straight walls along the gaps
        for (ex, ey) in (arc[0], arc[-1]):
            pts = [(apex[0], apex[1]), (ex, ey)]
            if (ex, ey) == arc[-1]:
                pts.reverse()
            (x0, y0), (x1, y1) = pts
            # the edge direction turned 90 degrees clockwise points out of
            # the wedge, into the gap
            n = v_norm((y1 - y0, x0 - x1, 0))
            p = m.vert((x0, y0, zf), n)
            q = m.vert((x1, y1, zf), n)
            r = m.vert((x1, y1, zb), n)
            s = m.vert((x0, y0, zb), n)
            m.tri(p, r, q)
            m.tri(p, s, r)
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
                 blend=False, color_anim=None):
        self.name, self.color, self.texture = name, color, texture
        self.roughness, self.blend, self.color_anim = roughness, blend, color_anim

    def color_at(self, time):
        return self.color_anim(time) if self.color_anim else self.color


def build_scene():
    roots = []
    mats = {}

    def mat(*a, **k):
        m = Material(*a, **k)
        mats[m.name] = m
        return m

    # backdrop, far behind (opaque)
    bd = mat("Sky", texture=make_backdrop(), roughness=1.0)
    roots.append(Node("Sky", quad_mesh(56, 34), bd, t=(0, 1.0, -16)))

    # the logo (opaque): bob > sway(Y) > tilt(X) > spin(Z) + mesh
    logo_mat = mat("Logo", color=(0.95, 0.96, 1.0, 1.0), roughness=0.45)
    centre = (-9.4, 1.4, 4.0)
    bob = Node("LogoBob", t=centre)
    bob.anim["translation"] = lambda tm: (
        centre[0], centre[1] + 0.35 * math.sin(two_pi_t(tm)), centre[2])
    sway = bob.add(Node("LogoSway"))
    sway.anim["rotation"] = lambda tm: quat_axis(
        (0, 1, 0), math.radians(16) * math.sin(two_pi_t(tm)))
    tilt = sway.add(Node("LogoTilt"))
    tilt.anim["rotation"] = lambda tm: quat_axis(
        (1, 0, 0), math.radians(6) * math.cos(two_pi_t(tm)))
    spin = tilt.add(Node("LogoSpin", logo_mesh(6.0, 0.55, 1.4), logo_mat))
    spin.anim["rotation"] = lambda tm: quat_axis(
        (0, 0, 1), -math.radians(90) * tm / LOOP_SECONDS)
    roots.append(bob)

    # translucent pieces, drawn back to front in this order
    star_tex = make_star()
    rnd = random.Random(3)
    plain_star = mat("Star", texture=star_tex, blend=True)
    twinklers = []
    for i in range(6):
        phase = i / 6.0
        cycles = 1 + (i % 2)

        def anim(tm, phase=phase, cycles=cycles):
            b = 0.55 + 0.45 * math.sin(
                2 * math.pi * (cycles * tm / LOOP_SECONDS + phase))
            return (b, b, b, b)
        twinklers.append(mat("StarTwinkle%d" % i, texture=star_tex, blend=True,
                             color_anim=anim))
    stars = []
    # far stars, just in front of the sky
    for i in range(22):
        x, y = rnd.uniform(-25, 25), rnd.uniform(-11, 16)
        if -1 < x < 19 and -3 < y < 6:  # keep the title area clean
            continue
        size = rnd.uniform(0.6, 1.3)
        m = twinklers[i % 6] if i % 3 == 0 else plain_star
        stars.append((-14.5, Node("StarFar%d" % i, quad_mesh(size, size), m,
                                  t=(x, y, -14.5))))
    # a few closer stars for depth, near the edges
    for i, (x, y, z) in enumerate([(15.5, 9.0, -3), (-17, -6.5, -2),
                                   (2.0, 10.5, -5), (18.5, -7.0, -6),
                                   (-4.5, -8.6, -1)]):
        m = twinklers[(i + 2) % 6]
        stars.append((z, Node("StarNear%d" % i, quad_mesh(0.9, 0.9), m,
                              t=(x, y, z))))
    stars.sort(key=lambda s: s[0])
    roots += [n for _, n in stars]

    halo = mat("Halo", texture=quantize_rgba4(
        make_radial(64, (150, 190, 255), 0.42, 1.8)), blend=True)
    roots.append(Node("Halo", quad_mesh(19, 19), halo, t=(-9.4, 1.4, 1.0)))

    title = mat("Title", texture=make_text("Moonlight", 256, 64, TITLE_FONTS,
                                           (255, 255, 255)), blend=True)
    roots.append(Node("Title", quad_mesh(17.0, 4.25), title, t=(8.8, 3.2, 2.0)))
    sub = mat("Subtitle", texture=make_text("Game Streaming", 256, 32, SUB_FONTS,
                                            (178, 196, 228)), blend=True)
    roots.append(Node("Subtitle", quad_mesh(16.0, 2.0), sub, t=(8.8, -0.7, 2.0)))
    return roots, mats


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
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--preview", action="store_true",
                    help="also render preview frames (slow, pure Python)")
    args = ap.parse_args()
    roots, mats = build_scene()
    glb = os.path.join(args.out, "banner.glb")
    write_glb(roots, mats, glb)
    print("wrote", glb)
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
