# 3D HOME Menu banner

The banner shown on the top screen when Moonlight is selected in the HOME
Menu is a 3D model in CGFX format (`3ds/res/banner.cgfx`), like the banners of
retail games. bannertool packs it into the CIA when the Makefile's
`BANNER_IMAGE` ends in `.cgfx`; with a `.png` it makes the old flat banner.

`make_banner.py` builds the scene from code, so no 3D modelling program is
needed: the eight-wedge Moonlight logo turning like a wheel in place, a glow
behind it and the title, all popping out of the screen, in front of a night
sky with twinkling stars from deep behind the screen to out in front of it.
The top of the script lists the camera and the limits the scene is designed
around.

## Rebuilding

Requirements: Python 3.12+ with `pillow` and `gltflib`, and
[pycgfx](https://github.com/skyfloogle/pycgfx) (commit `1f78850`).

```sh
python make_banner.py --preview      # writes banner.glb and banner-preview.png
python path/to/pycgfx/main.py banner.glb ../res/banner.cgfx
```

`--preview` renders four frames of the loop with a small software renderer
that approximates the HOME Menu's lighting, which is enough to check the
layout. The real banner must still be checked on a 3DS.

pycgfx warns if the CGFX is over 512 KiB, the most the 3DS accepts.
