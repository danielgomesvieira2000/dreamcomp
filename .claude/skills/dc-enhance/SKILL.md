---
name: dc-enhance
description: Add a PC enhancement to a dreamcomp Dreamcast port - widescreen, HUD fixes, internal resolution, texture packs, mods, frame rate, settings - separating the reusable framework mechanism from per-game findings. Use when Daniel asks for widescreen/texture packs/higher fps/mods/settings on a Dreamcast port, or "the same X as in my other port".
argument-hint: "<enhancement> [port]"
---

# Enhancement: $ARGUMENTS

## 1. Mechanism or finding?

| Piece | Where it belongs |
|---|---|
| Generic mechanism (settings key, presenter mode, hook plumbing, texture hashing, mod loader) | dreamcomp: `src/`, `include/dreamcomp/`, or `engine/` (+ ledger) |
| Per-game measurement (an address, a float, a function entry, a HUD draw identity) | the port: `src/<id>.cpp`, `game/<id>.toml` `[hooks]`, documented in `docs/GAME-INTERNALS.md` |

Copy mechanisms literally between ports; **re-measure findings** on each game.

## 2. Known recipes

- **Widescreen, anamorphic** (`PortInfo::widescreen`): one projection value the game recomputes
  (Flycast `core/cheats.cpp` table by product number). Re-assert each vblank; prefer an `@exit`
  hook on the function that computes it once found. Presenter shows 16:9 (`widescreen_aspect`).
  Known cost: HUD stretched -> fix with hooks on the 2D path.
- **Widescreen, Hor+**: widen the game's viewport/clip values in an exit hook so culling widens
  too (engine `docs/widescreen-crazytaxi-study.md` is the worked method).
- **Internal resolution**: engine `--scale`, setting `scale`.
- **Higher fps**: never change game logic rate; frame generation per engine
  `docs/rendering-enhancements-study.md`.
- **Texture packs / mods**: see `DC/docs/ROADMAP.md` P2 for the agreed design before building.

## 3. Verify on the picture

`dc.py shots` before/after on the same frames and press script, in the condition the
enhancement affects (gameplay, not only menus). Long run (`dc.py report --frames 3600+`) with the
enhancement on: it must not fault -- RAM patches have crashed games when written at the wrong time.
Measure per-frame cost if any work is added (Daniel plays on Iris Xe).

## 4. Ship it on

Once verified, the setting defaults **on** in the build Daniel runs; the switch exists to turn it
off for A/B. Say which switch reverts it. Docs: usage + how-it-works in the same commit.
