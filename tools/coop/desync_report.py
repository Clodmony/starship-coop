#!/usr/bin/env python3
"""Reads desync reports (WP-23): the files a game writes when two clients went out of step.

    python tools/coop/desync_report.py show <report>           # the header, and the dump's chunks per region
    python tools/coop/desync_report.py diff <a> <b>            # the first value that differs between two games' reports
    python tools/coop/desync_report.py diff <a> <b> --all      # every differing chunk

A report is `coop-desync-<frame>-player<n>.txt` in a game's own folder (src/port/coop/sync/desync_report.h): header
lines `key: value`, then the dump: `## <region>`, then for each chunk a line `name[index] size` and its bytes in hex.
The dump holds exactly the bytes the state hash ran over, so this tool recomputes the region hashes from it and
checks them against the `mine` line before it trusts a dump.

`diff` needs two reports of the same frame. It prints the regions that differ, then the first chunk that differs,
with the byte offset and both values, and exits 0 when it found one. Exit 1 when the dumps are equal or the reports do
not fit together, 2 when a file cannot be read.
"""
import argparse
import struct
import sys
from pathlib import Path

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK = (1 << 64) - 1


class Chunk:
    __slots__ = ("region", "name", "index", "size", "data")

    def __init__(self, region, name, index, size):
        self.region, self.name, self.index, self.size = region, name, index, size
        self.data = bytearray()

    def label(self):
        return f"{self.name}[{self.index}]" if self.index is not None else self.name


class Report:
    def __init__(self, path):
        self.path = Path(path)
        self.header = {}
        self.regions = []  # region names in order, from the `regions:` line
        self.chunks = []
        self.dump_line = None
        self.parse()

    def parse(self):
        lines = self.path.read_text(encoding="utf-8", errors="replace").splitlines()
        region = None
        chunk = None
        for line in lines:
            if not line:
                continue
            if chunk is not None and len(chunk.data) < chunk.size:
                chunk.data += bytes.fromhex(line.strip())
                continue
            if line.startswith("## "):
                region = line[3:].strip()
                chunk = None
                continue
            if line.startswith("#"):
                continue
            if region is None:
                key, _, value = line.partition(": ")
                if key == "dump":
                    self.dump_line = value
                else:
                    self.header[key] = value
                continue
            name, _, size = line.rpartition(" ")
            index = None
            if name.endswith("]") and "[" in name:
                name, _, rest = name.rpartition("[")
                index = int(rest[:-1])
            chunk = Chunk(region, name, index, int(size))
            self.chunks.append(chunk)
        self.regions = self.header.get("regions", "").split()[1:]
        for c in self.chunks:
            if len(c.data) != c.size:
                raise ValueError(f"{self.path.name}: chunk {c.label()} has {len(c.data)} of {c.size} bytes")

    def frame(self):
        return int(self.header.get("frame", "-1"))

    def hashes(self, key):
        """(total, [regions]) of the `mine` or `theirs` line, or None."""
        words = self.header.get(key, "").split()
        if len(words) < 1 + len(self.regions) or not all(len(w) == 16 for w in words[:1 + len(self.regions)]):
            return None
        return int(words[0], 16), [int(w, 16) for w in words[1:1 + len(self.regions)]]

    def region_chunks(self, region):
        return [c for c in self.chunks if c.region == region]

    def verify(self):
        """Recompute every region hash and the total from the dump. Returns a list of problems."""
        mine = self.hashes("mine")
        if mine is None:
            return ["no `mine` hashes in the header"]
        if not self.chunks:
            return ["no dump"]
        problems = []
        totals = []
        for i, region in enumerate(self.regions):
            h = FNV_OFFSET
            for c in self.region_chunks(region):
                for b in c.data:
                    h = ((h ^ b) * FNV_PRIME) & MASK
            totals.append(h)
            if h != mine[1][i]:
                problems.append(f"region {region}: the dump hashes to {h:016x}, the report says {mine[1][i]:016x}")
        h = FNV_OFFSET
        for b in struct.pack("<" + "Q" * len(totals), *totals):
            h = ((h ^ b) * FNV_PRIME) & MASK
        if h != mine[0]:
            problems.append(f"total: the dump hashes to {h:016x}, the report says {mine[0]:016x}")
        return problems


def word_at(data, offset):
    """The aligned 32-bit word that holds offset, as hex, int and float, for a human to read."""
    start = offset - offset % 4
    if start + 4 > len(data):
        return ""
    raw = bytes(data[start:start + 4])
    (u,) = struct.unpack("<I", raw)
    (i,) = struct.unpack("<i", raw)
    (f,) = struct.unpack("<f", raw)
    return f"word at +{start}: 0x{u:08x} = {i} = {f:g}"


def cmd_show(args):
    try:
        r = Report(args.report)
    except (OSError, ValueError) as e:
        print(f"desync: {e}")
        return 2
    for key, value in r.header.items():
        print(f"{key}: {value}")
    print(f"dump: {r.dump_line or 'none'}")
    for region in r.regions:
        chunks = r.region_chunks(region)
        if chunks:
            print(f"  {region}: {len(chunks)} chunks, {sum(c.size for c in chunks)} bytes, "
                  f"{chunks[0].label()} .. {chunks[-1].label()}")
    problems = r.verify()
    print("dump verified: the region hashes match the bytes" if not problems else "\n".join(problems))
    return 0 if not problems else 1


def cmd_diff(args):
    try:
        a, b = Report(args.a), Report(args.b)
    except (OSError, ValueError) as e:
        print(f"desync: {e}")
        return 2
    print(f"desync: {a.path.name}: {a.header.get('player', '?')}, frame {a.frame()}, {a.header.get('my state', '?')}")
    print(f"desync: {b.path.name}: {b.header.get('player', '?')}, frame {b.frame()}, {b.header.get('my state', '?')}")
    if a.frame() != b.frame():
        print(f"desync: the reports are of different frames, {a.frame()} and {b.frame()}: nothing to compare")
        return 1
    if a.regions != b.regions:
        print("desync: the reports list different regions: nothing to compare")
        return 1
    for r in (a, b):
        problems = r.verify()
        if problems:
            print(f"desync: {r.path.name}: the dump does not match its hashes:\n  " + "\n  ".join(problems))
            return 1
    print("desync: both dumps verified against their region hashes")
    ha, hb = a.hashes("mine"), b.hashes("mine")
    regions = [name for i, name in enumerate(a.regions) if ha[1][i] != hb[1][i]]
    print(f"desync: regions differing: {', '.join(regions) or 'none'}")
    for key, other in (("theirs", b), ("theirs", a)):
        pass
    first = None
    differing = 0
    for region in a.regions:
        ca, cb = a.region_chunks(region), b.region_chunks(region)
        if [c.label() for c in ca] != [c.label() for c in cb]:
            print(f"desync: region {region} has different chunks in the two reports")
            return 1
        for x, y in zip(ca, cb):
            if x.data == y.data:
                continue
            differing += 1
            offset = next(i for i in range(x.size) if x.data[i] != y.data[i])
            count = sum(1 for i in range(x.size) if x.data[i] != y.data[i])
            line = (f"  {region}: {x.label()} differs in {count} of {x.size} bytes, first at +{offset}: "
                    f"{x.data[offset]:02x} vs {y.data[offset]:02x}; {word_at(x.data, offset)} vs {word_at(y.data, offset)}")
            if first is None:
                first = line
                print("desync: first difference:")
                print(line)
                if not args.all:
                    break
            else:
                print(line)
        if first is not None and not args.all:
            break
    if first is None:
        print("desync: the dumps are equal byte for byte")
        return 1
    if args.all:
        print(f"desync: {differing} chunks differ")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)
    show = sub.add_parser("show", help="print a report's header and what its dump holds")
    show.add_argument("report")
    show.set_defaults(func=cmd_show)
    diff = sub.add_parser("diff", help="the first value that differs between two reports of one frame")
    diff.add_argument("a")
    diff.add_argument("b")
    diff.add_argument("--all", action="store_true", help="every differing chunk, not only the first")
    diff.set_defaults(func=cmd_diff)
    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
