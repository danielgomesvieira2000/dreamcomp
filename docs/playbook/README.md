# Dreamcast port playbook

Traps that cost real time, symptom first, so the next port checks here before theorising. Each
row: what you see, why, what fixed it, where it was learned. Mechanism (applies to any game) is
kept apart from numbers (this game's addresses).

| # | Symptom | Cause | Fix | Source |
|---|---|---|---|---|
| T1 | Hundreds/thousands of untranslated calls to `0x8C000600` (or VBR+0x100/0x400/0x600) from frame 0 | Katana copies its exception/interrupt stubs below the load address at boot; VBR points there; the translator never saw that code at its run address | `--suggest-config FILE` lists `[[relocations]]` (source/size/dest); paste, rebuild | soulcalibur-recomp, 2026-10-05 (Soulcalibur VBR = 0x8C000000) |
| T2 | Game runs, music should play, report says `mixer: 0 key-ons` while `channel writes` is large | The sound driver keys voices with **32-bit** stores to AICA channel registers; the mixer only ran key-on for 16-bit writes | Engine: `Aica::reg_write` treats size 4 as 16-bit (as Flycast does). Fixed for all games | soulcalibur-recomp, 2026-10-05 |
| T3 | A RAM-patch enhancement (widescreen value written each vblank) crashes the translated build minutes in, while `--interpret` with the same patch runs clean | Translated code diverging from the interpreter once game state changes: an emitter defect the patch merely exposes | Treat as an emitter bug: replay/differential harness (`/dc-investigate` §4). Never "fix" by dropping the enhancement | soulcalibur-recomp, 2026-10-05 (open) |
| T4 | `.cue` dump rejected / user hand-writes a `.gdi` | Engine originally read only GDI/CHD | Engine now reads Redump `.cue` (HD area at LBA 45000, INDEX 01 = track start) | dreamcomp, 2026-10-05 |
| T5 | `dc.py shots` gets fewer screenshots than asked | `--max-frames` counts guest frames, `--screenshot-at` presented frames; or the run faulted (a half-written `.ppm` is the tell) | Leave headroom; check the run's `stop:` line | dreamcomp, 2026-10-05 |

## Engine-side knowledge worth reading before a new port

`engine/docs/per-game-bring-up.md` (stages), `engine/docs/emitter-design.md` (delay slots, FPSCR
modes, literal folding), `engine/docs/runtime-devinterp.md` (what the interpreter-fallback
switches can and cannot isolate), `engine/docs/compiler-idioms.md`, `engine/docs/runtime-aica.md`.
