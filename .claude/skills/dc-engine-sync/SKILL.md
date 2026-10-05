---
name: dc-engine-sync
description: Pull upstream dream-recomp changes into dreamcomp's vendored engine/ subtree, reconcile dreamcomp's own engine changes, and re-verify ports. Use when Daniel asks to update the engine, sync with upstream, or pick up an upstream fix.
argument-hint: "[upstream ref, default main]"
---

# Engine sync: $ARGUMENTS

1. Clean tree; note the current base (`git log --grep "^Squashed 'engine/'" -1`) and each port's
   last verified checkpoint.
2. Read upstream's log since the base (`git ls-remote`, or a scratch clone in `work/eval/`): what
   changed in translator / runtime / render; any upstream fix that duplicates a row in
   `docs/engine-changes.md`.
3. `git subtree pull --prefix engine https://github.com/phobos665/dream-recomp.git <ref> --squash`.
   Resolve conflicts in favour of upstream's structure, re-applying dreamcomp's intent.
4. Update `docs/engine-changes.md`: new pinned base row; rows upstream absorbed move to
   *Upstreamed*. `python tools/audit.py all` must pass.
5. Rebuild every port; `dc.py report` with each port's standard press script; compare untranslated
   targets, faults and key counters against before. `dc.py shots` on the standard frames.
6. Commit "Engine: sync with upstream <sha>" with the before/after numbers. Never push to the
   upstream repository; offering a change upstream is a PR from Daniel's account, only if he asks.
