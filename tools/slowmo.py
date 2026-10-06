#!/usr/bin/env python3
"""Slow-motion clip of frame interpolation, to judge 120 fps motion on a 60 Hz screen.

Runs a scenario with interpolation forced on (fps=120) and DREAM_SHOT_INTERP=1, screenshots a run
of consecutive presented frames (each real frame plus the blended frame shown before it), and
plays them back at 60 fps, i.e. half speed, side by side:

    left   60 fps: game frames only, each held for two slots (what a 60 Hz display shows)
    right 120 fps: blended, real, blended, real ... (what a 120 Hz display shows)

    python tools/slowmo.py                                   # fight, frames 2400-2519
    python tools/slowmo.py --scenario fight --start 3400 --count 180 --out work/slowmo-b
    python tools/slowmo.py --set aspect=expanded             # any scenario.py --set

Writes <out>/slowmo.mp4 (needs `pip install imageio-ffmpeg` in .venv; otherwise slowmo.webp),
and keeps the frames in <out>/frames/. Output is game footage: never commit it.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenario", default="fight")
    ap.add_argument("--start", type=int, default=2400, help="first presented frame")
    ap.add_argument("--count", type=int, default=120, help="game frames (the clip is 2x as long)")
    ap.add_argument("--out", default=os.path.join(ROOT, "work", "slowmo"))
    ap.add_argument("--set", action="append", default=[], help="setting for the run (scenario.py --set)")
    ap.add_argument("--width", type=int, default=640, help="width of each panel")
    a = ap.parse_args()

    from PIL import Image, ImageDraw

    out = os.path.abspath(a.out)
    frames_dir = os.path.join(out, "frames")
    shots = ",".join(str(n) for n in range(a.start, a.start + a.count))
    cmd = [sys.executable, os.path.join(ROOT, "tools", "scenario.py"), a.scenario,
           "--frames", str(int((a.start + a.count) * 1.1) + 120), "--shots", shots,
           "--out", frames_dir, "--set", "fps=120", "--env", "DREAM_SHOT_INTERP=1"]
    for kv in a.set:
        cmd += ["--set", kv]
    print("capturing:", a.scenario, f"frames {a.start}-{a.start + a.count - 1}")
    subprocess.run(cmd, check=False, stdout=subprocess.DEVNULL)

    real, blend = {}, {}
    for n in range(a.start, a.start + a.count):
        r = os.path.join(frames_dir, f"shot_{n:05d}.png")
        b = os.path.join(frames_dir, f"shot_{n:05d}_interp.png")
        if os.path.exists(r):
            real[n] = r
        if os.path.exists(b):
            blend[n] = b
    if len(real) < 2:
        print(f"only {len(real)} frames captured; see {frames_dir}/run.log")
        return 1
    print(f"{len(real)} game frames, {len(blend)} blended (missing ones: camera cuts or not drawn)")

    cache = {}

    def load(path):
        if path not in cache:
            im = Image.open(path).convert("RGB")
            h = round(im.height * a.width / im.width) // 2 * 2
            cache[path] = im.resize((a.width, h), Image.LANCZOS)
        return cache[path]

    ns = sorted(real)
    sample = load(real[ns[0]])
    W, H = a.width * 2, sample.height + 24
    video = []
    for i, n in enumerate(ns):
        prev = real[ns[i - 1]] if i else real[n]
        # Slot 1: the blended frame (right) while a 60 Hz display still shows the previous frame.
        # Slot 2: the real frame on both sides.
        for left, right, tag in ((prev, blend.get(n, prev), "blended" if n in blend else "no blend"),
                                 (real[n], real[n], "game frame")):
            canvas = Image.new("RGB", (W, H))
            canvas.paste(load(left), (0, 24))
            canvas.paste(load(right), (a.width, 24))
            d = ImageDraw.Draw(canvas)
            d.text((6, 6), "60 fps (game frames only)", fill=(255, 255, 255))
            d.text((a.width + 6, 6), f"120 fps (with in-between frames)  f{n}  {tag}",
                   fill=(255, 220, 0) if tag == "blended" else (255, 255, 255))
            d.text((W - 150, 6), "half speed", fill=(160, 160, 160))
            video.append(canvas)

    try:
        import imageio_ffmpeg
        path = os.path.join(out, "slowmo.mp4")
        w = imageio_ffmpeg.write_frames(path, (W, H), fps=60, codec="libx264", quality=9,
                                        macro_block_size=2)
        w.send(None)
        for im in video:
            w.send(im.tobytes())
        w.close()
    except ImportError:
        path = os.path.join(out, "slowmo.webp")
        video[0].save(path, save_all=True, append_images=video[1:], duration=1000 // 60, loop=0,
                      quality=90)
    print(f"wrote {path}: {len(video)} slots, {len(video) / 60:.1f} s at 60 fps "
          f"({len(ns) / 60:.1f} s of game time)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
