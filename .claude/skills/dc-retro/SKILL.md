---
name: dc-retro
description: End-of-session retrospective for dreamcomp work - route this session's corrections and discoveries into durable knowledge (playbook, port docs, decisions, skills, ports registry). Use when Daniel ends a session, asks to wrap up, or asks Claude to learn from the session.
---

# Retro

For each correction Daniel made and each discovery that cost time, pick exactly one home:

| Kind | Home |
|---|---|
| A trap another Dreamcast port could hit (symptom -> cause -> fix) | `DC/docs/playbook/README.md` row, with `Source:` (port, commit) |
| A fact about this game | the port's `docs/GAME-INTERNALS.md` |
| A toolchain/runtime fact of this port | the port's `docs/PORTING.md` |
| A choice made between options | `DC/docs/DECISIONS.md` (new row; never rewrite old ones) |
| An engine change | `DC/docs/engine-changes.md` (the audit enforces it) |
| How Daniel wants Claude to work, on any port | his framework's `working-style.md` (ask before editing) |
| A workflow step that was missing or wrong | the matching `DC/.claude/skills/dc-*/SKILL.md` |
| Per-port paths, commands, keys, status | `DC/docs/ports.md` |

Then: update `DC/docs/ROADMAP.md` statuses honestly (skipped verification stays "not verified"),
run `python DC/tools/audit.py all`, commit docs. List what remains open in the active plan.
