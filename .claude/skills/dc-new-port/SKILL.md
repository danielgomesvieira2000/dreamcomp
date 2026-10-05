---
name: dc-new-port
description: Start a new Dreamcast PC port with dreamcomp (survey the disc, create the port repo, first boot loop to zero untranslated targets, plan with gates). Use when Daniel asks to create/start/plan a port or recomp of a Dreamcast game.
argument-hint: "<game title> [disc path] [autonomous]"
---

# New Dreamcast port: $ARGUMENTS

Root: the dreamcomp checkout (below: `DC`). Ports live in `DC/ports/<slug>/`, each its own git repo.

## 0. Load what is already known (before planning)

Read `DC/docs/PORTING-GUIDE.md`, `DC/docs/playbook/README.md` (every trap), `DC/docs/ports.md`
(what other ports settled on), `DC/docs/DECISIONS.md`. Do not re-derive what they answer; cite the
playbook entry in the plan when one applies. The reference port is `soulcalibur-recomp`: copy its
repo shape, `game/CMakeLists.txt`, `src/<id>.cpp` registration and docs layout literally.

## 1. Survey (read-only) -> `docs/findings/phase-00.md`

| Question | How |
|---|---|
| Identity: product, version, region, boot file, its SHA-1 | `python -m dcdisc info <disc>` (PYTHONPATH `DC/engine/tools/dcdisc`) |
| Overlays / extra code files | `dcdisc ls`; `dcdisc scan FILE` on suspicious `.BIN`s; more than one code file = `[[relocations]]`/overlays work |
| SDK and versions | strings on the boot file: Shinobi/Ninja/Kamui/sd banners |
| Windows CE? | refuse: out of scope (`dcdisc info` says) |
| Existing decomp / RE / widescreen value | search GitHub; Flycast `core/cheats.cpp` widescreen table by product number |
| Several discs of Daniel's? | only the one asked for |

## 2. Repository

Create `DC/ports/<slug>/` (`<full-title>-recomp`) from the reference port; `git init -b main`;
`python DC/tools/audit.py install-hook --repo DC/ports/<slug>`; create a git-ignored
`CLAUDE.local.md` whose first line is `@../../CLAUDE.md` so every session loads the framework rules.
Write `docs/PLAN.md` with phases, **entry gates**, exit criteria that are visible behaviour, a
Decisions log and a Verified checkpoints table.

Unless Daniel said autonomous, present the plan and wait. Autonomous: take the recommended option
at each decision, log it in the plan, continue.

## 3. Boot loop (repeat until clean)

```
python DC/tools/dc.py setup  DC/ports/<slug> --disc "<disc>"
python DC/tools/dc.py build  DC/ports/<slug>
python DC/tools/dc.py report DC/ports/<slug> --frames 1800 --press start@600 -- --suggest-config DC/ports/<slug>/game/suggested.toml
```

Paste suggestions into `game/<id>.toml` with a comment saying how each was found; rebuild. Done
when `untranslated call targets: 0`, `unmapped memory accesses: 0` across menus **and** gameplay
(use `--press` scripts to get there). Then `dc.py shots` and look at the sheet.

## 4. Record

Facts about the game -> `docs/GAME-INTERNALS.md`; toolchain facts -> `docs/PORTING.md`; same commit.
Register the port in `DC/docs/ports.md`. Engine bugs found -> fix in `DC/engine`, log in
`DC/docs/engine-changes.md`, add a playbook entry. End with `/dc-retro`.

GitHub: create the repo only when Daniel asks (`gh repo create danielgomesvieira2000/<slug>`).
