"""Converts banner.glb to banner.cgfx with pycgfx, then sets the root node
("Banner", see make_banner.py) to a Y-axial billboard.

    python convert.py PATH_TO_PYCGFX banner.glb ../res/banner.cgfx

The HOME Menu turns every 3D banner about a vertical axis by itself; a
Y-axial billboard turns that node back to face the viewer, so the banner
stays flat and still. glTF has no billboards, so this is set on pycgfx's
converted model before it is written.
"""

import os
import sys

pycgfx_dir, glb, out = sys.argv[1:4]
sys.path.insert(0, os.path.abspath(pycgfx_dir))

import gltflib  # noqa: E402
import main as pycgfx  # noqa: E402  (pycgfx's main.py)
from cgfx.sobj import BillboardMode  # noqa: E402

gltf = gltflib.GLTF.load(glb, load_file_resources=True)
cgfx = pycgfx.convert_gltf(gltf)
model = cgfx.data.models["COMMON"]
bone = model.skeleton.bones["Banner"]
bone.billboard_mode = BillboardMode.YAxial
with open(out, "wb") as f:
    f.write(pycgfx.write(cgfx))
print("wrote", out, "with the Banner node as a Y-axial billboard")
