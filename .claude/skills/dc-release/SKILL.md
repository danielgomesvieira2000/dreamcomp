---
name: dc-release
description: Package and publish a release of a dreamcomp Dreamcast port (version bump, changelog, Windows/Linux packages without any game data, tag, full GitHub release). Use only when Daniel explicitly asks to release, publish or package a version.
argument-hint: "<port> <version> [notes]"
---

# Release: $ARGUMENTS

1. Preconditions: Daniel asked; the port's last verified checkpoint is the commit being released;
   `python DC/tools/audit.py all --repo <port>` and on dreamcomp pass.
2. Version in the port's `CMakeLists.txt`; `CHANGELOG.md` entry; `docs/releases/<v>.md` with short
   bullets: added, known bugs, not tested.
3. Build Release; package per platform: the executable, SDL3 runtime, `game/<id>.toml` (config,
   no extracted files), `README.txt` (how to point it at your own disc), `LICENSE`,
   `THIRD_PARTY_NOTICES.md`. A `-debug-symbols` variant carries the `.pdb`.
4. **Inspect every archive** before upload: no `1ST_READ.BIN`, `extracted/`, `gen/`, disc
   images, VMU or settings files. `python DC/tools/audit.py assets` cannot see inside zips: list
   them.
5. `gh release create v<v> --repo danielgomesvieira2000/<slug>` as a **full release marked
   Latest** (never `--prerelease`), with the notes file. Push only to Daniel's own repositories.
6. Record the release in the port's `docs/PLAN.md` and `DC/docs/ports.md`.
