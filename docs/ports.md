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
| Status | menus, fights, audio OK to frame 3500; widescreen faults at frame 2272 (emitter defect, investigating) |
