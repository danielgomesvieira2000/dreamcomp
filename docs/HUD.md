# Widescreen HUD correction

## Using it

Ports with anamorphic widescreen (`PortInfo::widescreen`) draw their 2D HUD in the guest's
640x480 screen space, so the horizontal stretch that widens the 3D view also widens the HUD.
With `PortInfo::hud.enabled`, HUD primitives are un-stretched each frame so they keep their
original proportions -- always, with the Expanded aspect ratio (Daniel's rule: a widened picture
never stretches the HUD; there is no on/off setting). Setting `hud_layout` (menu: HUD layout):

- `edges` (menu "Expanded", default): HUD sprites are grouped into elements -- pieces that touch,
  and glyphs of one text line -- and each element keeps its 4:3 distance from the screen edge of
  the third its centre falls in: left third from the left edge, right third from the right edge,
  middle third from the centre. Health bars and names sit at the screen edges, the timer stays
  centred, and a row such as "STAGE 1  0'09"07" stays together at any width (it drifted apart
  when elements were un-stretched about their own edges, fixed 2026-10-06).
- `center` (menu "Original"): the whole HUD is scaled about the screen centre, i.e. the original
  4:3 layout, centred.

With the Original aspect ratio there is nothing to correct: the HUD is exactly the console's.
`DREAMCOMP_NO_HUD_FIX=1` shows the stretched HUD, for an A/B. The launch report prints how many primitives
were corrected.

## How a primitive is classified

A display-list primitive is HUD when all hold:

1. it is a TA **sprite** (PCW para type 5), or a polygon (type 4) when `polygons` is set,
2. all its vertices share one depth (1/w),
3. another sprite in the same frame has exactly that depth (`min_shared`, default 2), or its depth
   is ≥ `overlay_z` (default 1000),
4. it is not wider than `full_width` (default 600 px): full-screen fades and flashes stay full width,
5. the flat pieces at its depth do not together cover more than `backdrop_cover` (default 0.6) of
   the 640x480 screen (summed bounding boxes): tiled full-screen backdrops keep filling the picture.

Why rule 3: games draw their 2D layers at a common depth, while 3D sprites (particles, billboards)
are projected individually and land at distinct depths. Measured on Soulcalibur (17 fight frames,
`dreamcomp_ta_dump` + a cluster script): HUD sprites share 1/w values that move with the scene
(0.113-0.207) plus an overlay layer at 2000; particle sprites reach 0.41, so a plain depth
threshold would misfire, but every particle was a singleton.

Soulcalibur sets `polygons = true` and `overlay_z = 1.0` (2026-10-06). Character select draws its
portraits and frames as flat type-4 polygons at 1/w 0.5 and 0.7 (shared), with the selected
portrait at 3 and its frame at 5.01 (each alone); with sprites only they stayed stretched and the
frames slid off the pictures. No 3D geometry in its fights came closer than about 0.3. The
attract-mode character intros then lost their full-screen backdrops (tiled flat polygons squeezed
into 4:3 with black bars), hence rule 5. Checked with `tools/scenario.py`: fight frame 2500
pixel-identical to before, character select aligned in both layouts, attract backdrops full
width; the namco logo is now un-stretched; some light streaks in the sword scene are squeezed
toward the centre (still reaching both edges).

## Mechanism (to reuse elsewhere)

- Engine: `dream::host::Extension::on_frame(render::Frame&)` is called with each decoded display
  list before it is drawn (`engine/runtime/include/dream/runtime/host_ext.h`, called from the
  launcher's `Live::render`).
- dreamcomp: `Core::on_frame` (`src/core.cpp`) applies `PortInfo::hud`. Transform:
  `x' = 320 + (x - 320) * (4/3) / aspect` in guest space, before the anamorphic render target
  stretches it back.
- Per game: measure with `--capture-at N` and `dreamcomp_ta_dump capture.ta`, look at sprite
  depths in the HUD's screen region, then set the rule's fields in the port's `PortInfo`.

## Limits and next step

- Edge grouping is a heuristic: in Soulcalibur the round clock is grouped apart from "STAGE 1"
  and anchors to the centre. Exact per-element anchoring (and overrides) belongs in an in-game
  inspector like Daniel's N64 ports (select an element, choose left/centre/right).
- With `polygons`, a 3D scene that draws flat polygons at a shared depth (a wall facing the
  camera head-on) would be squeezed; not seen in Soulcalibur's fights, attract or menus.
