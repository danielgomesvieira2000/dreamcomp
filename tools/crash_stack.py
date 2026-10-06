#!/usr/bin/env python3
"""Stack of the crashing thread in a dreamcomp minidump, resolved against the linker map.

    .venv/Scripts/python tools/crash_stack.py [DUMP] [--port ports/soulcalibur-recomp]

DUMP defaults to the newest %APPDATA%/dream-recomp/dream-recomp/crash-*.dmp. Without debug
symbols a frame-by-frame unwind is not possible, so this scans the crashing thread's stack
memory from RSP upward and lists every value that points into the game executable's code,
named with its map (tools/crash_lookup.py): the callers, plus some stale values. The first few
are the most reliable. Needs `pip install minidump` (in .venv).

A hang dump (no exception record), or --all: every thread's instruction pointer and the code
addresses found on its stack. --map names the linker map of the build that wrote the dump (the
port's current build/game/*.map otherwise).
"""
import argparse
import bisect
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import crash_lookup  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump", nargs="?")
    ap.add_argument("--port", default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                                   "ports", "soulcalibur-recomp"))
    ap.add_argument("--words", type=int, default=4096, help="stack words to scan")
    ap.add_argument("--show", type=int, default=40)
    ap.add_argument("--all", action="store_true", help="every thread, not only the faulting one")
    ap.add_argument("--map", help="linker map of the build that wrote the dump")
    a = ap.parse_args()
    from minidump.minidumpfile import MinidumpFile

    dump = a.dump
    if not dump:
        dumps = sorted(glob.glob(os.path.join(os.environ.get("APPDATA", ""), "dream-recomp", "dream-recomp", "crash-*.dmp")),
                       key=os.path.getmtime)
        if not dumps:
            sys.exit("no crash dumps")
        dump = dumps[-1]
    mf = MinidumpFile.parse(dump)
    reader = mf.get_reader()
    exc = mf.exception.exception_records[0] if mf.exception else None
    tid = exc.ThreadId if exc else None
    rec = exc.ExceptionRecord if exc else None
    if rec:
        print(f"{dump}\nexception {rec.ExceptionCode} at 0x{rec.ExceptionAddress:x}, thread {tid}")
    else:
        print(f"{dump}\nno exception record (a hang dump): every thread")
    modules = [(m.baseaddress, m.baseaddress + m.size, os.path.basename(m.name)) for m in mf.modules.modules]

    def module_of(addr):
        for lo, hi, name in modules:
            if lo <= addr < hi:
                return lo, name
        return None, None

    exe = next((m for m in modules if m[2].lower().endswith(".exe")), None)
    maps = [a.map] if a.map else glob.glob(os.path.join(a.port, "build", "game", "*.map"))
    syms = crash_lookup.load(maps[0]) if maps else []
    rvas = [s[0] for s in syms]

    def name(addr):
        lo, mod = module_of(addr)
        if mod is None:
            return None
        off = addr - lo
        if exe and mod == exe[2] and syms:
            i = bisect.bisect_right(rvas, off) - 1
            if i >= 0:
                return f"{mod}+0x{off:x}  {syms[i][1]} + 0x{off - syms[i][0]:x}"
        return f"{mod}+0x{off:x}"

    if rec:
        print("faulting:", name(rec.ExceptionAddress))

    def scan(thread, show):
        # The thread's RSP from its context, then the code addresses on its stack.
        ctx = thread.ContextObject
        rip = getattr(ctx, "Rip", None)
        rsp = getattr(ctx, "Rsp", None)
        if rip:
            print(f"  rip {name(rip) or hex(rip)}")
        if not rsp:
            print("  no context")
            return
        shown = 0
        for i in range(a.words):
            try:
                data = reader.read(rsp + 8 * i, 8)
            except Exception:
                break
            v = struct.unpack("<Q", data)[0]
            lo, mod = module_of(v)
            if mod is None:
                continue
            print(f"  [rsp+0x{8 * i:04x}] {name(v)}")
            shown += 1
            if shown >= show:
                break

    for thread in mf.threads.threads:
        if rec and not a.all and thread.ThreadId != tid:
            continue
        print(f"thread {thread.ThreadId}{' (faulting)' if thread.ThreadId == tid else ''}")
        scan(thread, a.show if (rec and thread.ThreadId == tid) or not (rec or a.all) else min(a.show, 12))


if __name__ == "__main__":
    main()
