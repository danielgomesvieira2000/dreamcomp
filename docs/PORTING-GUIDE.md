# Bringing up a new port

The skill `/dc-new-port` walks Claude through exactly this; humans can follow it by hand. The
engine's own guide (`engine/docs/per-game-bring-up.md`) covers translator internals in depth.

## 0. Survey (read-only)

```powershell
.venv\Scripts\python -m dcdisc info "<disc>.cue"      # PYTHONPATH=engine/tools/dcdisc
.venv\Scripts\python -m dcdisc ls   "<disc>.cue"
```

Record in the port's `docs/findings/phase-00.md`: product number and version, boot file name and
SHA-1, SDK banners (`strings` on the boot file: Shinobi/Ninja/Kamui versions), whether other
files look like code (overlays), Windows CE (out of scope), existing decomps, Flycast widescreen
table entry, VGA support.

## 1. Repository

```
ports/<slug>/                   git init -b main; slug = <full-title>-recomp
  CMakeLists.txt                add_subdirectory(${DREAMCOMP_DIR} dreamcomp); add_subdirectory(game)
  game/<id>.toml                [game] [disc] sha1_1st_read [binary] -- copy soulcalibur's shape
  game/CMakeLists.txt           dreamcomp_add_port(<id> TITLE ... OUTPUT_NAME <slug> SOURCES ...)
  src/<id>.cpp                  PortInfo registration
  docs/PLAN.md, docs/GAME-INTERNALS.md, docs/findings/
  .gitignore (no-game-data banner), LICENSE (GPL-2.0), README.md, CLAUDE.md
```

Then `python tools/audit.py install-hook --repo ports/<slug>`.

## 2. First boot loop

```powershell
python tools/dc.py setup  ports/<slug> --disc "<disc>"
python tools/dc.py build  ports/<slug>
python tools/dc.py report ports/<slug> --frames 1800 -- --suggest-config ports/<slug>/game/suggested.toml
```

Paste `[[relocations]]` / `[functions] extra` from `suggested.toml` into the config, rebuild,
repeat until `untranslated call targets: 0` and `unmapped memory accesses: 0`.

Read the report every time: `PVR ... renders`, `AICA ... key-ons`, `GD-ROM sectors read`,
`maple ... pad polls` say which subsystems the game is actually using.

## 3. See it

```powershell
python tools/dc.py shots ports/<slug> --at 120,300,600,900,1200 --press start@600
```

Look at `shots/sheet.png`. Scripted input: `--press a@1200,start@900,...` (buttons: a b c x y z
start up down left right; each held 8 frames).

## 4. Get it right

Symptom-first triage: `docs/playbook/README.md`. Disciplined investigation: `/dc-investigate`.
Engine bugs are fixed in `engine/` and logged in `docs/engine-changes.md` (the audit fails
otherwise); a port-specific workaround goes in the port's sources.

## 5. Enhance

Widescreen via `PortInfo::widescreen` (start from Flycast's `core/cheats.cpp` table if the title
is there), then hooks ([HOOKS.md](HOOKS.md)). One verified change at a time, each behind a setting
that is **on** once verified.
