# Tools

| Tool | Use | Notes |
|---|---|---|
| `tools/dc.py doctor` | What this machine has / lacks | |
| `tools/dc.py deps` | SDL3 into `.deps/` (Windows) | Linux/macOS: package manager |
| `tools/dc.py setup <port> --disc X` | Verify the disc (boot-file SHA-1), extract only the boot executable, remember the disc | `.cue`, `.gdi`, `.chd` |
| `tools/dc.py build <port> [--headless] [--fresh]` | Configure (clang-cl + Ninja on Windows, inside vcvars) and build | `build/game/<slug>.exe` |
| `tools/dc.py run <port> [--window] [args]` | Launch with the remembered disc | |
| `tools/dc.py report <port> --frames N --press S [-- flags]` | Headless deterministic run; prints the end-of-run report | read: stop, untranslated, unmapped, AICA, PVR |
| `tools/dc.py shots <port> --at A,B,C [--press S] [-- flags]` | One windowed run, screenshots at presented frames A,B,C, `shots/sheet.png` | game's own 640x480 pixels |
| `tools/profile.py <port> --frames N [--press S]` | Guest-time hot list per translated function, with callers | sampling adds scheduler events and can shift interrupt timing (playbook T3) |
| `tools/scenario.py <boot\|menus\|fight\|fight-long> [--set k=v] [--env K=V] [--out DIR]` | Repeatable scripted run with scratch settings/VMU/bindings and a fixed RTC seed: `run.log`, `audio.wav` + `audio.txt`, `shot_*.png`, `sheet.png`, `summary.json` | `--compare DIR DIR...` puts summaries side by side (speed, clicks, clipping, drops, textures) |
| `tools/audio_check.py run.wav [--per-second]` | Clipping and clicks (isolated second-difference spikes) per channel, with timestamps | locate a click, then `DREAM_AICA_PROBE` it |
| `tools/audit.py all [--repo DIR]` | No game data, engine ledger complete, docs/skills consistent | pre-commit hook: `install-hook` |
| `tools/build_linux.sh <port> [--headless]` | Linux build (native or WSL) into `<port>/build-linux` | window build needs `libsdl3-dev`, `glslc` |

Engine-level instruments (flags of the port executable, `--help`): `--suggest-config`,
`--interpret`, `--write-hash`, `--write-log-range`, `--dump-*`, `--capture-at`, `--wav`,
`--replay*` (needs `-DDREAM_TRANSLATE_EXTRA_FLAGS=--replay-hooks`). See
`engine/docs/differential-harness.md` and `engine/docs/runtime-devinterp.md`.

## Debug environment variables

| Variable | Effect |
|---|---|
| `DREAM_AICA_PROBE=FIRST,COUNT[,FILE]` | For output samples FIRST.. (the `--wav` frame index) write every playing channel's state and contribution, the DSP's share, every channel-register write and every register byte that changed (FILE, default `aica_probe.txt`) |
| `DREAM_PVR_WATCH=11C,108` | Log writes to these PVR register offsets (hex from 0x005F8000) to stderr |
| `DREAM_AUDIO_TRACE=1` | Log every audio block the sink drops, with the queue level |
| `DREAM_PROFILE=1` | Host time per scheduler event and rendering after the report |
| `DREAM_SINGLE_THREAD=1` | Guest and window events on one thread (upstream behaviour) |

## Press scripts

`--press B@F[,B@F...]` holds button B for 8 frames from guest frame F. Buttons: `a b c x y z start
up down left right`. Each port records its standard script in `docs/ports.md`.
