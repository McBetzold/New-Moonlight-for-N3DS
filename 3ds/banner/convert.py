"""Converts banner.glb to banner.cgfx with pycgfx, then applies the fixes a
real HOME Menu needs.

    python convert.py PATH_TO_PYCGFX banner.glb ../res/banner.cgfx

pycgfx is used unmodified (skyfloogle/pycgfx, pinned commit 1f78850); the
fixes are applied to its converted model before it is written. They were
learnt on hardware by others, and are reimplemented here:

  From daaaaavia/CBC-for-YANBF (MIT), as listed by GabeSud0/pokeplatinum-3ds
  (tools/banner3d, ~30 hardware tests):
  1. every mesh records its node's bone (mesh_node_name); some banners crash
     the HOME Menu without it
  2. the bone lookup tree is rebuilt after pycgfx moves the bones of BLEND
     materials to the end; otherwise a bone lookup finds the wrong bone and
     the HOME Menu crashes (this is what crashed 62abada)
  3. BLEND materials are marked translucent (pycgfx leaves them opaque)
  From pokeplatinum-3ds:
  4. materials are unlit (texture x material colour), whatever the HOME
     Menu's light does
  5. the HOME Menu turns every 3D banner one full turn per 10 s about the
     vertical axis. "autorotate" turns the other way at the same rate (like
     Pokemon Y's y_autorotate), so everything under it stays still, and the
     flat cards are screen-facing billboards whose bones sit at the origin
  6. every curve loops over its own keys inside the animation
  And ours:
  7. the wheel's full turn is written as one float curve (0 to -2 pi), which
     glTF -> Euler conversion cannot express without wrapping at 180 degrees
  8. no material animations: pokeplatinum-3ds found them unsafe on the HOME
     Menu, so a scene that has them is refused
"""

import math
import os
import sys

CGFX_LIMIT = 0x80000          # the HOME Menu's limit for a banner's CGFX
BILLBOARDS = ("backdrop", "badge", "title")
SPIN_NODE = "wheelspin"
AUTOROTATE = "autorotate"


def main():
    pycgfx_dir, glb, out = sys.argv[1:4]
    sys.path.insert(0, os.path.abspath(pycgfx_dir))
    import gltflib
    import main as pycgfx  # pycgfx's main.py
    from cgfx.canm import (FloatAnimationCurve, FloatSegment, InterpolationType,
                           QuantizationType, RepeatMethod, StepLinear64Key)
    from cgfx.mtob import ConstantColorSource, MTOBFlag
    from cgfx.sobj import BillboardMode

    gltf = gltflib.GLTF.load(glb, load_file_resources=True)
    scene = pycgfx.convert_gltf(gltf)
    cmdl = scene.data.models["COMMON"]
    bones = cmdl.skeleton.bones

    # 1. each mesh's node bone (pycgfx names meshes "<mesh>_<material>")
    mesh_node = {gltf.model.meshes[n.mesh].name: n.name
                 for n in gltf.model.nodes if n.mesh is not None}
    for sobj_mesh in cmdl.meshes.data.contents:
        name = max((m for m in mesh_node if sobj_mesh.name.startswith(m + "_")),
                   key=len)
        sobj_mesh.mesh_node_name = bones[mesh_node[name]]
    # 2. the bone lookup tree
    bones.dict.regenerate()
    # 3. translucent BLEND materials, 4. unlit materials
    blend = {m.name for m in (gltf.model.materials or []) if m.alphaMode == "BLEND"}
    for name in cmdl.materials:
        material = cmdl.materials[name]
        if name in blend:
            material.transluscency_kind = 1
        material.flags = MTOBFlag(0)
        stage = material.fragment_shader.texture_combiners[0]
        stage.constant = ConstantColorSource.Diffuse
        stage.src_rgb = stage.src_alpha = 0x0E3   # texture x constant (diffuse)
        stage.combine_rgb = stage.combine_alpha = 1
        for stage in material.fragment_shader.texture_combiners[1:]:
            stage.src_rgb = stage.src_alpha = 0x00F   # pass the previous stage
            stage.combine_rgb = stage.combine_alpha = 0
    # 8. no material animations
    if list(scene.data.material_animations):
        sys.exit("material animations are not safe in a HOME Menu banner")
    # 5. billboards
    found = set()
    for n in bones.dict.nodes[1:]:
        if n.content.name in BILLBOARDS:
            n.content.billboard_mode = BillboardMode.Screen
            found.add(n.content.name)
    missing = set(BILLBOARDS) - found
    if missing:
        sys.exit("billboard nodes not found: %s" % sorted(missing))

    # 5./6./7. the animation
    def full_turn(frames, turn):
        curve, seg = FloatAnimationCurve(), FloatSegment()
        curve.start_frame, curve.end_frame = 0.0, frames
        curve.pre_repeat_method = curve.post_repeat_method = RepeatMethod.Repeat
        seg.start_frame, seg.end_frame = 0.0, frames
        seg.interpolation = InterpolationType.Linear
        seg.quantization = QuantizationType.StepLinear64
        seg.keys = [StepLinear64Key(0.0, 0.0), StepLinear64Key(frames, turn)]
        curve.segments.append(seg)
        return curve

    done = set()
    for name in scene.data.skeletal_animations:
        animation = scene.data.skeletal_animations[name]
        animation.looping = True
        frames = float(animation.frame_size)
        for member in animation.member_animations_data:
            bone = animation.member_animations_data[member]
            for curve in vars(bone).values():
                if hasattr(curve, "segments") and curve.segments:
                    curve.start_frame = min(sg.start_frame for sg in curve.segments)
                    curve.end_frame = max(sg.end_frame for sg in curve.segments)
                    curve.pre_repeat_method = curve.post_repeat_method = RepeatMethod.Repeat
            if member in (AUTOROTATE, SPIN_NODE):
                bone.scale_x = bone.scale_y = bone.scale_z = None
                bone.pos_x = bone.pos_y = bone.pos_z = None
                bone.rot_x = bone.rot_y = bone.rot_z = None
                if member == AUTOROTATE:
                    bone.rot_y = full_turn(frames, 2 * math.pi)
                else:
                    bone.rot_z = full_turn(frames, -2 * math.pi)
                done.add(member)
    if done != {AUTOROTATE, SPIN_NODE}:
        sys.exit("animated nodes not found: %s" % sorted({AUTOROTATE, SPIN_NODE} - done))

    raw = pycgfx.write(scene)
    if len(raw) > CGFX_LIMIT:
        sys.exit("the banner's CGFX is %d bytes, over the HOME Menu's %d"
                 % (len(raw), CGFX_LIMIT))
    with open(out, "wb") as f:
        f.write(raw)
    print("wrote %s (%d bytes, %d frames)" % (out, len(raw), int(frames)))


if __name__ == "__main__":
    main()
