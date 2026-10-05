# Roadmap

Phases have **entry gates** (measured true before starting) and **exit criteria** that are visible
behaviour, not "it builds". Status is honest: "not verified" means not verified.

## F0 — Framework foundation ✅ (2026-10-05)

| Item | Status |
|---|---|
| Engine vendored (D-001), builds on Windows with clang-cl + Ninja + Vulkan | done |
| `tools/dc.py` setup/build/run/report/shots; `tools/audit.py` + pre-commit hook | done |
| Port layer: `[hooks]`, launcher extensions, settings, presentation modes | done; hooks compiled but **not yet exercised by a port** |
| Docs, decisions, legal, skills | done (this commit series) |

## P1 — Soulcalibur bring-up (active)

Entry gate: met (boots to menus on the engine).

| Exit criterion | Status |
|---|---|
| Boots from `--disc` alone, boot file verified | done |
| Music and effects play | done (0 → 12 key-ons in 1800 frames; RMS -13…-25 dBFS); **not yet listened to by Daniel** |
| Reaches a fight; 0 untranslated targets over 60 s | 1 new target (`0x0C019FC0`) at ~frame 3000; interpreter covers it; seed pending |
| Plays a full arcade run with no fault | not verified |
| Saves to the per-user VMU and loads them | done 2026-10-05: fresh card → "Successfully saved"; next boot → "Successfully loaded" (27 block reads) |
| Intro (frames 60-200): checkered gaps in the green tile background | **unverified**: may be the original effect; needs comparison with real hardware / a reference video |
| Daniel playtests and confirms | pending |

## P2 — Enhancements

| Item | Plan | Status |
|---|---|---|
| Internal resolution | engine `--scale 1-4`; setting `scale` | works (engine) |
| Widescreen (anamorphic) | `PortInfo::widescreen`; 16:9 / 21:9 / 32:9 at full resolution (`--render-aspect`) | done; crash was an emitter defect (fixed); no edge culling seen at 32:9 |
| Widescreen HUD fix | per-frame sprite-depth rule (docs/HUD.md): HUD keeps its 4:3 proportions, centred | done for Soulcalibur; edge anchoring needs an inspector |
| Texture dump / replacement packs | content hash (XXH64, scheme v1) over the raw VRAM bytes the decoder reads (+ used palette colours / VQ codebook); `--dump-textures DIR`, `--texture-pack DIR`; settings `dump_textures`, `texture_pack` (default `<config dir>/textures`); in-house PNG codec | works (Soulcalibur: dump + 1 replaced texture verified in-game); no manifest, no mip chain, no async preload yet. `docs/TEXTURE-PACKS.md` |
| Mods: file replacement | virtual disc layer (`ModDisc`) that serves `<settings dir>/mods/<name>/<path>` in place of disc files, re-laying out ISO9660 extents; `--mod DIR`, setting `mods`, launcher list | works 2026-10-05: same-size (in place) and larger (relocated) replacements logged on Soulcalibur; first-wins order verified; in-game effect of a real mod not yet shown |
| Mods: code | `[hooks]` + port sources; later a mod DLL ABI | hooks done |
| Higher frame rate | geometry interpolation between game frames (docs/INTERPOLATION.md); logic stays at 60 | implemented; G1-G3 passed, G4 needs a >60 Hz display (auto mode off on 60 Hz) |
| Settings UI / launcher (disc picker) | N64-recomp-style frontend ([FRONTEND.md](FRONTEND.md)): launcher list (Load/Start Game, Controls, Settings, Mods, Quit) + tabbed settings panel with description pane, Apply, Quit Game confirm; the same panel over the running game on Escape / pad Select (CPU-rendered, composited) | redesign done 2026-10-05: screenshots of every screen and of the in-game panel over a running fight checked; keyboard, synthetic pad and mouse scripted; Apply (live: fit, HUD, rumble, volume, fullscreen) and Quit verified; **no physical pad tried, Daniel has not used it yet**; window move/resize still stalls the game (guest thread pending) |

## P3 — Platforms

| Platform | Plan | Status |
|---|---|---|
| Windows x64 | primary | works |
| Linux x64 | WSL build script + CI | not started |
| macOS ARM64 | engine's primary dev platform (MoltenVK); CI | not verified here |
| Android ARM64 | SDL3 + Vulkan, Gradle wrapper, host-built translator output, SAF disc access | not started; blockers listed in ARCHITECTURE notes (execinfo, argv/config paths, lifecycle) |

## P4 — Tooling

| Item | Status |
|---|---|
| Automated screenshots / contact sheet (`dc.py shots`) | done |
| Headless reports (`dc.py report`) | done |
| Profiler: symbolised per-function hot list from `--sample` | not started |
| Regression gate per port (engine `tools/regress/gate.py`) | not wired up |
| CI (audit + engine tests + port config lint) | not started |

## P5 — Second title (Jet Grind Radio)

Entry gate: P1 exit met; the port template extracted from Soulcalibur.
