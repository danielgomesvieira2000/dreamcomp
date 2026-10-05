# Decisions

Each entry: the choice, the options, the reason, who decided. A changed decision gets a new entry
that supersedes the old one; entries are never rewritten. The engine's own ADRs
(`engine/docs/decisions/README.md`) still apply inside `engine/`.

| # | Date | Decision | Options considered | Reason | Decided by |
|---|---|---|---|---|---|
| D-001 | 2026-10-05 | **Engine: vendor dream-recomp as a git subtree at `engine/`**, pinned at `51a148c`; dreamcomp is GPL-2.0 | (a) subtree, (b) submodule + patch files, (c) own MIT engine with emulators only as oracles | Measured before deciding: on this machine dream-recomp built with clang-cl and Soulcalibur reached Character Select, rendered correctly. (c) meant months to reach that point. (a) over (b): dreamcomp needs many engine changes; patch files rot, a subtree merges | Daniel (asked 2026-10-05) |
| D-002 | 2026-10-05 | Each port is its own git repository under `ports/<slug>/`, ignored by dreamcomp, consuming dreamcomp as a `dreamcomp/` submodule (or `-DDREAMCOMP_DIR`) | monorepo `games/<id>/` (engine's model) | Daniel asked for one repo per port; ports release on their own schedule | Daniel (initial brief) |
| D-003 | 2026-10-05 | The SH-4 interpreter fallback (`DREAM_DEV_INTERPRETER`) is **on** in port builds | off in release (engine ADR 2) | A call target discovery missed costs speed instead of a crash, and is reported for the next config round. Revisit when a port has run long sessions with 0 untranslated targets | Claude (autonomous), logged |
| D-004 | 2026-10-05 | Generated code and the extracted boot executable are build artefacts; a packaged port runs from the player's disc via `--disc`, re-verifying the boot file's SHA-1 | ship an extracted file; ship nothing and translate on the player's machine | Smallest player setup that never redistributes game data; wrong revisions are refused | Claude (autonomous), logged |
| D-005 | 2026-10-05 | Enhancements hook in through the launcher-extension registry and `[hooks]`, not by forking `boot_main.cpp` | fork the launcher into dreamcomp | Keeps engine merges cheap; both mechanisms are generic and can go upstream | Claude (autonomous), logged |
| D-006 | 2026-10-05 | Default presentation fit is **crop** (fill the window, no bars); letterbox and stretch selectable | letterbox (engine default) | Daniel's series rule: no black bars, ever | series rule |
| D-007 | 2026-10-05 | Widescreen first as **anamorphic** (game renders a 16:9 view into 640x480, presenter stretches) where a known projection value exists; true Hor+ per game later | Hor+ only | Cheap, proven by Flycast's per-title table; HUD stretching is the known cost, fixed per game via hooks | Claude (autonomous), logged |
