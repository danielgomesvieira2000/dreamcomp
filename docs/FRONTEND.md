# Frontend (launcher)

The window a player sees when they double-click a port's executable. They pick and verify their
disc, choose settings, and press **Start Game**; the launcher closes and the game opens in its own
Vulkan window with those settings. Built with [RmlUi](https://github.com/mikke89/RmlUi) on SDL3
(RmlUi was Daniel's choice).

## Using it

| Start | What happens |
|---|---|
| no arguments (double-click) | launcher, then the game with the saved settings |
| `--launcher [flags]` | the same, keeping the other flags (e.g. `--settings`, `--vmu`) |
| `--no-launcher` or `DREAMCOMP_NO_LAUNCHER=1` | straight into the game (the old bare launch) |
| any other flags | no launcher, as before (test runs, `dc.py run/report/shots`) |
| `--launcher-screenshot FILE.png [--launcher-tab NAME] [--launcher-size WxH]` | renders each tab (or one) to `FILE-<tab>.png` / `FILE.png` and exits; nothing is saved |

| Tab | Contents |
|---|---|
| Play | game title / product / region (from the TOML), disc status and path, **Select disc image…** (native dialog: `.cue`, `.gdi`, `.chd`), memory card path |
| Graphics | internal resolution 1–8× with the resulting size (`2× — 1707×960` at 16:9), aspect ratio (4:3 / 16:9 / 21:9 / 32:9 up to `PortInfo::max_aspect`; only when the port implements `PortInfo::widescreen`), fit (Crop / Letterbox / Stretch), fullscreen |
| Controls | rumble strength 0–100 % (0 = off), where to rebind (F1 / pad Select in game), connected pads and the player each drives (pad 1 = P1 with the keyboard, pad 2 = P2, …) |
| Enhancements | texture pack on/off + Open texture folder, dump textures, mods (`<settings dir>/mods/*`: on/off and order, first wins) |
| About | credits, licences, "you need your own disc", Read me (local `README.md` next to the exe, else the dreamcomp README on GitHub) |

Disc check: the image is opened with `gdrom::open_disc`, the boot file named in IP.BIN is read
(`read_root_file`) and its SHA-1 compared with the TOML's `[disc] sha1_1st_read`. Results:
**Disc verified**, **Wrong release or revision** (both hashes shown), **Cannot read this image**.
**Start Game** is enabled only for a verified disc. The check runs on a worker thread.

**Settings apply on Apply** (or Start Game, which applies first), never when a value is clicked.
Changed rows carry a blue dot and the footer says *Unsaved changes*; Quit discards them.

### Input

| Action | Keyboard | Gamepad | Mouse |
|---|---|---|---|
| Move focus | arrows | d-pad / left stick (repeats when held) | hover |
| Change a value | ← → on the row | d-pad ← → | click the row or its ‹ › |
| Activate | Enter / Space | A | click |
| Back | Esc / Backspace (content → tab bar → Quit) | B | — |
| Switch tab | Q / E | LB / RB | click the tab |
| Start Game | — | Start | Start Game |

Prompts in the footer use PromptFont glyphs and follow the device: pad glyphs when a pad is
connected, keys otherwise. Focus movement is the launcher's own spatial search (RmlUi's
`nav: auto` does not cross the scrolling content area): left/right stay in a region, up/down move
between tab bar, content and footer.

## Settings keys it writes

`disc`, `scale`, `aspect`, `fit`, `fullscreen`, `rumble`, `texture_pack`, `dump_textures`,
`mods` — the same keys the core turns into engine flags (ARCHITECTURE.md § Settings). `vmu` is
shown, not edited.

## How it is built

| Piece | Where | Notes |
|---|---|---|
| CMake | `cmake/DreamcompFrontend.cmake`, included by the root `CMakeLists.txt` when `DREAMCOMP_FRONTEND` (default ON), SDL3 is found and the Vulkan renderer is built | headless builds (`-DDREAM_RENDERER=OFF`, or no SDL3) configure without it and start the game directly; `DREAMCOMP_WITH_FRONTEND` is defined for `dreamcomp_core` |
| FreeType `VER-2-14-3`, RmlUi `6.3` | `FetchContent` at configure time (shallow, pinned) into `<build>/_deps/` | static; FreeType without zlib/bzip2/png/harfbuzz/brotli; `Freetype::Freetype` is an alias of the fetched target |
| RmlUi SDL3 backends | `Backends/RmlUi_Platform_SDL.cpp`, `RmlUi_Renderer_SDL.cpp` compiled into `dreamcomp_frontend` | `RMLUI_SDL_VERSION_MAJOR=3` |
| SDL_image | not used: `src/frontend/shim/SDL3_image/SDL_image.h` + `sdl_image_shim.cpp` implement `IMG_LoadTyped_IO` for PNG with the engine's `dream::render::png` | RGBA bytes = `SDL_PIXELFORMAT_ABGR8888` |
| Fonts | Inter 4.1 (Regular/SemiBold/Bold) and PromptFont 1.10, downloaded with SHA-256 checks into `<build>/dreamcomp_fonts/` | OFL-1.1, licence files shipped beside them |
| Page | `frontend/launcher.rml` (skeleton), `frontend/dreamcomp.rcss` (theme) | tab contents are generated in `src/frontend/launcher.cpp` |
| Logic | `src/frontend/launcher_model.{h,cpp}` (draft/Apply, option tables, disc check, mods) | tested by `dreamcomp_launcher_tests` |
| Assets next to the exe | `dreamcomp_add_port` POST_BUILD copies `frontend/` and the fonts to `<exe dir>/frontend/` | a release package must include that folder |

Nothing third-party is committed. The launcher's SDL window, renderer and the SDL subsystems it
started are destroyed before `adjust_args` returns, so the engine opens its own window as before.

Tests: `cmake --build <port>/build --target dreamcomp_launcher_tests`, then
`<port>/build/dreamcomp/dreamcomp_launcher_tests.exe <scratch dir> [<disc image> <sha1>]`.

Test aids (environment, for automated checks only): `DREAMCOMP_LAUNCHER_DRAFT="scale=4;mods=a,b"`
(unapplied values in a screenshot), `DREAMCOMP_LAUNCHER_FOCUS=<element id>`,
`DREAMCOMP_LAUNCHER_PADS="Name;Name"` (screenshot only), `DREAMCOMP_LAUNCHER_KEYS="e,down,right,
enter,pad:rb,pad:a,pad:start"` (scripted key/pad presses through the real handlers in the live
window; prints the focus after each; quits 1.5 s after the last).

## Customising it in a port

| What | How |
|---|---|
| Title | `PortInfo::title` (else the TOML's `[game] title`); subtitle from `[game] product` / `region` |
| Widescreen choices | `PortInfo::widescreen` and `max_aspect` |
| Art, colours | `dreamcomp_add_port(... LAUNCHER_DIR ${PROJECT_SOURCE_DIR}/launcher)`; its files are copied over `<exe dir>/frontend/`. `art.png` appears at the right of the header; `port.rcss` is linked after `dreamcomp.rcss`, so any rule can be overridden (e.g. `.tab.active { border-bottom-color: #ff8a3d; }`) |

Port art must be the port author's own work, never extracted from the disc (LEGAL.md).

## Not done yet (phase 1 limits)

- No in-game overlay: the launcher runs only before the game.
- Bindings are still edited in game (F1); the launcher only points there.
- Pad hot-plug updates the Controls list; player assignment is fixed by connection order (the
  engine's rule), not editable.
- Gamepad navigation was verified with synthetic SDL events (`DREAMCOMP_LAUNCHER_KEYS=pad:*`), not
  with a physical pad.
