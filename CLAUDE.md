# dreamcomp — instructions for Claude

Framework for native PC ports of Dreamcast games: `engine/` (vendored dream-recomp: SH-4 → C++
translator + emulated hardware) plus dreamcomp's port layer, tools, docs and skills. Ports are
separate git repos in `ports/<slug>/`. Start with `docs/ARCHITECTURE.md` and `docs/ROADMAP.md`.

## Hard rules

- **No game data, ever**, in any commit of any repo: no disc files, extracted/converted assets,
  generated code (`gen/`), BIOS/flash/VMU images, SDK material. `python tools/audit.py all` must
  pass (pre-commit hook installed with `audit.py install-hook`). `docs/LEGAL.md` has the rules.
- **Every change under `engine/`** gets a row in `docs/engine-changes.md` in the same commit (the
  `engine` audit fails otherwise). Prefer generic, upstreamable engine changes; game-specific code
  belongs in the port.
- Never hand-edit generated code; change the TOML config, hooks, or the translator.
- Never push to the upstream engine repository; push only to `danielgomesvieira2000/*`, and only
  when asked.
- One verified change at a time. A fix is done when the symptom is shown gone in the reported
  condition (screenshots via `tools/dc.py shots`, reports via `tools/dc.py report`), not when a
  counter moves. Say plainly what was not verified.
- Test runs never leave choices in the player's settings: use flags/`--set`, not
  `--save-settings`.
- Python heredocs eat backslashes in this shell: write patch scripts with the Write tool, and
  assert every scripted replacement matched.

## Workflow

| Task | Skill / command |
|---|---|
| New game | `/dc-new-port`; guide `docs/PORTING-GUIDE.md` |
| Build / run / look | `/dc-playtest`; `python tools/dc.py build|run|report|shots <port>` |
| Bug | `/dc-investigate`; traps first: `docs/playbook/README.md` |
| Widescreen, textures, mods, fps | `/dc-enhance`; hooks: `docs/HOOKS.md` |
| HUD element anchors (F1 editor, promote) | `/dc-hud`; `docs/HUD.md` |
| Engine upstream sync | `/dc-engine-sync` |
| Audit / release | `/dc-audit`, `/dc-release` (release only when asked) |
| End of session | `/dc-retro` |

## Docs contract

Decisions → `docs/DECISIONS.md` (append-only). Cross-game traps → `docs/playbook/`. Port facts →
the port's `docs/GAME-INTERNALS.md` / `docs/PORTING.md`. Status → `docs/ROADMAP.md` and
`docs/ports.md`. Docs change in the same commit as the code they describe. Style: direct,
technical, tables, short sections; negative results kept; inference marked as inference.
