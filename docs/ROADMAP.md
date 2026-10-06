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
| Intro (frames 60-200): checkered gaps in the green tile background | not seen on 2026-10-06 (`scenario.py boot`, frames 80-260 at 2x): background continuous |
| Daniel playtests and confirms | first playtests 2026-10-05; reported crackling audio, wrong textures/HUD, performance |
| Accuracy pass 2026-10-06 (from Daniel's report, audits against Flycast master) | **graphics:** HUD black boxes (PT_ALPHA_REF power-on value), character shadows (modifier volumes), per-vertex/table fog, stale textures after a stage change (VRAM write tracking), rolling menu spilling out of its box (user tile clip), translucent depth/sort as Flycast, mipmap chains, per-sampler texture keys; **audio:** mixer/DSP/ARM7 audited line by line against Flycast -- identical with Flycast's DSP on; the crackle is the effects mix clipping on loud hits plus voice-reuse ticks, both also in Flycast (DSP on); small AICA register fixes. Each verified by scenario screenshots/diffs and 0 validation errors |
| Performance pass 2026-10-06 | 7x faster twiddled decode, staging ring, pipeline cache, in-game menu on its own thread, game on its own thread (window move/resize no longer stalls); late frames per fight run ~10 -> ~4 (scene changes only) |

## P2 — Enhancements

| Item | Plan | Status |
|---|---|---|
| Internal resolution | engine `--scale 1-4`; setting `scale` | works (engine) |
| Widescreen (anamorphic) | `PortInfo::widescreen`; setting Original / Expanded (N64-recomp style): Expanded follows the window's shape up to `max_aspect` at full resolution (`--render-aspect`, rebuilt live) | done; 2026-10-06 verified with real window resizes (16:9, 3:1, square) in both modes; no edge culling seen at 32:9 |
| Widescreen HUD fix | per-frame sprite-depth rule (docs/HUD.md): HUD keeps its 4:3 proportions, centred | done for Soulcalibur; edge anchoring needs an inspector |
| Texture dump / replacement packs | content hash (XXH64, scheme v1) over the raw VRAM bytes the decoder reads (+ used palette colours / VQ codebook); `--dump-textures DIR`, `--texture-pack DIR`; settings `dump_textures`, `texture_pack` (default `<config dir>/textures`); in-house PNG codec | works (Soulcalibur: dump + 1 replaced texture verified in-game); no manifest, no mip chain, no async preload yet. `docs/TEXTURE-PACKS.md` |
| Mods: file replacement | virtual disc layer (`ModDisc`) that serves `<settings dir>/mods/<name>/<path>` in place of disc files, re-laying out ISO9660 extents; `--mod DIR`, setting `mods`, launcher list | works 2026-10-05: same-size (in place) and larger (relocated) replacements logged on Soulcalibur; first-wins order verified; in-game effect of a real mod not yet shown |
| Mods: code | `[hooks]` + port sources; later a mod DLL ABI | hooks done |
| Frame pacing and latency | measured from the display (PresentMon, docs/PACING.md); pads read after the pacing sleep; display sync (`frame_timing`, D-008) | every frame shown once (0 repeats in 60 s); latency 48 ms frame start to photons through the Intel driver's GDI copy path; DXGI flip presentation built as the opt-in Latency: Low: plugged in 35 ms steady (mean 37.0 with fallbacks, 3 repeats in 90 s vs Standard's 47.8 ms and 1); on battery not worth it |
| Higher frame rate | geometry interpolation between game frames (docs/INTERPOLATION.md); logic stays at 60 | implemented; G1-G3 passed, G4 rate measured on a 60 Hz panel without vsync (1.0x, ~120 presents/s); smoothness needs a >60 Hz display (auto mode off on 60 Hz) |
| Settings UI / launcher (disc picker) | N64-recomp-style frontend ([FRONTEND.md](FRONTEND.md)): launcher list (Load/Start Game, Controls, Settings, Mods, Quit) + tabbed settings panel with description pane, Apply, Quit Game confirm; the same panel over the running game on Escape / pad Select (rendered on its own thread, blended on the GPU) | 2026-10-06: RecompFrontend-style option rows (all choices visible, greyed when unavailable), Controls tab with player cards and two binding slots per input (PromptFont glyphs), real mouse and keyboard verified; **no physical pad tried** |

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
| Profiler: symbolised per-function hot list | done 2026-10-06 (`--profile`, `tools/profile.py`; Windows). First fight capture at real speed: busy 74.5 %; of that translated game code 38.7 %, sound 16.1 %, renderer CPU 9.3 %, memory access 9.2 %, SH-4 runtime 6.2 %, driver 5.2 % |
| Profiler-driven optimisation, round 1 (2026-10-06) | Soulcalibur frame-wait idle skip (port hook; 47 % of emulated time was a wait loop calling an empty function), DSP stops at its last real step; both bit-identical. Unthrottled fight 46.7 -> 40.3 s host (-13.7 %); store-queue fast path -> 38.9 s; ARM pc histogram in a flat array; scheduler deadlines array (neutral). Not done, needs Daniel's call: non-local returns (4-5 % of busy, C++ exceptions unwinding to run_guest) could resume in the matching frame through an unwind flag, but interrupt checks would then fall a few instructions differently, so runs would no longer be bit-identical to today's. Tried and dropped: an exact ARM7 idle-pass skip (the driver's polling pass is 481 of a slice's 512 cycles, so no whole pass ever fits) |
| Scripted scenarios with screenshots, audio analysis, pacing and before/after diffs (`tools/scenario.py`, `tools/audio_check.py`) | done 2026-10-06 |
| Debug switches: `DREAM_AICA_PROBE`, `DREAM_PVR_WATCH`, `DREAM_TEX_SLOW`, `DREAM_NO_*` A/B switches; `pacing:` / `textures:` report lines | done 2026-10-06 (docs/TOOLS.md) |
| Regression gate per port (engine `tools/regress/gate.py`) | not wired up |
| CI (audit + engine tests + port config lint) | not started |

## P5 — Second title (Jet Grind Radio)

Entry gate: P1 exit met; the port template extracted from Soulcalibur.
