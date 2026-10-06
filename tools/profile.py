#!/usr/bin/env python3
"""Where the game thread's host time goes: names and summarises a --profile capture.

    python tools/scenario.py fight --speed real --out work/prof/a -- --profile work/prof/a.prof
    python tools/profile.py work/prof/a.prof                  # port found from the frames
    python tools/profile.py work/prof/a.prof --top 40 --collapsed work/prof/a.folded
    python tools/profile.py work/prof/a.prof --compare work/prof/b.prof

The engine's sampler (--profile FILE, ~600-1000 samples a second, Windows) records the game
thread's whole call stack as module+offset frames. This names the game executable's frames from its
linker map (the build writes game/<id>.map; MSVC names are undecorated with undname.exe when the
build tools have it) and prints:

    categories      what kind of work each sample was doing: the game's translated code, memory
                    access, sound, rendering, the graphics driver, sleeping in the pacing...
    self            the innermost function of ours (or the system module) per sample
    inclusive       every function of ours on the stack, counted once per sample
    guest functions the Dreamcast functions (gen::fn_XXXXXXXX), inclusive and as the innermost
                    guest frame (its own code plus the engine work it calls)

Percentages are of busy time (samples not sleeping in the pacing) unless --all. --collapsed writes
outermost-first folded stacks for https://www.speedscope.app (or flamegraph.pl).
"""
import argparse
import bisect
import collections
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_map(exe_module):
    stem = os.path.splitext(exe_module)[0]
    hits = glob.glob(os.path.join(ROOT, "ports", "*", "build", "game", stem + ".map"))
    return hits[0] if hits else None


def load_map(path):
    base = 0x140000000
    syms = []
    pat = re.compile(r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s+(?:f\s+)?(\S+)?")
    with open(path, encoding="latin-1") as f:
        for line in f:
            m = re.search(r"Preferred load address is ([0-9a-fA-F]+)", line)
            if m:
                base = int(m.group(1), 16)
                continue
            m = pat.match(line)
            if m:
                va = int(m.group(2), 16)
                if va >= base:
                    syms.append((va - base, m.group(1)))
    syms.sort()
    return [s[0] for s in syms], [s[1] for s in syms]


def find_undname():
    pats = [r"C:\Program Files (x86)\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\undname.exe",
            r"C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\undname.exe"]
    for p in pats:
        hits = sorted(glob.glob(p))
        if hits:
            return hits[-1]
    return None


def simple_demangle(name):
    # ?fn_0c2246a0@gen@dream@@YAX... -> dream::gen::fn_0c2246a0
    if name.startswith("?") and not name.startswith("??"):
        head = name[1:].split("@@")[0]
        parts = [p for p in head.split("@") if p]
        if parts:
            return "::".join(reversed(parts))
    return name


def demangle_all(names):
    out = {n: simple_demangle(n) for n in names}
    und = find_undname()
    if not und:
        return out
    todo = [n for n in names if n.startswith("?")]
    # Batches under Windows' 32 KB command line.
    chunks, cur, size = [], [], 0
    for n in todo:
        if cur and size + len(n) + 3 > 24000:
            chunks.append(cur)
            cur, size = [], 0
        cur.append(n)
        size += len(n) + 3
    if cur:
        chunks.append(cur)
    for chunk in chunks:
        try:
            res = subprocess.run([und, "0x2"] + chunk, capture_output=True, text=True, timeout=60)
        except (OSError, subprocess.TimeoutExpired):
            break
        # undname prints 'Undecoration of :- "X"' then 'is :- "Y"'.
        got = re.findall(r'Undecoration of :- "(.*?)"\s*is :- "(.*?)"', res.stdout, re.S)
        for src, dst in got:
            dst = dst.replace("struct ", "").replace("class ", "").replace("__cdecl ", "")
            out[src] = dst
    return out


GUEST = re.compile(r"\bfn_([0-9a-f]{8})(?:__resume)?\b")


def short(name):
    """A readable label: guest functions as fn_XXXXXXXX, C++ names without their arguments."""
    m = GUEST.search(name)
    if m and "gen::" in name:
        return "fn_" + m.group(1) + (" (resume)" if "__resume" in name else "")
    # Drop the argument list (the first '(' outside template brackets) and everything before the
    # qualified name (access, return type: the last space outside brackets).
    depth, cut, last_space = 0, len(name), -1
    for i, ch in enumerate(name):
        if ch in "<`":
            depth += 1
        elif ch in ">'":
            depth = max(0, depth - 1)
        elif ch == "(" and depth == 0 and i > 0:
            cut = i
            break
        elif ch == " " and depth == 0:
            last_space = i
    n = name[last_space + 1:cut] if last_space < cut else name[:cut]
    return n[:110]


def category(frames):
    """frames: innermost first, as (module, label)."""
    labels = [l for _, l in frames]
    leaf_mod = frames[0][0]
    first_ours = next((l for m, l in frames if m == "exe"), "")
    if any(k in l for l in labels[:12] for k in ("precise_sleep_until", "pacing_sleep_until", "sleep_until", "sleep_for")):
        return "sleeping (pacing)"
    if leaf_mod != "exe":
        low = leaf_mod.lower()
        if any(k in low for k in ("igvk", "igc", "vulkan-1", "graphics-hook", "d3d11", "dxgi", "igd", "win32u", "gdi32")):
            return "graphics driver / Vulkan / D3D"
        if "sdl3" in low:
            return "SDL (window, input, audio)"
    f = first_ours
    if f.startswith("fn_"):
        return "guest code (translated)"
    if "devinterp" in f:
        return "guest code (interpreter)"
    if any(k in f for k in ("dream::mem::", "DcMemory", "dream::Memory", "Memory::")):
        return "guest memory access (engine)"
    if "dream::aica" in f or "arm7" in f.lower():
        return "sound (AICA / ARM7)"
    if any(k in f for k in ("dream::render", "dream::pvr", "Renderer", "TextureCache", "texture")):
        return "PVR / renderer (CPU side)"
    if "dream::sched" in f or "Scheduler" in f:
        return "scheduler"
    if "dream::sh4" in f:
        return "SH-4 runtime (dispatch, interrupts, FPU helpers)"
    if "dreamcomp" in f or "Live::" in f or "frontend" in f:
        return "host side (present, input, menu)"
    if leaf_mod != "exe":
        return "system (%s)" % leaf_mod
    return "engine, other"


def load_profile(path):
    header = ""
    stacks = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#"):
                header = line.strip("# \n")
                continue
            count, _, rest = line.strip().partition(" ")
            if not rest:
                continue
            frames = []
            for tok in rest.split(";"):
                mod, _, off = tok.rpartition("+")
                frames.append((mod, int(off, 16) if off else 0))
            stacks.append((int(count), frames))
    return header, stacks


def analyse(path, map_path=None):
    header, raw = load_profile(path)
    exe = next((m for _, fr in raw for m, _ in fr if m.lower().endswith(".exe")), None)
    map_path = map_path or (find_map(exe) if exe else None)
    addrs, names = load_map(map_path) if map_path else ([], [])
    pretty = demangle_all(sorted(set(names))) if names else {}

    def label(mod, off):
        if mod == exe and addrs:
            i = bisect.bisect_right(addrs, off) - 1
            if i >= 0:
                return ("exe", short(pretty.get(names[i], names[i])))
        return (mod, mod)

    cache = {}
    stacks = []
    for count, frames in raw:
        out = []
        for mod, off in frames:
            k = (mod, off)
            if k not in cache:
                cache[k] = label(mod, off)
            out.append(cache[k])
        stacks.append((count, out))
    return header, exe, map_path, stacks


def report(path, stacks, header, top, include_sleep):
    total = sum(c for c, _ in stacks)
    cats = collections.Counter()
    for c, fr in stacks:
        cats[category(fr)] += c
    sleep = cats.get("sleeping (pacing)", 0)
    busy = total - sleep
    denom = total if include_sleep else max(1, busy)
    print(f"{os.path.basename(path)}: {header}")
    print(f"busy {busy} of {total} samples ({100.0 * busy / max(1, total):.1f} %); "
          f"percentages below are of {'all' if include_sleep else 'busy'} samples\n")

    def table(title, counter, n):
        print(title)
        for name, c in counter.most_common(n):
            print(f"  {100.0 * c / denom:6.2f} %  {c:7d}  {name}")
        print()

    cats_shown = collections.Counter({k: v for k, v in cats.items() if include_sleep or k != "sleeping (pacing)"})
    table("categories", cats_shown, 20)

    self_c, incl_c, gin_c, gself_c = (collections.Counter() for _ in range(4))
    for c, fr in stacks:
        if not include_sleep and category(fr) == "sleeping (pacing)":
            continue
        # Self: the innermost frame of ours, or the system module the sample was in.
        leaf_mod, leaf = fr[0]
        if leaf_mod != "exe":
            ours = next((l for m, l in fr if m == "exe"), "?")
            self_c[f"[{leaf_mod}] <- {ours}"] += c
        else:
            self_c[leaf] += c
        seen = set()
        for m, l in fr:
            if m == "exe" and l not in seen:
                seen.add(l)
                incl_c[l] += c
        guests = [l for m, l in fr if m == "exe" and l.startswith("fn_")]
        for g in set(guests):
            gin_c[g] += c
        if guests:
            gself_c[guests[0]] += c
    table(f"self (top {top})", self_c, top)
    table(f"inclusive (top {top})", incl_c, top)
    table(f"guest functions, innermost: their code and the engine work they call (top {top})", gself_c, top)
    table(f"guest functions, inclusive (top {top})", gin_c, top)
    return cats, denom


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("profile")
    ap.add_argument("--map", help="linker map of the game executable (found from the frames)")
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--all", action="store_true", help="percentages of all samples, sleep included")
    ap.add_argument("--collapsed", help="write folded stacks (outermost first) for speedscope")
    ap.add_argument("--compare", help="a second profile: categories side by side")
    a = ap.parse_args()

    header, exe, map_path, stacks = analyse(a.profile, a.map)
    if not map_path:
        print("no linker map found for the game executable: frames stay as offsets (pass --map)")
    cats, denom = report(a.profile, stacks, header, a.top, a.all)
    if a.collapsed:
        folded = collections.Counter()
        for c, fr in stacks:
            folded[";".join(l.replace(";", ",") for _, l in reversed(fr))] += c
        with open(a.collapsed, "w", encoding="utf-8") as f:
            for k, v in folded.items():
                f.write(f"{k} {v}\n")
        print(f"wrote {a.collapsed} (open it in https://www.speedscope.app)")
    if a.compare:
        h2, _, _, s2 = analyse(a.compare, a.map)
        c2 = collections.Counter()
        for c, fr in s2:
            c2[category(fr)] += c
        sl2 = c2.get("sleeping (pacing)", 0)
        d2 = sum(c2.values()) if a.all else max(1, sum(c2.values()) - sl2)
        print(f"{'category':55s} {os.path.basename(a.profile):>14s} {os.path.basename(a.compare):>14s}")
        for k in sorted(set(cats) | set(c2), key=lambda k: -cats.get(k, 0)):
            if not a.all and k == "sleeping (pacing)":
                continue
            print(f"{k:55s} {100.0 * cats.get(k, 0) / denom:13.2f}% {100.0 * c2.get(k, 0) / d2:13.2f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
