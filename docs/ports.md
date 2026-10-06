# Ports registry

One section per port: paths, commands, keys, status. Skills read this instead of guessing.

## Soulcalibur (USA) — `soulcalibur-recomp`

| | |
|---|---|
| Local repo | `ports/soulcalibur-recomp` (branch `main`; no GitHub remote yet) |
| Disc | T1401N V1.000 (1999-07-30); boot `1ST_READ.BIN` 3,679,460 bytes, SHA-1 `967ca1fe2e8c7df57a5de1e83d939bed483cc6c9` |
| SDK | Shinobi 1.43, Ninja (Apr 1999), Kamui 1.06, sd 1.00.18 (strings in the boot file) |
| Build | `python tools/dc.py build ports/soulcalibur-recomp` → `build/game/soulcalibur-recomp.exe` |
| Run | `python tools/dc.py run ports/soulcalibur-recomp --window`, or double-click the exe |
| Settings | `%APPDATA%\dreamcomp\soulcalibur\settings.ini`, VMU `vmu_a1.bin` beside it |
| Standard script | `--press start@600,start@900,a@1200,a@1260` → title, Arcade, Kilik, first fight (~frame 1700+) |
| Port flags | `--widescreen` (anamorphic, Flycast value 0.75 at `0x8C266C28`) |
| Status | boots, intro, menus, arcade fights across stages, saves; 2026-10-06 accuracy pass (HUD, shadows, fog, texture invalidation, tile clip, mipmaps) and performance pass; scenarios: `python tools/scenario.py boot|menus|fight|fight-long|attract` |

## Jet Grind Radio (USA) — `jet-grind-radio-recomp`

| | |
|---|---|
| Local repo | `ports/jet-grind-radio-recomp` (branch `main`; no GitHub remote yet) |
| Disc | MK-51058 V1.005 (2000-10-02); boot `1ST_READ.BIN` 3,075,648 bytes, SHA-1 `dc0bda1431d97223885a22c68f7ddb3524757895` |
| SDK | Shinobi 1.68, Ninja (Feb 2000), Kamui 1.11; CRI ADX/SJ middleware (streamed music) |
| Build | `python tools/dc.py build ports/jet-grind-radio-recomp` → `build/game/jet-grind-radio-recomp.exe` |
| Settings | `%APPDATA%\dreamcomp\jetgrindradio\settings.ini` |
| Standard script | `--press start@600,start@900` → title flyby (~frame 2100), "PRESS START BUTTON" |
| Not translated | `2_DP.BIN` (Dream Passport 2 browser for the online features) |
| Status | 2026-10-06: boots, `scenario.py play --port jetgrindradio` reaches Gum's tutorial and skates the first street (0 untranslated / 0 unmapped); widescreen to 21:9 with the HUD corrected (polygon rule + one override); idle skip; translated = interpreted at attract frame 10040. Not yet: compared with Flycast, listened to by Daniel, played past the tutorial. Plan: the port's docs/PLAN.md |
