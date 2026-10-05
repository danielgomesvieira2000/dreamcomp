#!/usr/bin/env python3
"""dreamcomp audits: keep game data out, keep engine changes accounted for, keep docs honest.

    python tools/audit.py assets [--staged] [--repo DIR]   no disc/game/derived data tracked or staged
    python tools/audit.py engine                           every file changed under engine/ is in docs/engine-changes.md
    python tools/audit.py docs [--repo DIR]                skills/docs reference files that exist
    python tools/audit.py all                              everything above (CI and the pre-commit hook)
    python tools/audit.py install-hook [--repo DIR]        install the pre-commit hook into a repo

Works on this framework and on any port repo (`--repo ports/<slug>`). Exit code 1 on any finding.
What each check looks for, and why, is in docs/AUDITING.md.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Extensions that are disc images, Dreamcast game data containers, or saves/firmware.
BLOCKED_EXT = {
    ".gdi", ".cdi", ".chd", ".cue", ".iso", ".mds", ".mdf", ".nrg",  # disc images
    ".pvr", ".pvm", ".afs", ".adx", ".ahx", ".sfd", ".str", ".kat", ".mlt", ".osb",  # DC assets
    ".p16", ".p08", ".p04",  # Soulcalibur stream audio
    ".vms", ".vmi", ".vmu", ".dci",  # memory card saves
    ".elf", ".fidb",
}
# Files whose bytes identify Dreamcast game material regardless of name.
MAGIC = [
    (0, b"SEGA SEGAKATANA", "IP.BIN boot header"),
    (0, b"SEGA SEGAKATANA ", "IP.BIN boot header"),
    (0, b"GBIX", "PVR texture (GBIX header)"),
    (0, b"PVRT", "PVR texture"),
    (0, b"PVMH", "PVM texture archive"),
    (0, b"AFS\x00", "AFS archive"),
    (0, b"CRID", "Sofdec/CRI container"),
    (0, b"\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x00", "raw CD sector (sync pattern)"),
]
SIZE_LIMIT = 2 * 1024 * 1024  # anything larger must be on the allow-list
# Paths that may legitimately hold binaries (fonts/icons we made or that are licensed, test fixtures
# synthesised by the tests themselves).
ALLOW = [
    re.compile(r"^engine/tools/dcdisc/tests/fixtures/"),
    re.compile(r"^engine/third_party/"),
    re.compile(r"^(engine/)?tests/"),
    re.compile(r"^assets/(fonts|icons)/"),
]


def git(repo: str, *args: str) -> str:
    r = subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    if r.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {r.stderr.strip()}")
    return r.stdout


def audit_assets(repo: str, staged_only: bool) -> list[str]:
    if staged_only:
        files = git(repo, "diff", "--cached", "--name-only", "--diff-filter=ACMR").splitlines()
    else:
        files = git(repo, "ls-files").splitlines()
    findings = []
    for rel in files:
        if not rel or any(a.match(rel) for a in ALLOW):
            continue
        path = os.path.join(repo, rel)
        ext = os.path.splitext(rel)[1].lower()
        if ext in BLOCKED_EXT:
            findings.append(f"{rel}: blocked file type {ext}")
            continue
        base = os.path.basename(rel).upper()
        if base in ("1ST_READ.BIN", "IP.BIN") or re.match(r".*\(TRACK \d+\)\.BIN$", base):
            findings.append(f"{rel}: disc boot file or track")
            continue
        try:
            size = os.path.getsize(path)
            with open(path, "rb") as f:
                head = f.read(64)
        except OSError:
            continue  # deleted in the work tree but still staged/tracked: git shows it elsewhere
        for off, magic, what in MAGIC:
            if head[off:off + len(magic)] == magic:
                findings.append(f"{rel}: looks like {what}")
                break
        else:
            if size > SIZE_LIMIT:
                findings.append(f"{rel}: {size} bytes; large files need an ALLOW entry and a reason")
    return findings


def subtree_base(repo: str) -> str | None:
    """The newest squashed upstream commit of engine/ (what `git subtree` recorded)."""
    out = git(repo, "log", "--format=%H %s", "--grep", "^Squashed 'engine/' content from commit")
    for line in out.splitlines():
        return line.split()[0]
    return None


def audit_engine(repo: str) -> list[str]:
    base = subtree_base(repo)
    if not base:
        return ["engine/: no squashed subtree commit found; was engine/ added with git subtree?"]
    changed = git(repo, "diff", "--name-only", f"{base}", "HEAD:engine").splitlines()
    # Uncommitted edits count too: the ledger must be written in the same commit.
    changed += [p[len("engine/"):] for p in git(repo, "diff", "--name-only", "HEAD", "--", "engine").splitlines()]
    changed += [p[len("engine/"):] for p in git(repo, "diff", "--cached", "--name-only", "--", "engine").splitlines()]
    ledger = open(os.path.join(repo, "docs", "engine-changes.md"), encoding="utf-8").read()
    findings = []
    for f in sorted(set(changed)):
        if f and f not in ledger and f"engine/{f}" not in ledger:
            findings.append(f"engine/{f}: changed from upstream but not named in docs/engine-changes.md")
    return findings


LINK = re.compile(r"`((?:docs|tools|engine|cmake|enhance|frontend|templates|platform|\.claude)/[^`\s*<>{}]+)`")


def audit_docs(repo: str) -> list[str]:
    findings = []
    for rel in git(repo, "ls-files", "*.md").splitlines():
        if rel.startswith("engine/"):
            continue  # upstream's own docs; their links are theirs to keep
        text = open(os.path.join(repo, rel), encoding="utf-8", errors="replace").read()
        for m in LINK.finditer(text):
            target = m.group(1).rstrip(".,:;)").split("#")[0].split(":")[0]
            # Framework docs also name engine paths without the prefix and port-relative paths
            # (docs/PLAN.md of "the port"), which the port template defines.
            roots = [repo, os.path.join(repo, "engine"), os.path.join(repo, "templates", "port")]
            if target.startswith("docs/") and os.path.isdir(os.path.join(repo, "engine")):
                roots.append(os.path.join(repo, "engine"))
            if not any(os.path.exists(os.path.join(r, target)) for r in roots):
                findings.append(f"{rel}: references missing path {target}")
    skills = os.path.join(repo, ".claude", "skills")
    if os.path.isdir(skills):
        for name in os.listdir(skills):
            p = os.path.join(skills, name, "SKILL.md")
            if not os.path.exists(p):
                findings.append(f".claude/skills/{name}: no SKILL.md")
                continue
            head = open(p, encoding="utf-8").read(600)
            if not re.search(r"^name:\s*" + re.escape(name) + r"\s*$", head, re.M):
                findings.append(f".claude/skills/{name}/SKILL.md: frontmatter name is not '{name}'")
            if not re.search(r"^description:", head, re.M):
                findings.append(f".claude/skills/{name}/SKILL.md: no description in frontmatter")
    return findings


HOOK = """#!/bin/sh
# Installed by dreamcomp tools/audit.py install-hook: refuse commits that stage game data.
exec python "{script}" assets --staged --repo "$(git rev-parse --show-toplevel)"
"""


def install_hook(repo: str) -> int:
    hooks = git(repo, "rev-parse", "--git-path", "hooks").strip()
    hooks = hooks if os.path.isabs(hooks) else os.path.join(repo, hooks)
    os.makedirs(hooks, exist_ok=True)
    path = os.path.join(hooks, "pre-commit")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(HOOK.format(script=os.path.abspath(__file__).replace("\\", "/")))
    os.chmod(path, 0o755)
    print(f"installed {path}")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("check", choices=["assets", "engine", "docs", "all", "install-hook"])
    ap.add_argument("--repo", default=ROOT)
    ap.add_argument("--staged", action="store_true", help="assets: only what is staged for commit")
    a = ap.parse_args(argv)
    repo = os.path.abspath(a.repo)
    if a.check == "install-hook":
        return install_hook(repo)
    findings: list[str] = []
    if a.check in ("assets", "all"):
        findings += [f"[assets] {f}" for f in audit_assets(repo, a.staged)]
    if a.check in ("engine", "all") and os.path.isdir(os.path.join(repo, "engine")):
        findings += [f"[engine] {f}" for f in audit_engine(repo)]
    if a.check in ("docs", "all"):
        findings += [f"[docs] {f}" for f in audit_docs(repo)]
    for f in findings:
        print(f)
    print(f"audit {a.check}: {'FAIL, ' + str(len(findings)) + ' finding(s)' if findings else 'ok'}")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
