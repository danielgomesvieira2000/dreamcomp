#!/usr/bin/env python3
"""A/B timing of two builds (or two environments) with the noise of a laptop taken out.

A single run's host time moves by +-3-5 % on a laptop (clocks, temperature, battery), as much as
the optimisations being measured. This alternates the variants (A B A B ...) so drift hits both
alike, and reports the median and spread of host time and game-thread CPU cycles per variant.

    python tools/abtest.py --a work/ab/old.exe --b work/ab/new.exe --runs 4
    python tools/abtest.py --a-env DREAM_X=0 --runs 3        # same build, an env switch
    python tools/abtest.py --scenario fight --frames 3000 ...

Each run is tools/scenario.py (unthrottled, no screenshots) into work/ab/<variant><n>.
"""
import argparse
import json
import os
import statistics
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(variant, n, a, exe, env):
    out = os.path.join(ROOT, "work", "ab", f"{variant}{n}")
    cmd = [sys.executable, os.path.join(ROOT, "tools", "scenario.py"), a.scenario, "--shots", "",
           "--out", out]
    if a.frames:
        cmd += ["--frames", str(a.frames)]
    for kv in a.set:
        cmd += ["--set", kv]
    for kv in env:
        cmd += ["--env", kv]
    if exe:
        cmd += ["--exe", exe]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return json.load(open(os.path.join(out, "summary.json")))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--a", help="executable for variant A (default: the port's build)")
    ap.add_argument("--b", help="executable for variant B")
    ap.add_argument("--a-env", action="append", default=[])
    ap.add_argument("--b-env", action="append", default=[])
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--scenario", default="fight")
    ap.add_argument("--frames", type=int)
    ap.add_argument("--set", action="append", default=["aspect=expanded"])
    a = ap.parse_args()

    res = {"A": [], "B": []}
    for n in range(a.runs):
        for v, exe, env in (("A", a.a, a.a_env), ("B", a.b, a.b_env)):
            s = run(v, n, a, exe, env)
            res[v].append(s)
            print(f"  {v}{n}: host {s.get('host_s')} s, thread {s.get('thread_gcycles')} G cycles, "
                  f"stop {s.get('stop')}", flush=True)

    def stat(v, key):
        xs = [s[key] for s in res[v] if key in s]
        if not xs:
            return None, None, None
        return statistics.median(xs), min(xs), max(xs)

    print()
    for key in ("host_s", "thread_gcycles"):
        ma, la, ha = stat("A", key)
        mb, lb, hb = stat("B", key)
        if ma is None or mb is None:
            continue
        print(f"{key:15s} A median {ma:8.2f} [{la:.2f}-{ha:.2f}]   B median {mb:8.2f} [{lb:.2f}-{hb:.2f}]   "
              f"B/A {100.0 * (mb / ma - 1):+.1f} %")
    return 0


if __name__ == "__main__":
    sys.exit(main())
