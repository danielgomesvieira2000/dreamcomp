#!/usr/bin/env python3
"""Guest-code profiler: where does the game spend its SH-4 time?

    python tools/profile.py <port> [--frames 1800] [--every 20000] [--press ...] [--top 30] [-- launcher flags]
    python tools/profile.py <port> --samples FILE          # re-analyse an earlier sample file

Runs the port headless with the launcher's PC sampler (`--sample N`: one sample every N guest
cycles), then attributes each sample to the translated function containing it
(build/game/gen/<id>.functions.json, names from game/symbols.tsv when present) and prints a
ranked table with each hot function's most frequent callers (from PR).

What it measures: guest time (virtual cycles), i.e. what the game's own code does, which is what
matters for finding the code to hook or optimise. Host costs outside guest code (renderer, AICA
mixer) do not appear here; the run's `host:` line and the engine's
docs/cpu-performance-study.md cover those.
"""
from __future__ import annotations

import argparse
import bisect
import collections
import json
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dc  # noqa: E402  (port_info, launcher_args)


def load_functions(info: dict) -> tuple[list[int], list[tuple[int, int, str]]]:
    path = os.path.join(info["dir"], "build", "game", "gen", f"{info['id']}.functions.json")
    fns = json.load(open(path))["functions"]
    rows = sorted((int(f["entry"], 16), int(f["end"], 16), f.get("name") or f"fn_{f['entry'][2:]}")
                  for f in fns)
    names = {}
    sym = os.path.join(info["dir"], "game", "symbols.tsv")
    if os.path.exists(sym):
        for line in open(sym, encoding="utf-8"):
            parts = line.strip().split("\t")
            if len(parts) >= 2 and not line.startswith("#"):
                try:
                    names[int(parts[0], 16) & 0x1FFFFFFF] = parts[1]
                except ValueError:
                    pass
    rows = [(a, e, names.get(a & 0x1FFFFFFF, n)) for a, e, n in rows]
    return [r[0] for r in rows], rows


def lookup(starts, rows, pc: int) -> str:
    a = (pc & 0x1FFFFFFF) | 0x0C000000 if (pc & 0x1C000000) == 0x0C000000 else pc
    i = bisect.bisect_right(starts, a) - 1
    if i >= 0 and rows[i][0] <= a < rows[i][1]:
        return rows[i][2]
    return f"<{pc:08x}>"


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    extra = []
    if "--" in argv:
        cut = argv.index("--")
        argv, extra = argv[:cut], argv[cut + 1:]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--frames", type=int, default=1800)
    ap.add_argument("--every", type=int, default=20000, help="guest cycles between samples")
    ap.add_argument("--press")
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--samples", help="analyse this sample file instead of running")
    a = ap.parse_args(argv)
    info = dc.port_info(a.port)
    starts, rows = load_functions(info)

    sample_file = a.samples
    tmp = None
    if not sample_file:
        tmp = tempfile.TemporaryDirectory()
        sample_file = os.path.join(tmp.name, "samples.txt")
        args = ["--max-frames", str(a.frames), "--rtc-seed", "1000000", "--no-audio",
                "--sample", str(a.every), "--sample-file", sample_file]
        if a.press:
            args += ["--press", a.press]
        cmd = dc.launcher_args(info, args + extra)
        subprocess.run(cmd, cwd=info["dir"], stdout=subprocess.DEVNULL)

    by_fn = collections.Counter()
    callers: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    total = 0
    for line in open(sample_file):
        parts = line.split()
        if len(parts) < 6:
            continue
        pc, pr = int(parts[1], 16), int(parts[5], 16)
        fn = lookup(starts, rows, pc)
        by_fn[fn] += 1
        callers[fn][lookup(starts, rows, pr)] += 1
        total += 1
    if not total:
        print("no samples")
        return 1
    print(f"{total} samples, one per {a.every} guest cycles\n")
    print(f"{'%':>6}  {'samples':>8}  function                      top callers (by PR)")
    for fn, n in by_fn.most_common(a.top):
        top = ", ".join(f"{c} {k * 100 // n}%" for c, k in callers[fn].most_common(3))
        print(f"{100.0 * n / total:6.2f}  {n:8d}  {fn:<28}  {top}")
    if tmp:
        tmp.cleanup()
    return 0


if __name__ == "__main__":
    sys.exit(main())
