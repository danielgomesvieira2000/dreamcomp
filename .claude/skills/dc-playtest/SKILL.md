---
name: dc-playtest
description: Build a dreamcomp port and launch it for Daniel to play, or capture automated screenshots/reports, then read the evidence when he says "take a look". Use when Daniel asks to build/run/start a Dreamcast port so he can test, or wants Claude to look at what is on screen.
argument-hint: "[port] [what to test | take a look]"
---

# Playtest: $ARGUMENTS

Paths, exe names and keys: `DC/docs/ports.md` for this port. Do not guess.

## A. Build

1. Close running instances of the port's exe first (a running exe locks the binary).
2. `python DC/tools/dc.py build DC/ports/<slug>`.
3. If the change touches timing, renderer, audio or presentation, list the behaviours Daniel already
   confirmed (the port's `docs/PLAN.md` checkpoints) so he can re-check them.

## B. Launch for Daniel

`python DC/tools/dc.py run DC/ports/<slug> --window` in the background. Say exactly what was
launched and with which flags, and tell him in one sentence what to do. Keys: arrows d-pad,
Z X A S = A B X Y, Enter Start, Q/W triggers, F1 HUD editor (/dc-hud), F10 FPS, F11 capture, F12 screenshot.
Never leave altered values in his settings file: test-only choices go in flags (`--set`), which
are not saved unless `--save-settings`.

## C. Automated look (no Daniel needed)

- Pictures: `python DC/tools/dc.py shots DC/ports/<slug> --at 300,900,1500 --press start@600 -- <flags>`
  then Read `shots/sheet.png`. Screenshots are the game's own 640x480 pixels (not the presented
  aspect) -- widescreen shows as a squeezed image.
- Report: `python DC/tools/dc.py report DC/ports/<slug> --frames N --press ...`. Read: stop reason,
  untranslated targets, unmapped accesses, `AICA ... key-ons`, `PVR ... renders`.
- Runs are deterministic (`--rtc-seed`): a crash at frame N reproduces at frame N.
- Scenarios: `python DC/tools/scenario.py boot|menus|fight|fight-long --set aspect=4:3 --set
  hud_fix=false` (Daniel's own settings: read his `settings.ini`, never write it). One folder per
  run with `sheet.png`, `shot_*.png` (presented size), `audio.txt`, `summary.json`; `--speed real`
  for pacing; `--compare A B` for before/after including a screenshot diff. Other ports:
  `--port <id>` (scenarios per port: `scenario.py list`).
- Release-candidate checks (each must pass before asking Daniel to play):

  | Check | Command | Pass |
  |---|---|---|
  | Quit Game mid-game, both presenters | `scenario.py <play scenario> --speed real [--set latency=low] --env "DREAMCOMP_OVERLAY_KEYS=@<frame>,esc,wait,wait,click:p-quit,wait,wait,click:modal-quit"` | `stop: the window was closed`, no `crash:` in `run.err` |
  | HUD at a wide shape | `scenario.py <s> -- --aspect 21:9`, compare an element's *width* with the 4:3 shot | same width (uncorrected is 1.75x); playbook T17 |
  | Upscaled 2D | look at text in a shot at the default scale | no frames around glyphs (T18) |
  | Idle skip / optimisation hooks bit-identical | max-speed scenario with and without `--env DREAMCOMP_NO_IDLE_SKIP=1` | identical `audio.wav`, 0.00 % screenshot diffs |

## D. "Take a look"

Ask Daniel to press F11 (capture bundle in the working dir) or F12; read the newest files; crop to
the element he named. Report what the evidence shows, then **ask what he saw change** that a still
cannot show. Write any "this draw = that on-screen element" mapping into the port's docs.

## E. Verdict

"Works" -> record a verified checkpoint (commit of port + dreamcomp, what he confirmed) in the
port's `docs/PLAN.md`. "Still broken" -> treat as data; third unconverged round -> `/dc-investigate`.
No game process left running at the end.
