# Audits

`python tools/audit.py <check> [--repo DIR]` — exit 1 on any finding. Runs as a git pre-commit
hook (`audit.py install-hook --repo DIR`, `assets --staged` only) and should run in CI.

| Check | What it looks at | Why |
|---|---|---|
| `assets` | Every tracked (or, with `--staged`, staged) file: blocked extensions (disc images, PVR/PVM/AFS/ADX/SFD, Soulcalibur `.P16`, VMU saves, ELF), boot-file names (`1ST_READ.BIN`, `IP.BIN`, `(Track N).bin`), file signatures (`SEGA SEGAKATANA`, `GBIX`/`PVRT`, `PVMH`, `AFS\0`, `CRID`, raw CD sync), and anything over 2 MB not on the allow-list | Game data must never be published; once pushed it stays in history |
| `engine` | Files under `engine/` that differ from the newest squashed subtree commit, plus uncommitted edits; each must be named in `docs/engine-changes.md` | Every divergence from upstream stays reviewable and offerable as a PR; merges stay safe |
| `docs` | Backticked repo paths in `*.md` (outside `engine/`) must exist; every `.claude/skills/*/SKILL.md` has matching `name:` and a `description:` | Docs and skills that point at moved files mislead the next session |
| `all` | All of the above | CI, before push, before release |

What the script cannot see (zip contents, history, pasted disassembly, SDK text) is covered by the
`/dc-audit` skill's manual checklist.

## Building the same in another project

Three ideas carry over: (1) scan by *content signature and size*, not only by name, because
renamed game files slip past `.gitignore`; (2) for a vendored dependency, diff against the
recorded upstream tree and require a ledger row per changed file; (3) run the cheap check as a
pre-commit hook that only looks at staged files, and the full check in CI.
