---
name: dc-investigate
description: Disciplined bug investigation for a dreamcomp Dreamcast port (crash, freeze, wrong graphics, wrong/missing audio, regression) - reproduce deterministically, bisect translated vs interpreted, read known traps first, timebox, roll back cleanly. Use when Daniel reports a bug on a Dreamcast port or a fix attempt failed.
argument-hint: "<symptom, where/when it happens>"
---

# Investigate: $ARGUMENTS

## 1. Pin the symptom and a checkpoint

Write down: what is wrong, on which element, in which condition (mode, stage, setting, frame),
since when (a version or commit where it was fine is the strongest clue -- bisect with `git` on
both the port and dreamcomp). Record the last verified checkpoint from the port's `docs/PLAN.md`.

## 2. Read what is known (mandatory)

`DC/docs/playbook/README.md`, the port's `docs/GAME-INTERNALS.md` / `docs/PORTING.md` /
`docs/findings/`, the engine's docs for the subsystem (`DC/engine/docs/runtime-*.md`,
`emitter-design.md`, `differential-harness.md`, `runtime-devinterp.md`). Quote the relevant passage.

## 3. Reproduce deterministically

Headless first: `dc.py report <port> --frames N --press <script> -- <flags>` with the same
`--rtc-seed`. A run that faults prints `first unmapped access by frame F ... pc ... pr ...` and the
guest call chain. If it only happens in the window, say so.

## 4. Split the space

| Question | Instrument |
|---|---|
| Translated code wrong, or the runtime/hardware model? | same run with `--interpret` (everything interpreted). Clean interpreted + faulting translated = emitter defect. For a *picture*: `-- --dump-ta F --dump-ta-at-frame N` in both runs and `cmp` the files; byte-identical means the game really sends that geometry (JGR grey walls) |
| Upscaled only? | `--set scale=1`: a defect that vanishes is sampling at higher resolution, not accuracy (T18) |
| Hang or crash dump | `tools/crash_stack.py DUMP [--all] [--map FILE]` (hang dumps list every thread; `--map` for an older build); reproduce UI paths with `DREAMCOMP_OVERLAY_KEYS` (docs/FRONTEND.md) |
| Which function? | `DREAM_INTERP_FUNCS` / `DREAM_INTERP_RANGE` only redirect *indirect* calls (engine `runtime-devinterp.md`); prefer `--replay*` differential modes (dev build) |
| When did state diverge? | `--write-hash FILE` on two runs; first differing line = frame |
| Who wrote this address? | `DREAM_WATCH_WRITE`, `--write-log-range` |
| Audio | `tools/scenario.py fight` -> `audio.txt` (clicks/clipping with timestamps from `tools/audio_check.py`); a click at sample S: rerun with `--env DREAM_AICA_PROBE=S-10,30,p.txt` (every voice, register write and changed register byte); report `AICA` line, `DREAM_ARM7_WATCH`, `--dump-aram` |
| Graphics | F11 / `--capture-at N` bundle, replay with `dream_render_view`; `-- --dump-ta F --dump-ta-at-frame N --dump-vram V` then `dreamcomp_ta_dump F --region x0,y0,x1,y1 [--verts]` (list, PCW/ISP/TSP/TCW per poly, modifier count; per-vertex x, y, 1/w, u, v with `--verts`) and the `V.regs` PVR registers; `DREAM_PVR_WATCH=11C` for who writes a register; `DREAM_NO_MODVOL=1` to compare |
| Stutter / speed | real-time `tools/scenario.py <s> --speed real`: report `pacing:` (late frames with textures/pipelines/render/decode ms) and `textures:`; `DREAM_TEX_SLOW=1`; `DREAM_PROFILE=1` |
| Did my change alter anything else? | same scenario before/after, `tools/scenario.py x --compare A B`: summary side by side and per-screenshot pixel diff (`diff_*.png`) |
| Reference behaviour | Flycast sources: `curl` raw files from github.com/flyinghead/flycast into `work/ref/flycast/` and compare line by line (an audit agent works well for a whole subsystem) |
| Discovery gap | `--suggest-config FILE` |

A counter improving is a hypothesis; the symptom gone, in Daniel's condition, is the proof.

## 5. Fix

One change, end-to-end path read first, behind a switch if not obviously safe. Engine fixes go in
`DC/engine` + `DC/docs/engine-changes.md`. Re-check confirmed behaviours the area can affect.

## 6. Timebox

After three unconverged probes: stop, report what was measured, refuted hypotheses, the next
concrete attempt and its cost; let Daniel choose. Don't default to "known issue" for tractable bugs.

## 7. Rollback ("we are in too deep")

Findings into the port's docs (negative results included) -> experimental state on branch
`wip/<topic>` -> restore last verified commit on `main` -> rebuild -> tell Daniel which build he has.

## 8. Close out

Root cause in the port's docs in the same commit; a trap other ports could hit -> new row in
`DC/docs/playbook/README.md` (symptom, cause, fix, source).
