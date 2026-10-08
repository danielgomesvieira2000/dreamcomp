---
name: dc-hud
description: Fix or tune a dreamcomp port's widescreen HUD per element - see how each HUD element is anchored (F1 editor), change or add anchors, save, and promote the player's saved overrides into the port's game/hud_overrides.ini so they ship. Use when Daniel says a HUD element sits wrong, stretches, or should anchor elsewhere in a Dreamcast port, or asks to make HUD edits permanent.
---

# dc-hud: per-element HUD anchoring

The automatic correction (docs/HUD.md) anchors each HUD element by the screen third it sits in.
Overrides fix the rest. They are a rectangle in console coordinates before correction, the
element's texture words, and an anchor: left, center, right, stretch (left alone), or auto
(force into the HUD, anchored by thirds).

## Daniel's loop (in game)

1. Expanded aspect ratio, a screen with the HUD in question; press **F1**.
2. Outlines show every element: **L** blue / **C** green / **R** red / **S** yellow (stretched),
   `*` = overridden. Grey outlines are 2D pieces the correction left alone.
3. Left click cycles left > centre > right > stretched; right click goes back to automatic; a
   click on a grey outline adds that piece to the HUD. **Space** pauses the game (a HUD that is
   only briefly on screen stays put; edits redraw the still frame), drag the panel to move it
   (**P**: next corner), **S** saves, **F1/Esc** closes (and resumes).
   **F2** opens the same editor as a list of individual pieces (not the F1 groups), each with a
   dropdown for its anchor; hovering a row frames that piece; it pauses the game itself. A piece
   anchored on its own leaves its group. **Centre all** (F2 button) centres every listed HUD
   piece at once; then change the exceptions.
4. Saved to `<settings dir>/hud_overrides.ini` (Windows: `%APPDATA%/dreamcomp/<id>/`).

## Making it permanent (you)

```
python tools/hud_promote.py <slug> --dry-run      # what would change
python tools/hud_promote.py <slug>                # merge into ports/<slug>-recomp/game/hud_overrides.ini
```

- It merges the player's file into the port's (same rectangle + textures: anchor updated; else
  appended), then empties the player's file (`.bak` kept) so stale lines cannot mask the port's.
  `--keep` leaves it; `--from FILE` promotes another file (a scenario's scratch folder).
- Before promoting, read the player's file: a rectangle covering most of the screen, or the
  untextured word `0x00000000` in a large rectangle, catches far more than one element (T20).
  After promoting, compare *every* scenario (fight, menus, attract) against a run without the
  overrides and show Daniel the screens that changed beyond the element he edited.
- Verify: `python tools/scenario.py <scenario> --set aspect=expanded` and read the log line
  `dreamcomp: HUD overrides: N from the port, 0 from the player`; compare screenshots before and
  after (`scenario.py --compare`).
- Commit `game/hud_overrides.ini` in the **port** repository with a line in its
  docs/GAME-INTERNALS.md (which element, why). It is numbers about where the game draws, not game
  data; `python tools/audit.py all` still has to pass.
- Packaging ships it beside `<id>.toml` (dc-release).

## Scripted checks (no Daniel needed)

```
DREAMCOMP_OVERLAY_KEYS="@2300,f1,wait,wait,wait,shot:a.png,click:hud-0,wait,wait,wait,wait,wait,wait,shot:b.png,s,wait,f1"
```
through `scenario.py --env` (fight scenario): outlines are `hud-N` / `piece-N` in drawing order.
F2 list: `...,f2,wait,wait,select:sel-0:center,...` sets row 0's dropdown (rows `row-N`, dropdowns
`sel-N`, values auto/left/center/right/stretch/none); `click:centerall` presses Centre all. The list logs every row (`hud editor: row ... override=I tcw=...`): grep it to find which override line moves a piece. Give the run ~200 frames after `@F`, and take shots with `--shots F-2` (an overlay `shot:` with the editor closed stalls the script).
An edit shows after about a second (vblank hand-over + snapshot refresh): wait before the shot.
Unit tests: target `dreamcomp_hud_tests` (`<build>/dreamcomp/dreamcomp_hud_tests <scratch dir>`).

## Limits

- An override follows a rectangle: an element that moves out of it (animated, slides in) drops
  out of the override while outside. Make the rectangle cover its path.
- Elements are the correction's groups (touching pieces, one text line). Two elements that the
  grouping merges get one anchor; split them with two narrower overrides.
