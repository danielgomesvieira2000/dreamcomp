# Architecture

## Layers

| Layer | Where | What it does | Licence |
|---|---|---|---|
| Disc tooling | `engine/tools/dcdisc/` | Read `.cue`/`.gdi`/`.chd`, IP.BIN, ISO9660; extract the boot executable | GPL-2.0 (engine) |
| Translator | `engine/translator/` (`dream-translate`) | Discover functions in the boot executable, emit one C++ function per guest function; FPSCR mode inference, delay slots, interrupt polls at entries/back-edges, `[hooks]` | MIT intent per engine ADR 1, in a GPL tree |
| Runtime | `engine/runtime/` | Guest memory map, SH-4 context, scheduler, Holly interrupts, PVR2 TA/SPG, AICA + ARM7, GD-ROM and BIOS HLE, Maple, dev interpreter fallback | GPL-2.0 |
| Renderer / window / audio | `engine/render/`, `engine/audio/` | Vulkan PVR2 renderer, presenter, SDL3 window + input, SDL3 audio sink | GPL-2.0 |
| Launcher | `engine/runtime/boot/boot_main.cpp` | `main()`: flags, wiring, pacing, report | GPL-2.0 |
| **Frontend** | `src/frontend/`, `frontend/` (`dreamcomp_frontend`, optional) | The launcher before the game (disc check, Load/Start Game) and the settings panel, also over the running game on Escape / pad Select ([FRONTEND.md](FRONTEND.md)); RmlUi + FreeType fetched at configure time | GPL-2.0 (RmlUi MIT, FreeType FTL/GPL-2.0) |
| **dreamcomp port layer** | `include/dreamcomp/`, `src/` (`dreamcomp_core`) | Launcher extension: settings, bare-launch defaults, widescreen, presentation; port API (`PortInfo`); hook macros | GPL-2.0 |
| **Port** | `ports/<slug>/` (own repo) | `game/<id>.toml`, port sources (`PortInfo`, hooks, enhancements), docs | GPL-2.0 |

## Build data flow

```
player's disc (.cue/.gdi/.chd)
   |  tools/dc.py setup: verify sha1 of boot file, extract only it
   v
ports/<slug>/game/extracted/fs/1ST_READ.BIN          (git-ignored)
   |  dream-translate game --config game/<id>.toml   (custom command in dream_add_game)
   v
build/game/gen/<id>.cpp, <id>_reloc_*.cpp            (generated, never edited, never committed)
   |  + engine runtime + dreamcomp_core + port sources
   v
<slug>.exe   --disc <image> at run time (GD-ROM reads, boot file re-verified)
```

## Run-time extension points

| Point | Mechanism | Used for |
|---|---|---|
| A guest function | `[hooks]` in the TOML + `DC_HOOK_ENTRY/EXIT` in port code ([HOOKS.md](HOOKS.md)) | Replace or wrap game code: widescreen projection, HUD, camera, cheats, mods |
| Command line | `dream::host::Extension::adjust_args` / `parse_arg` | Settings-driven defaults; new flags |
| Boot | `Extension::on_start`, `PortInfo::on_start` | Apply settings, install patches |
| Each vblank | `Extension::on_vblank`, `PortInfo::widescreen`, `PortInfo::on_vblank` | Re-assert RAM values the game recomputes |
| Presentation | `dream::render::vk::present_options()` | Fit mode, anamorphic display aspect |
| Stop | `Extension::on_stop` | Save settings |

All callbacks run on the guest thread. The guest is single-threaded and deterministic for a given
`--rtc-seed` and input script, which is what makes `report`/`shots` runs comparable.

## Settings

`Settings` (`include/dreamcomp/settings.h`) is a `key = value` file per port in the platform
config directory. The launcher ([FRONTEND.md](FRONTEND.md)) edits it and writes it on Apply /
Start Game. Keys in use:

| Key | Values (default) | Engine flag added by `src/core.cpp` |
|---|---|---|
| `disc` | image path | `--disc` |
| `scale` | 1-8 (2) | `--scale` (launcher / bare launch) |
| `aspect` | `4:3`, `16:9` (default), `21:9`, `32:9`; capped by `PortInfo::max_aspect` | `--render-aspect` (anamorphic ports) |
| `fit` | `crop` (default), `letterbox`, `stretch` | presenter option |
| `fullscreen` | `true`/`false` (false) | `--fullscreen` |
| `vmu` | card path (`<config dir>/vmu_a1.bin`) | `--vmu` (launcher / bare launch) |
| `rumble` | 0-100 (100); 0 = off | `--rumble N` |
| `volume` | 0-100 (100) | `--volume N` |
| `fps` | `auto` (default), `120`, `60` | `--interpolate-auto` / `--interpolate` |
| `hud_fix`, `hud_layout` | `true` / `edges` (default), `center` | read by the core's HUD correction ([HUD.md](HUD.md)) |
| `sharp_2d` | `true` (default) / `false` | 1:1 2D quads point sampled when upscaled ([HUD.md](HUD.md) "Sharp 2D") |
| `texture_pack` | directory, `off`, unset = `<settings dir>/textures` if present | `--texture-pack` ([TEXTURE-PACKS.md](TEXTURE-PACKS.md)) |
| `dump_textures` | `true`/`false` | `--dump-textures <settings dir>/texture_dump` |
| `mods` | `name1,name2` (first wins) | `--mod <settings dir>/mods/<name>` each ([MODS.md](MODS.md)) |

Command-line flags override for one run and are only
saved with `--save-settings`, so test runs never leave choices behind in the player's file.

## Why recompilation and not emulation

The CPU is translated ahead of time: the game's logic runs as native code the C++ compiler
optimises, and individual functions can be replaced in C++ (hooks). The hardware the game drives
directly (PowerVR2, AICA) stays emulated at register level, because that is where games depend on
exact behaviour; the BIOS is replaced by native code so no BIOS image is needed. An SH-4
interpreter remains as a fallback for code discovery missed (D-003).
