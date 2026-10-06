#!/usr/bin/env python3
"""Name the function a crash report points at.

    python tools/crash_lookup.py ports/soulcalibur-recomp 0x18f3c [0x...]
    python tools/crash_lookup.py path/to/game.map 0x18f3c

The crash handler prints "crash: exception ... at ADDR (game.exe + 0xOFFSET)". OFFSET is an RVA;
the linker map (written beside the executable, /MAP) lists every function's RVA. This prints the
function containing each offset, with the distance into it, and the demangled-ish name.
"""
import bisect
import glob
import os
import re
import sys


def load(map_path):
    base = 0x140000000
    syms = []
    pat = re.compile(r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s+(?:f\s+)?(\S+)?")
    with open(map_path, encoding="latin-1") as f:
        for line in f:
            m = re.search(r"Preferred load address is ([0-9a-fA-F]+)", line)
            if m:
                base = int(m.group(1), 16)
                continue
            m = pat.match(line)
            if m:
                va = int(m.group(2), 16)
                if va >= base:
                    syms.append((va - base, m.group(1), m.group(3) or ""))
    syms.sort()
    return syms


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    target = sys.argv[1]
    if os.path.isdir(target):
        maps = glob.glob(os.path.join(target, "build", "game", "*.map"))
        if not maps:
            sys.exit(f"no .map under {target}/build/game (build the port first)")
        target = maps[0]
    syms = load(target)
    rvas = [s[0] for s in syms]
    for arg in sys.argv[2:]:
        off = int(arg, 16)
        i = bisect.bisect_right(rvas, off) - 1
        if i < 0:
            print(f"{arg}: before the first symbol")
            continue
        rva, name, obj = syms[i]
        print(f"{arg}: {name} + 0x{off - rva:x}  ({obj})")


if __name__ == "__main__":
    main()
