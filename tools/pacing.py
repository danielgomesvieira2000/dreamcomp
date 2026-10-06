#!/usr/bin/env python3
"""Frame pacing as the display sees it: a real-time scenario run under PresentMon.

Runs tools/scenario.py at real speed and records the game's presents with Intel PresentMon
(`winget install Intel.PresentMon.Console`; no admin needed for members of Performance Log Users),
then summarises what the player sees:

    present mode         how Windows shows the frames (Independent Flip / Composed: Flip are
                         good; "Composed: Copy with GPU GDI" adds a copy and latency)
    frame time           interval between presents (CPU side): even = good pacing
    displayed time       how long each frame stayed on screen: 16.7 ms each at 60 Hz is perfect;
                         33.3 = a repeated frame (stutter), 0/NA = a frame never shown (dropped)
    display latency      frame start to photons
    cpu busy / gpu time  per-frame work: the performance headroom

    python tools/pacing.py                                  # fight, windowed, scale 2
    python tools/pacing.py --set fullscreen=true --set scale=3 --out work/pace/fs3
    python tools/pacing.py --analyze work/pace/base/presentmon.csv
    python tools/pacing.py --compare work/pace/a work/pace/b

Writes <out>/presentmon.csv, <out>/pacing.json and <out>/pacing.png (frame and displayed times).
"""
import argparse
import csv
import glob
import json
import os
import statistics
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = "soulcalibur-recomp.exe"


def find_presentmon():
    pats = [os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WinGet", "Packages",
                         "Intel.PresentMon.Console*", "presentmon*.exe")]
    for p in pats:
        hits = glob.glob(p)
        if hits:
            return hits[0]
    return None


def num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def pct(v, q):
    v = sorted(v)
    return v[min(len(v) - 1, int(q * (len(v) - 1)))] if v else None


def analyze(path, hz=60.0):
    rows = list(csv.DictReader(open(path, encoding="utf-8-sig")))
    rows = [r for r in rows if r.get("Application", "").lower() == EXE]
    if not rows:
        return {"error": "no presents recorded"}
    refresh = 1000.0 / hz
    ft = [num(r["FrameTime"]) for r in rows if num(r["FrameTime"]) is not None]
    disp = [num(r["DisplayedTime"]) for r in rows]
    shown = [d for d in disp if d is not None and d > 0]
    lat = [num(r["DisplayLatency"]) for r in rows if num(r["DisplayLatency"]) is not None]
    cpu = [num(r["CPUBusy"]) for r in rows if num(r["CPUBusy"]) is not None]
    gpu = [num(r["GPUBusy"]) for r in rows if num(r["GPUBusy"]) is not None]
    modes = {}
    for r in rows:
        modes[r["PresentMode"]] = modes.get(r["PresentMode"], 0) + 1
    # Displayed time in refresh periods: 1 = shown once, 2 = held over a refresh (a stutter).
    held = [round(d / refresh) for d in shown]
    out = {
        "presents": len(rows),
        "present_mode": modes,
        "frame_ms": {"mean": statistics.fmean(ft), "p50": pct(ft, .5), "p95": pct(ft, .95),
                     "p99": pct(ft, .99), "max": max(ft), "stdev": statistics.pstdev(ft)},
        "displayed_ms": {"p50": pct(shown, .5), "p99": pct(shown, .99), "max": max(shown) if shown else None},
        "shown_once": sum(1 for h in held if h == 1),
        "held_2plus": sum(1 for h in held if h >= 2),
        "not_shown": sum(1 for d in disp if d is None or d <= 0),
        "display_latency_ms": {"mean": statistics.fmean(lat) if lat else None, "p99": pct(lat, .99)},
        "cpu_busy_ms": {"mean": statistics.fmean(cpu) if cpu else None, "p99": pct(cpu, .99)},
        "gpu_busy_ms": {"mean": statistics.fmean(gpu) if gpu else None, "p99": pct(gpu, .99)},
    }
    # Where the stutters fall (seconds into the capture), to line up with game events.
    t0 = num(rows[0]["CPUStartTime"]) or 0.0
    out["stutters_at_s"] = [round(((num(r["CPUStartTime"]) or 0) - t0) / 1000.0, 2)
                            for r, d in zip(rows, disp)
                            if d is not None and d > 1.5 * refresh][:40]
    out["drops_at_s"] = [round(((num(r["CPUStartTime"]) or 0) - t0) / 1000.0, 2)
                         for r, d in zip(rows, disp) if d is None or d <= 0][:40]
    return out


def plot(path, csv_path, hz=60.0):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    rows = [r for r in csv.DictReader(open(csv_path, encoding="utf-8-sig"))
            if r.get("Application", "").lower() == EXE]
    W, H, top = 1400, 400, 50.0  # ms at the top of the plot
    im = Image.new("RGB", (W, H), (20, 20, 24))
    d = ImageDraw.Draw(im)
    for ms, col in ((1000 / hz, (60, 90, 60)), (2000 / hz, (90, 60, 60))):
        y = H - ms / top * H
        d.line([(0, y), (W, y)], fill=col)
    n = len(rows)
    for i, r in enumerate(rows):
        x = i * (W - 1) / max(1, n - 1)
        for key, col in (("FrameTime", (90, 160, 255)), ("DisplayedTime", (255, 200, 60))):
            v = num(r.get(key))
            if v is None:
                if key == "DisplayedTime":
                    d.line([(x, H - 6), (x, H)], fill=(255, 60, 60))
                continue
            y = H - min(v, top) / top * H
            d.point((x, y), fill=col)
    d.text((6, 6), "blue: frame time (present to present)   yellow: displayed time   red ticks: not shown"
                   f"   lines: 1 and 2 refreshes   top = {top:.0f} ms", fill=(220, 220, 220))
    im.save(path)


def summary_line(name, s):
    if "error" in s:
        return f"{name}: {s['error']}"
    f, dsp = s["frame_ms"], s["displayed_ms"]
    return (f"{name}: {s['presents']} presents, mode {'/'.join(s['present_mode'])}; frame time "
            f"p50 {f['p50']:.2f} p99 {f['p99']:.2f} max {f['max']:.1f} sd {f['stdev']:.2f} ms; "
            f"shown once {s['shown_once']}, held 2+ refreshes {s['held_2plus']}, not shown "
            f"{s['not_shown']}; display latency {s['display_latency_ms']['mean']:.1f} ms; cpu busy "
            f"{s['cpu_busy_ms']['mean']:.2f}, gpu busy {s['gpu_busy_ms']['mean']:.2f} ms")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenario", default="fight")
    ap.add_argument("--frames", type=int, default=3600, help="guest frames to run")
    ap.add_argument("--skip", type=float, default=20.0, help="seconds before recording starts")
    ap.add_argument("--seconds", type=float, default=30.0, help="seconds recorded")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--hz", type=float, default=60.0, help="display refresh rate")
    ap.add_argument("--out", default=None)
    ap.add_argument("--analyze", help="summarise an existing PresentMon CSV")
    ap.add_argument("--compare", nargs="+", help="output folders to put side by side")
    ap.add_argument("extra", nargs="*", help="extra launcher flags after --")
    a = ap.parse_args()

    if a.analyze:
        s = analyze(a.analyze, a.hz)
        print(json.dumps(s, indent=1))
        print(summary_line(os.path.basename(a.analyze), s))
        return 0
    if a.compare:
        for d in a.compare:
            s = json.load(open(os.path.join(d, "pacing.json")))
            print(summary_line(os.path.basename(os.path.normpath(d)), s))
        return 0

    pm = find_presentmon()
    if not pm:
        print("PresentMon not found: winget install Intel.PresentMon.Console")
        return 1
    out = os.path.abspath(a.out or os.path.join(ROOT, "work", "pace", time.strftime("%H%M%S")))
    os.makedirs(out, exist_ok=True)
    run_dir = os.path.join(out, "run")
    cmd = [sys.executable, os.path.join(ROOT, "tools", "scenario.py"), a.scenario, "--frames",
           str(a.frames), "--shots", "", "--speed", "real", "--out", run_dir]
    for kv in a.set:
        cmd += ["--set", kv]
    for kv in a.env:
        cmd += ["--env", kv]
    if a.extra:
        cmd += ["--"] + a.extra
    game = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(a.skip)
    csv_path = os.path.join(out, "presentmon.csv")
    subprocess.run([pm, "--process_name", EXE, "--output_file", csv_path, "--timed",
                    str(int(a.seconds)), "--terminate_after_timed", "--no_console_stats",
                    "--v2_metrics", "--stop_existing_session"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    game.wait()
    s = analyze(csv_path, a.hz)
    s["set"] = a.set
    s["env"] = a.env
    try:
        log = open(os.path.join(run_dir, "run.log"), encoding="utf-8", errors="replace").read()
        s["engine_pacing"] = [l for l in log.splitlines() if l.startswith("pacing: ") and "frames," in l]
    except OSError:
        pass
    json.dump(s, open(os.path.join(out, "pacing.json"), "w"), indent=1)
    plot(os.path.join(out, "pacing.png"), csv_path, a.hz)
    print(summary_line(os.path.basename(out), s))
    if s.get("stutters_at_s"):
        print("stutters at (s):", s["stutters_at_s"])
    if s.get("drops_at_s"):
        print("not shown at (s):", s["drops_at_s"])
    print(f"output: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
