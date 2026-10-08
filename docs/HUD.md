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

## Overrides and the F1 editor (2026-10-06)

What the heuristic gets wrong is fixed per element, without code:

| Piece | Where |
|---|---|
| Override | `rect=x0,y0,x1,y1 anchor=left\|center\|right\|stretch\|auto [tcw=0x..,0x..]`, one per line: a rectangle in the console's 640x480 coordinates *before* correction (holds at any window shape), the texture words of the element's pieces (none: any), optional `full=1`. A flat piece whose centre is in the rectangle and whose texture is listed is forced into the HUD with that anchor (`auto`: by thirds), or left stretched. Pieces wider than the rule's `full_width` (fades, full-screen backdrops) are matched only by `full=1` overrides. Later lines win. |
| Files | the port's `game/hud_overrides.ini` beside `<id>.toml` (loaded first, shipped), then the player's `<settings dir>/hud_overrides.ini`. Log line `dreamcomp: HUD overrides: N from the port, M from the player`. |
| Editor | **F1** in game (src/frontend/hud_editor.cpp, in the overlay; the engine's binding screen no longer opens on F1). Outlines per element: **L** blue, **C** green, **R** red, **S** yellow (stretched), `*` overridden; grey: flat 2D pieces left alone. Left click cycles left > centre > right > stretched, right click back to automatic, a click on grey adds the piece; **Space** (or Pause) holds the game still so the HUD on screen can be edited -- the still frame is redrawn with each change -- and closing the editor always resumes; drag the panel to move it (P: next corner); S saves the player's file. Works with Original too (shows where elements would anchor; nothing moves). |
| List (F2) | The same editor as a list (Daniel, 2026-10-07: small elements were hard to click). One row per **piece** -- every HUD primitive on its own (`Snapshot::pieces`; Daniel: the F1 groups hide the individual elements), top to bottom, then the grey pieces -- with its position, size, `*` if overridden, and a dropdown: Automatic / Left / Centre / Right / Stretched (grey pieces: Not in HUD / Add: automatic / an anchor). Hovering a row frames that piece in white (one `hlbox` outline, `pointer-events: none`). A piece given its own override is never grouped with pieces that have another override or none, so it moves on its own (F1 groups follow). Opening the list pauses the game (rows stay put; Space resumes). Rows are rebuilt only when the elements or anchors change, so an open dropdown is not closed by the next snapshot. A choice posts `hud_set(box, value)` to the game thread (`Core::hud_set`: `auto` resets an override or adds a piece, an anchor creates or updates the player's override; the full-width guard applies). **Centre all** (button, list mode only; Daniel 2026-10-08: automatic anchors flip when an element crosses a screen third) gives every HUD row that is not centred a centre override in one game-thread task; grey rows stay out; the exceptions are then changed by hand. Each piece gets its own override, so on text this makes one per letter: check other screens after promoting (letters elsewhere at the same spot match). |
| Untextured pieces | Their TCW slot holds leftover data that changes every frame (JGR: `0x3CBAxxxx`, floats), so the texture word compared is 0 for a polygon without the PCW texture bit. Files saved before that carried such words: a word whose pixel-format field is 7 (reserved, never a texture) is read as 0 on load and by `hud_promote.py`, which also merges repeats (same anchor and words, rectangles overlapping by half). Found when one JGR piece needed 29 saves, each matching one frame. |
| Promotion | `python tools/hud_promote.py <slug>`: merges the player's file into the port's, empties the player's (`.bak` kept). Skill `/dc-hud`. |
| Code | `include/dreamcomp/hud.h`, `src/hud.cpp` (`hud::correct`: classification, overrides, grouping, anchoring, snapshot), `Core::on_frame` / `hud_edit` / `hud_save` (src/core.cpp), unit tests `src/tests/test_hud.cpp` (target `dreamcomp_hud_tests`). |

Verified 2026-10-06: the move into src/hud.cpp is pixel-identical (fight frame 2500, character
select, all 14 attract shots); scripted F1 session: outlines on every fight element, a click set
Kilik's group (health bar, name, STAGE 1, clock) to centre, save wrote it, promotion into the port
was loaded back (`1 from the port`).

Daniel's first session (2026-10-07, real mouse) asked for: F1 never opening the controls screen
(the engine polled F1 apart from the consumed event; refused now when an overlay exists), a
movable panel (drag), and a pause to edit a HUD that is only briefly on screen. Verified by
script: pause at frame 3726, a click moved Kilik's group on the still frame and its outline
followed (snapshots are numbered per redraw, not per vblank), the panel dragged and kept its place
after reopening, game resumed; fight scenario bit-identical when the editor is not used. Also
fixed: elements with no piece drawn produced `left: -infpx` style errors (now skipped).

Same day, a cascade from that session's saved file: one broad override (8,42-381,302, including
the untextured word 0x00000000) pulled the game's full-screen fade quad into the HUD; grouping then
chained it with everything into one outline covering the screen; a click on it created a
whole-screen override, and every later click on a piece with those textures cycled that same
entry. Fixes: overrides skip pieces wider than `full_width` unless `full=1`; the editor refuses to
create a new override from an outline wider than the rule's `full_width` (status line says why;
Reset still works). It first also refused anything over half the screen's area; that blocked a real
486x484 element in Jet Grind Radio (57 refused clicks in Daniel's session), so area no longer counts. Verified: the VS screen shows separate outlines (portraits, VS, STAGE 1);
unit checks for `full`.

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
- Per game: measure with `--capture-at N` (or `scenario.py ... -- --dump-ta F --dump-ta-at-frame
  N`) and `dreamcomp_ta_dump capture.ta`, look at the depths of flat pieces in the HUD's screen
  region, then set the rule's fields in the port's `PortInfo`. First check the summary line:
  `sprites 0` means the HUD is polygons and needs `polygons = true` (Jet Grind Radio).
- Verify at a shape where a miss is obvious: `-- --aspect 21:9` (fixed shape for the run, headless
  shots included) and compare an element's width against the 4:3 shot; uncorrected it is 1.75x.

| Game | Rule | Overrides shipped |
|---|---|---|
| Soulcalibur | sprites + `polygons`, `overlay_z 1.0`, backdrop cover | none |
| Jet Grind Radio | `polygons`, `overlay_z 1.05` (HUD at 1/w 1.11-6.67, 3D below 0.05) | boot notice fade (full-screen render-to-texture quad) |

## Sharp 2D (upscaled fonts and HUD art)

Fonts and HUD art are packed tight in their textures and drawn as screen-aligned quads that map
one texel to one console pixel, usually bilinear filtered. At 640x480 every pixel centre lands on a
texel centre, so the console's filter returns the texel itself. Rendered at 2x or more, the edge
pixels sample a quarter texel outside the quad and pull in the neighbouring glyph: thin frames
around every character (Jet Grind Radio's dialogue; gone at `scale=1`, which proves the cause).

`sharpen_2d` (`include/dreamcomp/sharp2d.h`, `src/sharp2d.cpp`), run on every frame before the HUD
correction, switches bilinear to point sampling on textured 4-vertex quads that are axis-aligned,
flat (one 1/w), on whole console pixels, and map exactly one texel per pixel starting on a texel
edge (flips allowed; stride, mipmapped and two-volume textures skipped). That reproduces the 1x
picture at any scale. Setting `sharp_2d` (default true); `DREAMCOMP_NO_SHARP_2D=1` forces it off.
Report line `dreamcomp: sharp 2D: N ... quads point sampled`. Unit checks in `dreamcomp_hud_tests`.

Verified 2026-10-06: Jet Grind Radio play scenario -- dialogue frames gone, HUD art with the
console's pixel edges, only 2D regions change (0.3-2 % of pixels), audio identical; Soulcalibur
fight and menus -- only the name plates change (sharper). Taste note: bilinear-upscaled HUD art
looks softer; the setting keeps that available.

## Limits and next step

- Edge grouping is a heuristic: in Soulcalibur the round clock is grouped apart from "STAGE 1"
  and anchors to the centre. Exact per-element anchoring (and overrides) belongs in an in-game
  inspector like Daniel's N64 ports (select an element, choose left/centre/right).
- With `polygons`, a 3D scene that draws flat polygons at a shared depth (a wall facing the
  camera head-on) would be squeezed; not seen in Soulcalibur's fights, attract or menus.
