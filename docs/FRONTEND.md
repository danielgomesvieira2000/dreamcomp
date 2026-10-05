# Frontend

What a player sees around the game, laid out like Daniel's N64 recomp ports (RecompFrontend
style, reproduced in dreamcomp's own RML/RCSS; none of RecompFrontend's files are used). Built with
[RmlUi](https://github.com/mikke89/RmlUi) (Daniel's choice).

| Part | Where | What |
|---|---|---|
| Launcher | its own window before the game (SDL_Renderer) | banded backdrop (or the port's art), title, centred list: **Load Game** (→ **Start Game** once a verified disc is loaded), **Controls**, **Settings**, **Mods**, **Quit**; disc status in one line under the list; version bottom-left |
| Settings panel | over the launcher, and **in game over the running game** (CPU renderer composited into the frame) | large translucent modal: tabs **General / Controls / Graphics / Sound / Mods**, **Quit Game** (asks "Quit Soulcalibur?", Cancel focused) and **×** top right; option rows left, the focused/hovered option's description right; prompts and **Apply** at the bottom |

## Using it

| Start | What happens |
|---|---|
| no arguments (double-click) | launcher, then the game with the saved settings |
| `--launcher [flags]` | the same, keeping the other flags (e.g. `--settings`, `--vmu`) |
| `--no-launcher` or `DREAMCOMP_NO_LAUNCHER=1` | straight into the game |
| other flags | no launcher (test runs, `dc.py run/report/shots`); the in-game panel still works |
| `--launcher-screenshot F.png [--launcher-screen NAME] [--launcher-size WxH]` | renders `launcher` and `settings/<tab>` to `F-<screen>.png` (or `F.png` for one) and exits; nothing is saved |

In game: **Escape** or a pad's **Select/Back** opens and closes the panel (on the tab used last,
General at first). **The game keeps running**; while the panel is open the keyboard and pads drive
the panel and the game sees its buttons released. Start stays the game's. F1 still opens the
engine's binding screen. Escape no longer quits; Quit Game (confirmed) or closing the window does,
through the normal stop path (report, settings, memory card). `DREAMCOMP_NO_OVERLAY=1` turns the
in-game panel off.

### Tabs

| Tab | Rows | Applies in game |
|---|---|---|
| General | Rumble strength (slider, 0 = off), Frame rate (Auto/120/60), About (credits; opens the read-me) | rumble live; frame rate next start |
| Controls | Rebind buttons… (in game: opens the binding screen; before the game: explains it is in game / F1), players (keyboard + pads, pad 1 = P1 with the keyboard) | — |
| Graphics | Resolution 1–8×, Aspect ratio (≤ `PortInfo::max_aspect`, only with `PortInfo::widescreen`), Fit to window, Fullscreen, Widescreen HUD, HUD layout (with `PortInfo::hud`) | fit, fullscreen, HUD live; resolution/aspect next start |
| Sound | Master volume | live |
| Mods | Texture pack, Open texture folder, Dump textures, mods list (on/off, ▲▼ order, first wins), Open mods folder | next start |

**Values take effect on Apply.** There is no discard: closing the panel keeps unapplied edits,
marked with a dot and "Not applied yet", for next time. In game, rows that only take effect at the
next start carry a *Next start* badge and say so in the description. Loading a disc is an action,
not a setting: a verified disc is remembered at once; a wrong release or unreadable image is
explained in a message and not kept.

### Input

| Action | Keyboard | Gamepad | Mouse |
|---|---|---|---|
| Move | arrows | d-pad / left stick (repeats) | hover |
| Change a value | ← → on the row | d-pad ← → | click the row or its ‹ › / the slider |
| Activate | Enter / Space | A | click |
| Close / back / cancel | Esc / Backspace | B (in game also Select) | × |
| Tabs | Q / E | LB / RB | click |
| Start Game (launcher) | — | Start | — |

Focus moves by the frontend's own spatial search (RmlUi's `nav: auto` does not cross scroll
containers) in regions: launcher list; panel tab bar, rows, footer; dialog buttons.

## Settings keys

`disc`, `scale`, `aspect`, `fit`, `fullscreen`, `fps`, `rumble`, `volume`, `hud_fix`,
`hud_layout`, `texture_pack`, `dump_textures`, `mods` (ARCHITECTURE.md § Settings).

## How it is built

| Piece | Where | Notes |
|---|---|---|
| CMake | `cmake/DreamcompFrontend.cmake`, from the root `CMakeLists.txt` when `DREAMCOMP_FRONTEND` (ON), SDL3 is found and the Vulkan renderer is built | otherwise the game starts directly; `DREAMCOMP_WITH_FRONTEND` is defined for `dreamcomp_core` |
| FreeType `VER-2-14-3`, RmlUi `6.3` | `FetchContent` at configure time (shallow, pinned) | static; `Freetype::Freetype` aliases the fetched target |
| Fonts | Inter 4.1, PromptFont 1.10, downloaded with SHA-256 checks | OFL-1.1, licences shipped beside them |
| Page | `frontend/launcher.rml` (skeleton), `frontend/dreamcomp.rcss` (theme) | screens generated in `src/frontend/menu_ui.cpp` (`MenuUi`, one class for both hosts) |
| Pre-game host | `src/frontend/launcher.cpp` | SDL window + RmlUi's SDL_Renderer backend; file dialog; screenshots |
| In-game host | `src/frontend/overlay.cpp`, `soft_render.cpp` | RmlUi re-initialised with a CPU RenderInterface; laid out in the part of the frame the window shows (fit mode), 1 dp = 1/720 of it |
| Logic | `src/frontend/launcher_model.{h,cpp}` | `dreamcomp_launcher_tests` |
| Engine hooks | `host_ext.h` overlay methods + `HostControls`; `Window::event_filter`, `set_pad_menu_button`, `set_game_input_blocked`, `request_close`; `--volume` | generic; ledger rows in engine-changes.md |
| Version | `dreamcomp_add_port` generates `<id>_port_version.cpp` from the port project's VERSION | shown bottom-left |

**Threads.** The overlay's entry points share one mutex. `on_event` runs inside the engine
window's event poll (`Window::poll`, called from `Live::present`); `draw` inside the present
(`Live::present`, compositing into a host copy of the frame); `on_vblank` on the guest thread
(test script only); Apply calls `HostControls` (window: rumble scale, fullscreen; audio: volume) and
`present_options()`. Nothing else assumes the guest and the window share a thread.

**Cost while open** (Iris Xe, 1707×960 frame): composite ≈ 6 ms per frame, a UI re-render ≈ 11 ms
on change; frames take the host-copy path instead of direct present. At scale 1 the game holds
60 fps with the panel open; at scale 2 this GPU is below real time with or without the panel.

## Automated checks

`--launcher-screenshot` as above, plus environment test aids:

| Variable | Where | Effect |
|---|---|---|
| `DREAMCOMP_LAUNCHER_KEYS="down,enter,pad:a,click:<id>,shot:<png>,..."` | launcher window | scripted input through the real handlers, 150 ms apart; quits 1.5 s after the last |
| `DREAMCOMP_OVERLAY_KEYS="@FRAME,esc,wait,shot:<png>,pad:rb,click:<id>,..."` | in game | from guest frame FRAME, pushes SDL events into the window's queue (same path as real input); `shot:` writes the composited frame |
| `DREAMCOMP_LAUNCHER_DRAFT="scale=4;mods=a,b"`, `DREAMCOMP_LAUNCHER_PADS="Name;Name"`, `DREAMCOMP_LAUNCHER_FOCUS=<id>` | screenshots | unapplied values, stand-in pads, focus |
| `DREAMCOMP_OVERLAY_PROFILE=1` | in game | composite / render times every 2 s |

## Customising it in a port

| What | How |
|---|---|
| Title, subtitle | `PortInfo::title` (else `[game] title`); `[game] product` / `region` |
| Version | the port project's `VERSION` |
| Art, colours | `dreamcomp_add_port(... LAUNCHER_DIR <dir>)`: `art.png` is drawn over the backdrop; `port.rcss` is linked after `dreamcomp.rcss` |

Port art must be the port author's own work, never extracted from the disc (LEGAL.md).

## Not done

- Window move/resize still stalls the game (guest and window share a thread; the coordinator is
  moving the guest to its own thread).
- Rebinding happens in the engine's own screen, not in the panel.
- Gamepad navigation verified with synthetic SDL events only, not a physical pad; input blocking
  while the panel is open is verified by code path, not with a held physical key.
