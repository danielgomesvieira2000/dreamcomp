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
| Translated code wrong, or the runtime/hardware model? | same run with `--interpret` (everything interpreted). Clean interpreted + faulting translated = emitter defect |
| Which function? | `DREAM_INTERP_FUNCS` / `DREAM_INTERP_RANGE` only redirect *indirect* calls (engine `runtime-devinterp.md`); prefer `--replay*` differential modes (dev build) |
| When did state diverge? | `--write-hash FILE` on two runs; first differing line = frame |
| Who wrote this address? | `DREAM_WATCH_WRITE`, `--write-log-range` |
| Audio | `--wav out.wav --audio`, report `AICA` line, `DREAM_ARM7_WATCH`, `--dump-aram` |
| Graphics | F11 / `--capture-at N` bundle, replay with `dream_render_view` |
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
