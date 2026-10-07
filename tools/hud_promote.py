#!/usr/bin/env python3
"""Promote HUD overrides saved in game (F1 editor) into the port, so every player gets them.

    python tools/hud_promote.py soulcalibur                 # the player's file -> the port's
    python tools/hud_promote.py ports/soulcalibur-recomp --dry-run
    python tools/hud_promote.py soulcalibur --from work/hudtest/run/hud_overrides.ini --keep

The F1 editor saves to the player's settings folder (`<settings dir>/hud_overrides.ini`, e.g.
%APPDATA%/dreamcomp/<id>/). The port ships `game/hud_overrides.ini` beside `<id>.toml`; the game
loads the port's file first and the player's after it, so the player's lines win (docs/HUD.md).

Merging: a player line with the same rectangle and textures as a port line replaces that line's
anchor; any other is appended. Afterwards the player's file is emptied (a .bak copy is kept) so
it does not mask later changes to the port's file -- unless --keep.

The port file holds rectangles in console coordinates and texture words: numbers that describe
where the game draws, not game data (docs/LEGAL.md). Commit it with the port.
"""
import argparse
import glob
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = ("# dreamcomp HUD overrides (docs/HUD.md): rect in the console's 640x480 coordinates\n"
          "# before correction, anchor left|center|right|stretch|auto, tcw = texture words of\n"
          "# the element's pieces (none: any). Later lines win. Edited with F1 in game and\n"
          "# promoted with tools/hud_promote.py.\n")
ANCHORS = {"auto", "left", "center", "right", "stretch"}


def port_dir(arg):
    if os.path.isdir(arg):
        return os.path.abspath(arg)
    cand = os.path.join(ROOT, "ports", f"{arg}-recomp")
    if os.path.isdir(cand):
        return cand
    sys.exit(f"no port {arg!r} (give ports/<slug>-recomp or <slug>)")


def port_id(pdir):
    tomls = [t for t in glob.glob(os.path.join(pdir, "game", "*.toml")) if not t.endswith("suggested.toml")]
    if len(tomls) != 1:
        sys.exit(f"{pdir}: expected exactly one game/<id>.toml")
    return os.path.splitext(os.path.basename(tomls[0]))[0]


def user_file(gid):
    if sys.platform == "win32":
        base = os.environ.get("APPDATA", "")
    elif sys.platform == "darwin":
        base = os.path.join(os.environ.get("HOME", ""), "Library", "Application Support")
    else:
        base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.environ.get("HOME", ""), ".config")
    return os.path.join(base, "dreamcomp", gid, "hud_overrides.ini")


def parse(path):
    """[(rect tuple, anchor, tcw tuple, original line)]"""
    out = []
    if not os.path.exists(path):
        return out
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        body = line.split("#", 1)[0].strip()
        if not body:
            continue
        fields = dict(w.split("=", 1) for w in body.split() if "=" in w)
        try:
            rect = tuple(float(v) for v in fields["rect"].split(","))
            anchor = fields["anchor"]
            assert len(rect) == 4 and anchor in ANCHORS
        except (KeyError, ValueError, AssertionError):
            sys.exit(f"{path}:{n}: expected rect=x0,y0,x1,y1 anchor=... [tcw=...]: {line.strip()}")
        # A word whose pixel-format field is 7 is float data from an untextured piece, never a
        # texture: it is saved as 0, which matches untextured pieces (docs/HUD.md).
        tcws = tuple(sorted({clean(int(t, 0)) for t in fields.get("tcw", "").split(",") if t}))
        full = fields.get("full", "") in ("1", "true")
        out.append((rect, anchor, tcws, full))
    return out


def clean(w):
    return 0 if (w >> 27) & 7 == 7 else w


def overlap(a, b):
    """Share of the smaller rectangle covered by the other."""
    ix = max(0.0, min(a[2], b[2]) - max(a[0], b[0]))
    iy = max(0.0, min(a[3], b[3]) - max(a[1], b[1]))
    small = min((a[2] - a[0]) * (a[3] - a[1]), (b[2] - b[0]) * (b[3] - b[1]))
    return ix * iy / small if small > 0 else 0.0


def merge(entries):
    """One entry per piece: same anchor, textures and full flag, rectangles overlapping by half or
    more -> their union (the same piece saved again a pixel apart)."""
    out = []
    for e in entries:
        rect, anchor, tcws, full = e
        for i, (r, an, t, fu) in enumerate(out):
            if an == anchor and t == tcws and fu == full and overlap(r, rect) >= 0.5:
                out[i] = ((min(r[0], rect[0]), min(r[1], rect[1]), max(r[2], rect[2]), max(r[3], rect[3])),
                          an, t, fu)
                break
        else:
            out.append(e)
    return out


def fmt(entry):
    rect, anchor, tcws, full = entry
    s = "rect=%s anchor=%s" % (",".join("%g" % v for v in rect), anchor)
    if tcws:
        s += " tcw=" + ",".join("0x%08x" % t for t in tcws)
    if full:
        s += " full=1"
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--from", dest="src", help="the player's file (default: the settings folder's)")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--keep", action="store_true", help="leave the player's file as it is")
    a = ap.parse_args()

    pdir = port_dir(a.port)
    gid = port_id(pdir)
    src = a.src or user_file(gid)
    dst = os.path.join(pdir, "game", "hud_overrides.ini")
    user = parse(src)
    if not user:
        print(f"nothing to promote: {src} has no overrides")
        return 0
    port = parse(dst)
    added = updated = 0
    for entry in user:
        rect, anchor, tcws, full = entry
        for i, (r, an, t, fu) in enumerate(port):
            if r == rect and t == tcws:
                if an != anchor or fu != full:
                    port[i] = entry
                    updated += 1
                break
        else:
            port.append(entry)
            added += 1
    before = len(port)
    port = merge(port)
    merged = before - len(port)
    print(f"{src}: {len(user)} override(s) -> {dst}: {added} added, {updated} updated, "
          f"{merged} merged as repeats, {len(port)} in all")
    for e in port:
        print("  " + fmt(e))
    if a.dry_run:
        print("(dry run: nothing written)")
        return 0
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write(HEADER)
        for e in port:
            f.write(fmt(e) + "\n")
    if not a.keep:
        shutil.copy(src, src + ".bak")
        with open(src, "w", encoding="utf-8", newline="\n") as f:
            f.write(HEADER)
        print(f"emptied {src} (copy in {src}.bak); the port's file now carries them")
    print(f"commit {os.path.relpath(dst, pdir)} in the port repository")
    return 0


if __name__ == "__main__":
    sys.exit(main())
