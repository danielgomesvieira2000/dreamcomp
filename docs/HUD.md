# Widescreen HUD correction

## Using it

Ports with anamorphic widescreen (`PortInfo::widescreen`) draw their 2D HUD in the guest's
640x480 screen space, so the horizontal stretch that widens the 3D view also widens the HUD.
With `PortInfo::hud.enabled`, HUD primitives are un-stretched each frame so they keep their
original proportions. Setting `hud_layout`:

- `edges` (default): HUD sprites are grouped into elements -- pieces that touch, and glyphs of one
  text line (same top/bottom within 4 px, word gaps up to 1.5x the text height) -- and each
  element is un-stretched about the edge of the screen third its centre falls in: health bars and
  names stay at the screen edges, the timer stays centred.
- `center`: the whole HUD is scaled about the screen centre, i.e. the original 4:3 layout,
  centred.

`hud_fix = false` (or `--set hud_fix=false`) turns the correction off for an A/B. The launch report prints how many primitives
were corrected.

## How a primitive is classified

A display-list primitive is HUD when all hold:

1. it is a TA **sprite** (PCW para type 5),
2. all its vertices share one depth (1/w),
3. another sprite in the same frame has exactly that depth (`min_shared`, default 2), or its depth
   is ≥ `overlay_z` (default 1000),
4. it is not wider than `full_width` (default 600 px): full-screen fades and flashes stay full width.

Why rule 3: games draw their 2D layers at a common depth, while 3D sprites (particles, billboards)
are projected individually and land at distinct depths. Measured on Soulcalibur (17 fight frames,
`dreamcomp_ta_dump` + a cluster script): HUD sprites share 1/w values that move with the scene
(0.113-0.207) plus an overlay layer at 2000; particle sprites reach 0.41, so a plain depth
threshold would misfire, but every particle was a singleton.

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
- A 2D element drawn as polygons instead of sprites is not corrected (none seen in Soulcalibur
  fights).
