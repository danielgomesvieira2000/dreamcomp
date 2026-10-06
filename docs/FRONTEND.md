# Frontend

What a player sees around the game, laid out like Daniel's N64 recomp ports (RecompFrontend
style, reproduced in dreamcomp's own RML/RCSS; none of RecompFrontend's files are used). Built with
[RmlUi](https://github.com/mikke89/RmlUi) (Daniel's choice).

| Part | Where | What |
|---|---|---|
| Launcher | its own window before the game (SDL_Renderer) | banded backdrop (or the port's art), title, centred list: **Load Game** (→ **Start Game** once a verified disc is loaded), **Controls**, **Settings**, **Mods**, **Quit**; disc status in one line under the list; version bottom-left |
| Settings panel | over the launcher, and **in game over the running game** (rendered at window size, blended on the GPU) | large translucent modal: tabs **General / Controls / Graphics / Sound / Mods**, **Quit Game** (asks "Quit Soulcalibur?", Cancel focused) and **×** top right; option rows left, the focused/hovered option's description right; prompts and **Apply** at the bottom |

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
| Controls | RecompFrontend's layout, below | bindings: at once |
| Graphics | Resolution 1–8×, Aspect ratio (Original / Expanded, as in the N64 recomps; Expanded only with `PortInfo::widescreen`), Window mode, HUD layout (Original = 4:3 centred / Expanded = anchored to the edges; never stretched; with `PortInfo::hud`) | aspect, window mode, HUD live; resolution next start |

**Aspect ratio.** `aspect = original`: the console's 4:3 picture, black bars when the window is
wider or taller, never stretched. `aspect = expanded`: the game's view is widened to the window's
own shape (between 4:3 and `PortInfo::max_aspect`; bars only outside that range); resizing or going
fullscreen rebuilds the render target at the new shape once the window has held it for 15 frames
(`Core::follow_window`, `HostControls::set_render_aspect`). There is no stretch or crop mode any
more. Older settings values read as: `4:3` -> original, any other ratio -> expanded.
| Sound | Master volume, Loud sounds (Original = the console's hard clipping / Softened = `--soft-clip`) | live |
| Mods | Texture pack, Open texture folder, Dump textures, mods list (on/off, ▲▼ order, first wins), Open mods folder | next start |

Option rows copy RecompFrontend's radio rows: the name, then every choice in a row; the selected
one white and underlined, the others dim, unavailable ones greyed out (aspect ratios beyond
`PortInfo::max_aspect`; HUD layout while the HUD fix is off or the picture is 4:3). Up/down moves to
the nearest row, onto its selected choice.

**Values take effect on Apply.** There is no discard: closing the panel keeps unapplied edits,
marked with a dot and "Not applied yet", for next time. In game, rows that only take effect at the
next start carry a *Next start* badge and say so in the description. Loading a disc is an action,
not a setting: a verified disc is remembered at once; a wrong release or unreadable image is
explained in a message and not kept.

### Controls tab

Copied from the N64 ports' RecompFrontend Controls tab (Wave Race 64 configuration):

| View | Contents |
|---|---|
| Player cards | one box per player (`PortInfo::players`, Soulcalibur 2): "Player N", a large PromptFont device glyph (gamepad U+243C / keyboard U+243D), the device name; **Edit Profile** (disabled with no controller). Player 1 = keyboard + controller 1; player N = controller N, in connection order |
| Mappings | "Editing: Keyboard/Controller profile" + **Go back**; one row per Dreamcast input (Analog Up/Down/Left/Right, A, B, X, Y, L/R Trigger, Start, D-Pad ×4) with **two binding boxes** (either one drives the input) and a round ✗ that clears both; footer: controller/keyboard switch and **Reset to defaults** |

Bindings show as PromptFont glyphs with RecompFrontend's code-point table
(`recompinput/src/input_types.cpp`: Xbox-style face buttons, A = U+21A7, sticks, triggers, keyboard
letters as U+FF21…, F1–F12 as U+2460…, PromptFont's swapped up/right arrows); a key PromptFont has
no glyph for shows its name. Click a box (or Enter / A on it): it turns red and the next key
(keyboard profile) or button / stick direction past half travel (controller profile) is bound;
Escape, a pad's Select, or a click elsewhere cancels. Changes are written to the bindings file at
once (the engine's per-user `bindings.txt`, or `--bindings`) and the running game reloads it
(`HostControls::reload_bindings`). No Apply, as in RecompFrontend. Bindings are per device type, not
per player: every controller uses the controller profile. The menu keys (Escape / Select) are fixed.
The engine's F1 binding screen still exists and edits the first boxes.

### Input

| Action | Keyboard | Gamepad | Mouse |
|---|---|---|---|
| Move | arrows | d-pad / left stick (repeats) | hover (moves the description) |
| Choose an option | ← → between the choices, Enter selects | d-pad ← →, A selects | click the choice (greyed ones are unavailable) |
| Slider | ← → on the row | d-pad ← → | click on the bar |
| Activate | Enter / Space | A | click |
| Close / back / cancel | Esc / Backspace | B (in game also Select) | × |
| Tabs | Q / E | LB / RB | click |
| Start Game (launcher) | — | Start | — |

Focus moves by the frontend's own spatial search (RmlUi's `nav: auto` does not cross scroll
containers) in regions: launcher list; panel tab bar, rows, footer; dialog buttons.

## Settings keys

`disc`, `scale`, `aspect` (`original`/`expanded`), `fullscreen`, `fps`, `rumble`, `volume`, `clip`, `hud_fix`,
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

**Threads.** With a window, the main thread only pumps SDL events (`Window::pump_events`) and
the title runs on its own thread, so moving or resizing the window (a modal loop on Windows) does
not stop the game; `DREAM_SINGLE_THREAD=1` puts everything back on one thread. The in-game menu has
a third thread of its own: RmlUi input, layout and rasterising (10-15 ms per redraw at window size)
run there, so navigating the menu never costs the game a frame. The game thread only queues events
(`Overlay::on_event`, deciding by event type whether the menu takes them), picks up the newest
finished image (`render_window`, `draw` for test screenshots) and runs what the menu asks of the
game -- Apply (`OverlayContext::applied`), Quit, the binding screen, reloading bindings -- at its
next vblank or present. `Settings` has a lock for the two threads.

**Cost while open** (Iris Xe): the panel is blended over the game on the GPU and redrawn on the
overlay thread only when it changes. Fight scenario with 40 key presses in the open panel: frames
over 20 ms 48 -> 4 after moving RmlUi off the game thread (the rest: first open, stage load).

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

- RecompFrontend's focus pulse, recording-dot pulse and icon SVGs are not reproduced (static
  colours; PromptFont glyphs stand in for the unlicensed SVGs). No "Assign players" dialog: players
  follow connection order. No per-player profiles.
- Gamepad navigation verified with synthetic SDL events only, not a physical pad; input blocking
  while the panel is open is verified by code path, not with a held physical key.
