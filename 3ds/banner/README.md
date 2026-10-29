# Icon and 3D HOME Menu banner

The banner shown on the top screen when New Moonlight for N3DS is selected in
the HOME Menu is a 3D model in CGFX format (`3ds/res/banner.cgfx`), like the
banners of retail games. bannertool packs it into the CIA when the Makefile's
`BANNER_IMAGE` ends in `.cgfx`; with a `.png` it uses the flat
`3ds/res/banner.png` instead.

- `logo_art.py` draws the logo in pieces: the wheel (cut out of
  `wheel_art.jpg`), the silver "new" with its coloured dashes, and
  "Moonlight / for N3DS" with the red M and 3.
- `make_banner.py` builds the 3D scene from those pieces (no 3D modelling
  program needed): Isaac's wheel (`wheel_art.jpg`, with the gaps between the
  spokes made see-through) spinning in place with "new" fixed in front of it
  and the title beside it, all popping out of the screen, over a night sky
  with stars that twinkle at every depth. It also writes the 48x48 app icon
  (`3ds/res/ic_moonlight.png`) and the flat banner. The top of the script
  lists the camera and the limits the scene is designed around.

## Rebuilding

Requirements: Python 3.12+ with `pillow` and `gltflib`, the Segoe UI Black
font (Windows), and [pycgfx](https://github.com/skyfloogle/pycgfx) (commit
`1f78850`).

```sh
python make_banner.py --preview --assets ../res   # banner.glb, preview, icon, flat banner
python path/to/pycgfx/main.py banner.glb ../res/banner.cgfx
```

`--preview` renders four frames of the loop with a small software renderer
that approximates the HOME Menu's lighting, which is enough to check the
layout. The real banner must still be checked on a 3DS.

pycgfx only warns above 512 KiB, but a 458 KB banner froze the whole 3DS as
soon as the app was selected in the HOME Menu, while 220-230 KB banners
worked. Keep `banner.cgfx` near 250 KB or less; textures (2 bytes per pixel)
are most of it, so keep them small.
