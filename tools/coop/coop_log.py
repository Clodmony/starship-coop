#!/usr/bin/env python3
"""Reads co-op logs (#172): the files each game writes during a session, its own and the ones it got from the other
players, and merges them into one timeline.

    python tools/coop/coop_log.py sessions <folder>                 # the sessions a game folder holds, and whose logs
    python tools/coop/coop_log.py merge <folder> [<folder> ...]     # one timeline of every player, by time and frame
    python tools/coop/coop_log.py merge <folder> --category session,desync --player 1,2
    python tools/coop/coop_log.py merge <folder> --session 20261108-140322 --by frame
    python tools/coop/coop_log.py merge <a> <b> --offset 2=-1.5     # player 2's clock was 1.5 s ahead: take it off

A game folder holds `coop-log-<session start>-player<n>.txt` for every player of the session (src/port/coop/log/coop_log.h):
the game's own, and the ones streamed to it. A line is `YYYY-MM-DD HH:MM:SS.mmm <frame> <level> <category> <text>`, the
frame and the level `-` outside lockstep and outside a level; `#` starts a header. Several folders, for example the ones
both players send back, merge into one timeline too: a line that two folders hold is shown once.

The clocks of two PCs differ, so the merge goes by time first and frame second (`--by time`), or by frame first for the
part of a session that ran in lockstep (`--by frame`). `--offset` moves one player's clock. Exit 0 when something was
merged, 1 when nothing matched, 2 when a folder cannot be read.
"""
import argparse
import re
import sys
from datetime import datetime
from pathlib import Path

FILE = re.compile(r"^coop-log-(\d{8}-\d{6})-(player(\d)|joiner)\.txt$")
LINE = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) (\S+) (\S+) (\S+) ?(.*)$")
CATEGORIES = ("session", "net", "lockstep", "desync", "game", "player", "state", "warn", "error", "log")


class Line:
    __slots__ = ("time", "frame", "level", "category", "text", "player", "source")

    def __init__(self, time, frame, level, category, text, player, source):
        self.time, self.frame, self.level, self.category = time, frame, level, category
        self.text, self.player, self.source = text, player, source

    def key(self, by, offsets):
        seconds = self.time.timestamp() + offsets.get(self.player, 0.0)
        frame = self.frame if self.frame is not None else -1
        if by == "frame" and frame >= 0:
            return (0, frame, seconds, self.player or 0)
        return (1 if by == "frame" else 0, seconds, frame, self.player or 0)

    def format(self, offsets=None):
        seconds = self.time.timestamp() + (offsets or {}).get(self.player, 0.0)
        when = datetime.fromtimestamp(seconds).strftime("%H:%M:%S.%f")[:-3]
        who = f"p{self.player}" if self.player is not None else "p?"
        frame = "-" if self.frame is None else str(self.frame)
        return f"{when} {who} {frame} {self.level} {self.category} {self.text}"


def parse_line(text, player=None, source=None):
    """A Line, or None for a header or anything that is not a log line."""
    match = LINE.match(text.rstrip("\n"))
    if match is None:
        return None
    when, frame, level, category, rest = match.groups()
    return Line(datetime.strptime(when, "%Y-%m-%d %H:%M:%S.%f"), int(frame) if frame.isdigit() else None, level,
                category, rest, player, source)


def log_files(folder):
    """{session: {player or None: path}} for the co-op logs in a game folder."""
    found = {}
    for path in sorted(Path(folder).iterdir()):
        match = FILE.match(path.name)
        if match:
            player = int(match.group(3)) if match.group(3) else None
            found.setdefault(match.group(1), {})[player] = path
    return found


def read_lines(path, player=None):
    lines = []
    for text in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        line = parse_line(text, player, path)
        if line is not None:
            lines.append(line)
    return lines


def merge(folders, session=None, categories=None, players=None, by="time", offsets=None):
    """The lines of every player of one session across the folders, sorted, each line once. Returns (session, lines);
    session is None when the folders hold none."""
    seen = set()
    merged = []
    chosen = session
    for folder in folders:
        sessions = log_files(folder)
        if not sessions:
            continue
        if chosen is None:
            chosen = sorted(sessions)[-1]
        for player, path in sessions.get(chosen, {}).items():
            if players is not None and player not in players:
                continue
            for line in read_lines(path, player):
                if categories is not None and line.category not in categories:
                    continue
                mark = (player, line.time, line.frame, line.category, line.text)
                if mark in seen:
                    continue
                seen.add(mark)
                merged.append(line)
    merged.sort(key=lambda line: line.key(by, offsets or {}))
    return chosen, merged


def parse_offsets(values):
    offsets = {}
    for value in values or []:
        player, _, seconds = value.partition("=")
        offsets[int(player)] = float(seconds)
    return offsets


def cmd_sessions(args):
    status = 1
    for folder in args.folders:
        if not Path(folder).is_dir():
            print(f"coop_log: not a folder: {folder}", file=sys.stderr)
            return 2
        for session, files in sorted(log_files(folder).items()):
            who = ", ".join(f"player {p}" if p is not None else "joiner" for p in sorted(files, key=lambda p: p or 0))
            counts = {p: len(read_lines(path)) for p, path in files.items()}
            print(f"{folder}: session {session}: {who}; lines " +
                  ", ".join(f"{c}" for _, c in sorted(counts.items(), key=lambda kv: kv[0] or 0)))
            status = 0
    return status


def cmd_merge(args):
    for folder in args.folders:
        if not Path(folder).is_dir():
            print(f"coop_log: not a folder: {folder}", file=sys.stderr)
            return 2
    categories = set(args.category.split(",")) if args.category else None
    players = {int(p) for p in args.player.split(",")} if args.player else None
    offsets = parse_offsets(args.offset)
    session, lines = merge(args.folders, args.session, categories, players, args.by, offsets)
    if session is None:
        print("coop_log: no co-op log in these folders", file=sys.stderr)
        return 1
    print(f"# session {session}, {len(lines)} lines, by {args.by}")
    for line in lines:
        print(line.format(offsets))
    return 0 if lines else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)
    s = sub.add_parser("sessions", help="the sessions in game folders")
    s.add_argument("folders", nargs="+")
    s.set_defaults(run=cmd_sessions)
    m = sub.add_parser("merge", help="one timeline of every player")
    m.add_argument("folders", nargs="+")
    m.add_argument("--session", help="the session start stamp (default: the newest)")
    m.add_argument("--category", help=f"comma-separated, from {', '.join(CATEGORIES)}")
    m.add_argument("--player", help="comma-separated player numbers, 1 to 4")
    m.add_argument("--by", choices=("time", "frame"), default="time")
    m.add_argument("--offset", action="append", metavar="PLAYER=SECONDS", help="add seconds to that player's clock")
    m.set_defaults(run=cmd_merge)
    args = ap.parse_args()
    return args.run(args)


if __name__ == "__main__":
    sys.exit(main())
