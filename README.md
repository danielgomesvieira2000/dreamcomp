# dreamcomp

A framework for native PC ports of Sega Dreamcast games, built for AI-assisted development.

dreamcomp statically recompiles a game's SH-4 code to C++ ahead of time, links it against emulated
Dreamcast hardware (PowerVR2 graphics, AICA sound with its ARM7, GD-ROM, Maple), and adds what a
*port* needs on top: per-game repositories, hooks into the game's own functions, settings,
widescreen and other enhancements, more platforms, and tooling to debug, profile and audit the
work. Every game becomes its own repository that holds configuration and original code only.

**You supply the game.** Nothing here contains Sega or publisher code or data, and nothing derived
from a disc is ever committed (see [docs/LEGAL.md](docs/LEGAL.md)).

## Status (2026-10-05)

| Port | Repository | State |
|---|---|---|
| Soulcalibur (USA, T1401N) | `soulcalibur-recomp` | Plays: intro, menus, arcade fights with correct graphics, music and effects, memory-card saves and loads. 4x real time on an Intel Iris Xe at 2x internal resolution (headroom for real-time play). Widescreen 16:9 / 21:9 / 32:9 at full resolution (HUD stretched). Players 1-4 on separate pads, rumble. Texture packs, file mods. 8 minutes of scripted play per seed with no faults. |

Platforms: Windows x64 is built and tested (clang-cl, Vulkan, Intel Iris Xe). Linux compiles
(g++ 15, headless verified); macOS is the engine's own development platform; Android is planned
(docs/PLATFORMS.md).

## How it fits together

```
dreamcomp/                     this repository (GPL-2.0)
  engine/                      dream-recomp, vendored as a git subtree: SH-4 -> C++ translator,
                               emulated hardware, Vulkan renderer, SDL3 window/audio
  include/dreamcomp/, src/     the port layer: hooks, launcher extension, settings
  cmake/DreamcompPort.cmake    dreamcomp_add_port() for port repositories
  tools/dc.py                  setup / build / run / screenshots / report
  tools/audit.py               no-game-data scanner, engine-change ledger, docs lint
  .claude/skills/              Claude Code workflows (new port, playtest, investigate, ...)
  docs/                        architecture, decisions, playbook, roadmap
ports/<slug>/                  each port is its own git repository (ignored here)
```

The engine is [dream-recomp](https://github.com/phobos665/dream-recomp) by phobos665. dreamcomp
pins it at a known commit and records every change made to it in
[docs/engine-changes.md](docs/engine-changes.md), so improvements can go back upstream.

## Quick start (Windows)

Needs: Visual Studio 2022 Build Tools, LLVM (clang-cl), CMake, Ninja, Python 3.10+, Vulkan SDK.

```powershell
python tools/dc.py doctor                       # what is missing
python tools/dc.py deps                         # SDL3 into .deps/
git clone <port repo> ports/soulcalibur-recomp
python tools/dc.py setup ports/soulcalibur-recomp --disc "D:\discs\Soulcalibur (USA).cue"
python tools/dc.py build ports/soulcalibur-recomp
python tools/dc.py run   ports/soulcalibur-recomp --window
```

`setup` checks that your disc is the exact release the port was made for (SHA-1 of the boot
executable) and extracts only that executable, which the translator needs at build time. Disc
images may be `.cue` (Redump), `.gdi` or `.chd`.

Started with no arguments, a port's executable opens a window with the saved settings. Settings
live in `%APPDATA%\dreamcomp\<port>\settings.ini` (macOS: Application Support, Linux:
`~/.config`). In the window: arrows = d-pad, Z X A S = A B X Y, Enter = Start, Q W = triggers,
F1 = HUD editor (anchors per element), F10 = FPS, F11 = frame capture, F12 = screenshot.

## Documentation

| | |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layers, data flow, extension points |
| [docs/PORTING-GUIDE.md](docs/PORTING-GUIDE.md) | Bringing up a new game, step by step |
| [docs/HOOKS.md](docs/HOOKS.md) | Replacing or wrapping game functions from a port |
| [docs/DECISIONS.md](docs/DECISIONS.md) | Why things are the way they are |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Phases, entry gates, what is next |
| [docs/AUDITING.md](docs/AUDITING.md) | What the audits check and why |
| [docs/playbook/README.md](docs/playbook/README.md) | Dreamcast traps learned the hard way, symptom first |
| [docs/LEGAL.md](docs/LEGAL.md) | What may and may not be published |
| [docs/engine-changes.md](docs/engine-changes.md) | Every change to the vendored engine |

## Credits

- [dream-recomp](https://github.com/phobos665/dream-recomp) (phobos665), the recompiler and
  runtime this is built on, itself drawing on [Flycast](https://github.com/flyinghead/flycast)
  (GPL-2.0).
- [libchdr](https://github.com/rtissera/libchdr), [toml++](https://github.com/marzer/tomlplusplus),
  [SDL3](https://libsdl.org), Vulkan. Full list: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- Widescreen addresses for several titles come from Flycast's widescreen cheat table.

## AI use

dreamcomp is developed with Claude Code (Anthropic) as an explicit part of the method: the
skills, docs and audits in this repository exist so that an AI agent can carry out port work
reliably and leave a reviewable record. Code is reviewed and tested by the maintainer; commits
made with AI assistance carry a `Co-Authored-By` trailer.

## Licence

GPL-2.0 ([LICENSE](LICENSE)), the engine's licence. Port repositories that build on dreamcomp are
GPL-2.0 as well.
