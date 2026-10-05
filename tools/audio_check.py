#!/usr/bin/env python3
"""Find what makes recorded audio crackle: clipping and clicks, with timestamps.

    python tools/audio_check.py run.wav [--per-second] [--top 20]

Input: the engine's --wav output (16-bit stereo 44.1 kHz). Reports, per channel:

  clipped    samples at or within 1 LSB of full scale (the AICA mixer saturates at 16 bits;
             runs of clipped samples are audible as crackle on loud passages)
  clicks     isolated discontinuities: a sample whose second difference is far above the local
             level (an impulse in otherwise smooth audio), e.g. a channel starting mid-wave, a
             loop point that does not join up, or a sample-rate glitch
  dc         mean offset

--per-second lists the seconds that contain clipping or clicks, so a run with --screenshot-at or
--dump-ta at those frames can show what the game was doing. No numpy needed.
"""
import argparse
import array
import struct
import sys
import wave


def load(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            sys.exit(f"{path}: expected 16-bit samples")
        ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        data = array.array("h")
        data.frombytes(w.readframes(n))
    if sys.byteorder == "big":
        data.byteswap()
    return ch, rate, data


def analyse(chan, rate, top):
    n = len(chan)
    clipped = 0
    clip_runs = 0
    in_run = False
    clips_at = []
    for i, v in enumerate(chan):
        c = v >= 32766 or v <= -32767
        if c:
            clipped += 1
            if not in_run:
                clip_runs += 1
                clips_at.append(i)
        in_run = c
    # Clicks: |second difference| compared with the median-ish local level over a window.
    clicks = []
    win = 64
    d2 = [0] * n
    for i in range(1, n - 1):
        d2[i] = abs(chan[i + 1] - 2 * chan[i] + chan[i - 1])
    # Running mean of d2 over +-win as the local level.
    pref = [0]
    for v in d2:
        pref.append(pref[-1] + v)
    for i in range(win, n - win):
        v = d2[i]
        if v < 3000:
            continue
        local = (pref[i + win] - pref[i - win] - v) / (2 * win - 1)
        if v > 12 * max(local, 40):
            clicks.append((i, v, local))
    # Keep one click per 5 ms.
    merged = []
    for c in clicks:
        if not merged or c[0] - merged[-1][0] > rate // 200:
            merged.append(c)
    return {
        "n": n,
        "clipped": clipped,
        "clip_runs": clip_runs,
        "clips_at": clips_at,
        "clicks": merged,
        "dc": sum(chan) / n if n else 0,
        "peak": max((abs(v) for v in chan), default=0),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav")
    ap.add_argument("--per-second", action="store_true")
    ap.add_argument("--top", type=int, default=10)
    a = ap.parse_args()
    ch, rate, data = load(a.wav)
    secs = len(data) / ch / rate
    print(f"{a.wav}: {secs:.1f} s, {ch} ch, {rate} Hz")
    per_sec = {}
    for c in range(ch):
        chan = data[c::ch]
        r = analyse(chan, rate, a.top)
        name = "LR"[c] if ch == 2 else str(c)
        print(f"  {name}: peak {r['peak']}, dc {r['dc']:.1f}, clipped {r['clipped']} samples "
              f"({100.0 * r['clipped'] / max(1, r['n']):.3f} %) in {r['clip_runs']} runs, "
              f"{len(r['clicks'])} clicks")
        for i in r["clips_at"]:
            per_sec.setdefault(i // rate, [0, 0])[0] += 1
        for i, v, local in r["clicks"]:
            per_sec.setdefault(i // rate, [0, 0])[1] += 1
        for i, v, local in sorted(r["clicks"], key=lambda t: -t[1] / max(t[2], 40))[: a.top]:
            print(f"     click at {i / rate:8.3f} s (sample {i}): jump {v}, local level {local:.0f}")
    if a.per_second:
        print("  second: clip runs / clicks")
        for s in sorted(per_sec):
            cr, ck = per_sec[s]
            print(f"    {s:4d} s (frame ~{int(s * 59.94)}): {cr:5d} / {ck:4d}")


if __name__ == "__main__":
    main()
