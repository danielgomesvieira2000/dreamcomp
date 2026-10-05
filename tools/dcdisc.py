#!/usr/bin/env python3
"""Dreamcast GD-ROM image reader: identify, list and extract a user's own disc.

Supports Redump-style .cue (single/high-density area with REM markers) and .gdi
descriptors with raw 2352-byte or cooked 2048-byte MODE1 tracks.

    python tools/dcdisc.py info    <disc.cue|disc.gdi>
    python tools/dcdisc.py ls      <disc>
    python tools/dcdisc.py extract <disc> <out_dir> [--only 1ST_READ.BIN]
    python tools/dcdisc.py ipbin   <disc>              # print IP.BIN meta as JSON

Nothing this tool produces may be committed (see docs/LEGAL.md).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import sys
from dataclasses import dataclass, field

HD_AREA_LBA = 45000          # first LBA of the high-density area on every GD-ROM
FAD_OFFSET = 150             # ISO9660 extents on GD-ROM are frame addresses (LBA + 150)


@dataclass
class Track:
    number: int
    mode: str            # "MODE1" | "AUDIO"
    sector_size: int     # 2352 or 2048
    start_lba: int
    path: str
    size: int = 0

    @property
    def sectors(self) -> int:
        return self.size // self.sector_size


@dataclass
class Disc:
    path: str
    tracks: list[Track] = field(default_factory=list)

    # -- sector access ---------------------------------------------------
    def track_for(self, lba: int) -> Track:
        for t in sorted(self.tracks, key=lambda t: t.start_lba, reverse=True):
            if lba >= t.start_lba:
                if lba - t.start_lba < t.sectors:
                    return t
                break
        raise ValueError(f"LBA {lba} is not inside any track")

    def read_sectors(self, lba: int, count: int) -> bytes:
        out = bytearray()
        t = self.track_for(lba)
        with open(t.path, "rb") as f:
            for i in range(count):
                rel = lba + i - t.start_lba
                if t.sector_size == 2352:
                    f.seek(rel * 2352 + 16)  # 12 sync + 4 header, MODE1 user data
                else:
                    f.seek(rel * 2048)
                out += f.read(2048)
        return bytes(out)

    def data_track(self) -> Track:
        hd = [t for t in self.tracks if t.start_lba >= HD_AREA_LBA and t.mode != "AUDIO"]
        if not hd:
            raise ValueError("no high-density data track: not a GD-ROM image")
        return hd[0]


def _msf_to_frames(msf: str) -> int:
    m, s, f = (int(x) for x in msf.split(":"))
    return (m * 60 + s) * 75 + f


def open_disc(path: str) -> Disc:
    base = os.path.dirname(os.path.abspath(path))
    disc = Disc(path)
    if path.lower().endswith(".gdi"):
        lines = [l.split() for l in open(path, encoding="utf-8", errors="replace").read().splitlines() if l.strip()]
        for parts in lines[1:]:
            num, lba, ttype, ssize = int(parts[0]), int(parts[1]), int(parts[2]), int(parts[3])
            fname = " ".join(parts[4:-1]).strip('"')
            p = os.path.join(base, fname)
            disc.tracks.append(Track(num, "AUDIO" if ttype == 0 else "MODE1", ssize, lba, p, os.path.getsize(p)))
        return disc

    # Redump cue: tracks are laid out back to back in the single-density area;
    # the high-density area starts at LBA 45000.
    cur_file, area_hd, next_lba = None, False, 0
    pending: Track | None = None
    for raw in open(path, encoding="utf-8", errors="replace"):
        line = raw.strip()
        if line.upper().startswith("REM HIGH-DENSITY AREA"):
            area_hd, next_lba = True, HD_AREA_LBA
        elif line.upper().startswith("FILE"):
            cur_file = os.path.join(base, re.match(r'FILE\s+"(.+)"', line).group(1))
        elif line.upper().startswith("TRACK"):
            _, num, mode = line.split()
            mode = "AUDIO" if mode == "AUDIO" else mode.split("/")[0]
            size = os.path.getsize(cur_file)
            ss = 2352 if mode == "AUDIO" or "2352" in line else 2048
            pending = Track(int(num), mode, ss, next_lba, cur_file, size)
            disc.tracks.append(pending)
            next_lba += size // ss
    return disc


# -- IP.BIN -----------------------------------------------------------------
IP_FIELDS = [  # (name, offset, length) — Sega boot header, first 256 bytes
    ("hardware_id", 0x00, 16), ("maker_id", 0x10, 16), ("device_info", 0x20, 16),
    ("area_symbols", 0x30, 8), ("peripherals", 0x38, 8), ("product_number", 0x40, 10),
    ("product_version", 0x4A, 6), ("release_date", 0x50, 16), ("boot_filename", 0x60, 16),
    ("company", 0x70, 16), ("title", 0x80, 128),
]


def parse_ipbin(data: bytes) -> dict:
    meta = {k: data[o:o + n].decode("ascii", "replace").strip() for k, o, n in IP_FIELDS}
    meta["sha1"] = hashlib.sha1(data[:0x8000]).hexdigest()
    return meta


# -- ISO9660 ------------------------------------------------------------------
@dataclass
class Entry:
    path: str
    lba: int     # absolute LBA
    size: int
    is_dir: bool


def list_files(disc: Disc) -> list[Entry]:
    hd = disc.data_track()
    pvd = disc.read_sectors(hd.start_lba + 16, 1)
    if pvd[1:6] != b"CD001":
        raise ValueError("no ISO9660 primary volume descriptor at HD+16")
    root = pvd[156:156 + 34]
    root_ext = struct.unpack_from("<I", root, 2)[0]
    # Extents are absolute LBAs on Redump images but frame addresses (LBA+150) on
    # some rips: pick the interpretation whose root "." record points at itself.
    fixup = None
    for cand in (0, -FAD_OFFSET):
        try:
            probe = disc.read_sectors(root_ext + cand, 1)
        except ValueError:
            continue
        if struct.unpack_from("<I", probe, 2)[0] == root_ext:
            fixup = cand
            break
    if fixup is None:
        raise ValueError("cannot locate the ISO9660 root directory")
    out: list[Entry] = []

    def walk(ext: int, size: int, prefix: str):
        data = disc.read_sectors(ext + fixup, (size + 2047) // 2048)
        pos = 0
        while pos < len(data):
            rlen = data[pos]
            if rlen == 0:
                pos = (pos // 2048 + 1) * 2048
                continue
            e_ext, e_size = struct.unpack_from("<I", data, pos + 2)[0], struct.unpack_from("<I", data, pos + 10)[0]
            flags, nlen = data[pos + 25], data[pos + 32]
            name = data[pos + 33:pos + 33 + nlen]
            pos += rlen
            if name in (b"\x00", b"\x01"):
                continue
            n = name.decode("ascii", "replace").split(";")[0]
            p = f"{prefix}/{n}" if prefix else n
            is_dir = bool(flags & 2)
            out.append(Entry(p, e_ext + fixup, e_size, is_dir))
            if is_dir:
                walk(e_ext, e_size, p)

    walk(root_ext, struct.unpack_from("<I", root, 10)[0], "")
    return out


def read_file(disc: Disc, e: Entry) -> bytes:
    return disc.read_sectors(e.lba, (e.size + 2047) // 2048)[: e.size]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["info", "ls", "extract", "ipbin"])
    ap.add_argument("disc")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--only", action="append", help="extract only these paths (repeatable)")
    a = ap.parse_args(argv)
    disc = open_disc(a.disc)
    hd = disc.data_track()
    ip = parse_ipbin(disc.read_sectors(hd.start_lba, 16))

    if a.cmd in ("info", "ipbin"):
        if a.cmd == "info":
            for t in disc.tracks:
                print(f"track {t.number:02d} {t.mode:5s} {t.sector_size} lba={t.start_lba:6d} sectors={t.sectors}")
        print(json.dumps(ip, indent=2))
        if a.cmd == "info":
            files = list_files(disc)
            boot = next((f for f in files if f.path.upper() == ip["boot_filename"].upper()), None)
            if boot:
                print(f"boot file {boot.path}: {boot.size} bytes, sha1 {hashlib.sha1(read_file(disc, boot)).hexdigest()}")
        return 0
    files = list_files(disc)
    if a.cmd == "ls":
        for f in files:
            print(f"{'d' if f.is_dir else '-'} {f.lba:7d} {f.size:10d} {f.path}")
        return 0
    if not a.out:
        ap.error("extract needs an output directory")
    only = {o.upper() for o in (a.only or [])}
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "IP.BIN"), "wb") as fh:
        fh.write(disc.read_sectors(hd.start_lba, 16))
    n = 0
    for f in files:
        if only and f.path.upper() not in only:
            continue
        dst = os.path.join(a.out, *f.path.split("/"))
        if f.is_dir:
            os.makedirs(dst, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as fh:
            fh.write(read_file(disc, f))
        n += 1
    print(f"extracted {n} files to {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
