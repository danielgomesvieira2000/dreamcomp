---
name: dc-audit
description: Audit dreamcomp or a port for game data leaks, unlogged engine changes, stale docs, and licence/notice gaps before a commit, push or release. Use when Daniel asks for an audit, before publishing anything, or when unsure whether a file may be committed.
argument-hint: "[port dir | all]"
---

# Audit: $ARGUMENTS

1. `python DC/tools/audit.py all` (framework) and `python DC/tools/audit.py all --repo <port>`
   (each port). Any finding blocks the commit/push; fix the cause, never weaken the check to pass.
2. Check what the script cannot:
   - `git log -p --all -- '*.bin' '*.BIN' '*.cue' '*.gdi' '*.chd'` and large blobs in history
     (`git rev-list --objects --all | git cat-file --batch-check='%(objectsize) %(rest)' | sort -n | tail`):
     game data once committed stays in history and must be purged before any push.
   - Generated code or disassembly pasted into docs or comments (long hex dumps, instruction
     listings beyond a few lines needed to explain a fix).
   - Sega SDK identifiers/headers copied into sources.
   - `THIRD_PARTY_NOTICES.md` covers every vendored or shipped component.
   - Release archives: list contents (see `/dc-release`).
3. Report findings as a table: file, what, why it matters, fix. What each automated check does:
   `DC/docs/AUDITING.md`.
