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
| `tools/scenario.py <boot\|menus\|fight\|fight-long> [--set k=v] [--env K=V] [--out DIR]` | Repeatable scripted run with scratch settings/VMU/bindings and a fixed RTC seed: `run.log`, `audio.wav` + `audio.txt`, `shot_*.png`, `sheet.png`, `summary.json` | `--compare DIR DIR...` puts summaries side by side (speed, clicks, clipping, drops, textures); with two folders it also diffs the screenshots taken at the same frames (% pixels changed, `diff_<frame>.png` with changes in red) |
| `tools/audio_check.py run.wav [--per-second]` | Clipping and clicks (isolated second-difference spikes) per channel, with timestamps | locate a click, then `DREAM_AICA_PROBE` it |
| `tools/crash_lookup.py <port> 0xOFFSET...` | Names the function of a crash report's `module + 0xOFFSET` from the linker map (`/MAP`, beside the executable) | crash reports go to stderr; minidumps to `%APPDATA%/dream-recomp/dream-recomp/crash-*.dmp` |
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
| `DREAM_TEX_SLOW=MS` | Name every texture whose decode took longer than MS |
| `DREAM_NO_PIPELINE_CACHE=1` | Start without the persistent pipeline cache (to measure) |
| `DREAM_NO_MODVOL=1` | Do not draw modifier volumes (shadows), to compare |
| `DREAM_NO_FOG=1` | Do not apply fog, to compare |
| `DREAM_NO_VRAM_INVALIDATE=1` | Keep cached textures when the guest rewrites their memory (the old behaviour), to compare |
| `DREAM_NO_TILE_CLIP=1` | Ignore user tile clipping, to compare |
| `DREAM_NO_MIPMAPS=1` | Upload only the base level of mipmapped textures, to compare |
| `DREAM_DUMP_MIPS=DIR` | Write every level of the first 64 mipmapped textures as PNGs |
| `DREAM_PROFILE=1` | Host time per scheduler event and rendering after the report |
| `DREAM_SINGLE_THREAD=1` | Guest and window events on one thread (upstream behaviour) |

Report lines for performance: `pacing:` (real-time runs: frame-interval percentiles, frames over 20/33 ms, and per late frame the guest frame, textures decoded, pipelines made, render and decode ms) and `textures:` (total decode and upload time).

## Press scripts

`--press B@F[,B@F...]` holds button B for 8 frames from guest frame F. Buttons: `a b c x y z start
up down left right`. Each port records its standard script in `docs/ports.md`.
