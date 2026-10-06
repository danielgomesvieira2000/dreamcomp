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
| `tools/scenario.py <scenario> [--port ID\|slug] [--set k=v] [--env K=V] [--out DIR]` (scenarios per port: `list`) | Repeatable scripted run with scratch settings/VMU/bindings and a fixed RTC seed: `run.log`, `audio.wav` + `audio.txt`, `shot_*.png`, `sheet.png`, `summary.json` | `-- --aspect 21:9` fixes the view's shape for the run (shots come out 21:9 without a 21:9 screen; `expanded` follows the window). File paths after `--` (`--profile`, `--wav`, ...) are made absolute against the caller's folder, as in `dc.py` (the game runs in `--out`). `--compare DIR DIR...` puts summaries side by side (speed, clicks, clipping, drops, textures); with two folders it also diffs the screenshots taken at the same frames (% pixels changed, `diff_<frame>.png` with changes in red). With `--env DREAM_SHOT_INTERP=1` the blended frames are saved as `shot_<frame>_interp.png` |
| `.venv/Scripts/python tools/slowmo.py [--scenario fight] [--start N] [--count K] [--set k=v] [--out DIR]` | Half-speed side-by-side MP4 (`slowmo.mp4`, or `slowmo.webp` without `imageio-ffmpeg`): left 60 fps game frames, right 120 fps with the interpolated frames, so interpolation can be judged on a 60 Hz screen; frames kept in `DIR/frames/` | busy stretches of the `fight` scenario: 1580-1960 (pre-round camera sweep), 3650-3800 (fighting). Output is game footage: never commit it |
| `.venv/Scripts/python tools/pacing.py [--set k=v] [--seconds N] [--out DIR] [-- engine flags]` | Real-time scenario run recorded by Intel PresentMon: present mode, frame time, how long each frame stayed on screen (repeats, drops), display latency, CPU/GPU busy; `pacing.json`, `pacing.png`, `presentmon.csv`; `--compare DIR...`, `--analyze CSV` | docs/PACING.md; needs `winget install Intel.PresentMon.Console` |
| `dreamcomp_ta_dump FILE.ta [--list N] [--region x0,y0,x1,y1] [--verts]` (built with the port, `build/dreamcomp/`) | One line per polygon of a captured display list (`--dump-ta` / F11): PCW/ISP/TSP/TCW, screen bounds, 1/w range; `--verts` adds x, y, 1/w, u, v and colours per vertex | Which draw covers a wrong-looking area: `--region` with the area in 640x480 coordinates, then `--verts` on the candidates |
| `.venv/Scripts/python tools/profile.py FILE.prof [--top N] [--all] [--collapsed OUT] [--compare B.prof]` | Names a `--profile` capture (run e.g. `scenario.py fight --speed real -- --profile FILE`) from the linker map (undname for C++ names): busy share, categories (translated game code, sound, memory access, renderer, driver, sleep...), self / inclusive functions, Dreamcast functions; folded stacks for speedscope | Windows only. Known gap: one frequent frame still prints an empty name (5 % of busy samples on Jet Grind Radio) |
| `python tools/abtest.py --a EXE --b EXE [--a-env K=V] [--b-env K=V] [--runs N]` | Interleaved A/B timing (A B A B ...) of two builds or env switches through `scenario.py --exe`: median and range of host time and game-thread CPU cycles | single runs vary +-3-5 % on a laptop (clocks, temperature, power source); copies of the exe must sit in the build's `game/` folder (DLLs) |
| `python tools/hud_promote.py <slug> [--dry-run] [--from FILE] [--keep]` | Merges the HUD overrides saved with the F1 editor (player's settings folder) into the port's `game/hud_overrides.ini` | skill `/dc-hud`; docs/HUD.md |
| `dreamcomp_hud_tests <scratch dir>` (target, `EXCLUDE_FROM_ALL`) | HUD correction and overrides on synthetic frames | build with `cmake --build <port build> --target dreamcomp_hud_tests` |
| `tools/audio_check.py run.wav [--per-second]` | Clipping and clicks (isolated second-difference spikes) per channel, with timestamps | locate a click, then `DREAM_AICA_PROBE` it |
| `tools/crash_lookup.py <port> 0xOFFSET...` | Names the function of a crash report's `module + 0xOFFSET` from the linker map (`/MAP`, beside the executable) | crash reports go to stderr; minidumps to `%APPDATA%/dream-recomp/dream-recomp/crash-*.dmp` |
| `.venv/Scripts/python tools/crash_stack.py [DUMP]` | The crashing thread's stack from a minidump, scanned for return addresses in the game and named from the map (needs `pip install minidump`) | heuristic: the top entries are the reliable ones |
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
