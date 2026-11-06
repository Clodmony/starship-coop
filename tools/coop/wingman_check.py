#!/usr/bin/env python3
"""Run how a downed wingman returns, and check it (#89, the entry from #61 in docs/coop/NOTES.md).

    python tools/coop/wingman_check.py all                       # every scenario below, one after the other
    python tools/coop/wingman_check.py run alive                 # one scenario
    python tools/coop/wingman_check.py check alive <run folder>  # the rules of a scenario, on a run that exists

Each scenario plays input by rule in a fresh local/test-replay-* folder (replay.py), with a team trace
(src/port/coop/test/team_trace.h) and, where it says so, under gdb. Trace, gdb log and input stay in
local/test-wingman-<scenario>/. Nothing else is needed: no recording, no person.

  alive    Sector X, Slippy alive. Script 94's test runs and EVOP_RESTORE_TEAM does not.
  down     Sector X, Slippy forced to -1 halfway. EVOP_RESTORE_TEAM brings him back to 255 and he flies in.
  sitout   Slippy at -1 in Meteo, then Sector X: 0 at its start, and EVOP_RESTORE_TEAM brings him back there.
  chain    Falco at -1 in Meteo, then Sector X, then Titania: 0, then 255, in every store the entry names.

How a run gets there, none of it new game code:

  - gLevelSelector and gSkipBriefing: START skips the prologue, the D-pad picks a level on the map, A starts it. Both go
    through Map_PlayLevel and Play_Setup as any level start does. It skips the course menu, whose "go on" writes the
    same store (fox_map.c:3525-3528, :6763-6765).
  - gDebugWarpZone: L in Meteo or Sector X enters the warp, and the warp ends the level by its own end object. No other
    ending was reached by rule. gDebugLevelComplete left the player in PLAYERSTATE_LEVEL_COMPLETE in Corneria, on rails
    for 4000 frames and in all-range for 9500: it does not set what Boss_CompleteLevel does. A boss would have to be
    beaten.
  - gInvincible, as a run flies straight on and would be shot down.

gdb stops on the lines of the game that matter, found by searching the source, so a line that moves is followed. Each
run also stops on Map_PlayLevel: that stop must show in the log, or the silence about the other lines proves nothing.
The frames below hold for this game on one build; when the game changes they can drift, and the run fails on what it did
not reach instead of passing. Linux only, as it needs gdb. About 30 minutes, more on a loaded machine.
"""
import argparse
import json
import re
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hash_check  # noqa: E402
import replay as replay_tool  # noqa: E402

ROOT = replay_tool.ROOT

# OSContPad.button
BUTTON_A = hash_check.BUTTON_A
BUTTON_L = hash_check.BUTTON_L
BUTTON_START = 0x1000
BUTTON_RIGHT = 0x0100

# include/sf64thread.h, include/sf64level.h, include/sf64player.h
GS_MAP, GS_PLAY = 4, 7
PLAY_UPDATE = 2  # gPlayState: from here on the level runs, and gPlayer is this level's
LEVEL_METEO, LEVEL_SECTOR_X = 1, 2
FALCO, SLIPPY = 0, 1  # places in the trace's lists, which start at TEAM_ID_FALCO
TEAM_ID = {FALCO: 1, SLIPPY: 2}

# Path progress where Sector X loads script 94: object 997 of its list, 200 before its z (fox_enmy.c:630).
SCRIPT_94_PATH = 204773.5
SCRIPT_94_MARGIN = 300.0
PAST_SCRIPT_94 = SCRIPT_94_PATH + 2000.0

CVARS = {"gLevelSelector": 1, "gSkipBriefing": 1, "gDebugWarpZone": 1, "gInvincible": 1}

# Menus to the map, then the prologue skipped. On the map R moves one mission on: Meteo, Fortuna, Sector X.
TO_MAP = {frame: BUTTON_A for frame in range(60, 190, 30)}
TO_MAP[240] = BUTTON_START
START = 420
# A level by the first press: Meteo at 300, Sector X three presses on.
TO_METEO = {**TO_MAP, 300: BUTTON_RIGHT, START: BUTTON_A}
TO_SECTOR_X = {**TO_MAP, 300: BUTTON_RIGHT, 330: BUTTON_RIGHT, 360: BUTTON_RIGHT, START: BUTTON_A}
# Meteo and its warp take until about 4806, the map's own move until about 5142. Then two presses to Sector X.
THEN_SECTOR_X = {**TO_METEO, 1500: BUTTON_L, 5300: BUTTON_RIGHT, 5330: BUTTON_RIGHT, 5400: BUTTON_A}
# Sector X's warp ends on the map at about 9579, the map is idle from about 9915. A starts Sector X once more.
THEN_THIRD_LEVEL = {**THEN_SECTOR_X, 6000: BUTTON_L, 10000: BUTTON_RIGHT, 10100: BUTTON_A}  # one more right: Titania

GDB_SCRIPT = """set pagination off
set confirm off
handle SIGPIPE pass nostop noprint
handle SIGTERM pass nostop noprint
break Map_PlayLevel
commands
silent
printf "START level=%d\\n", gCurrentLevel
continue
end
break fox_enmy2.c:{test_line} if this->aiType == 94
commands
silent
printf "TEST slippy=%d path=%f\\n", gTeamShields[2], gPathProgress
continue
end
break fox_enmy2.c:{restore_line}
commands
silent
printf "RESTORE id=%d before=%d path=%f script=%d\\n", actorScript[this->aiIndex + 1], \
gTeamShields[actorScript[this->aiIndex + 1]], gPathProgress, this->aiType
continue
end
run
"""


def line_after(path, text):
    """The number of the line after the one holding text, in a game source file."""
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if text in line:
            return number + 1
    sys.exit(f"wingman: {text} not found in {path}")


def write_replay(path, frames, presses):
    """Write input by rule: the layout is in hash_check.write_generated_replay, this one presses what it is told."""
    data = bytearray(b"SFCR" + struct.pack("<III6i", 2, 20, 204, *hash_check.GENERATED_SEEDS))
    idle = bytes(20)
    for frame in range(frames):
        pads = struct.pack("<H18x", presses.get(frame, 0)) + idle * 3
        data += struct.pack("<I", frame) + pads + pads + bytes(40)
    path.write_bytes(data)


def gdb_wrapper(folder, exe):
    """Write a script that starts exe under gdb, to stand in for exe in replay.run_game. Returns it."""
    source = ROOT / "src" / "engine" / "fox_enmy2.c"
    commands = folder / "wingman.gdb"
    commands.write_text(GDB_SCRIPT.format(test_line=line_after(source, "case EVC_SLIPPY_ACTIVE:"),
                                          restore_line=line_after(source, "case EV_OPC(EVOP_RESTORE_TEAM):")))
    wrapper = folder / "wingman-gdb.sh"
    wrapper.write_text(f'#!/bin/sh\nexec gdb -q -batch -x "{commands}" --args "{exe}" > "{folder / "gdb.log"}" 2>&1\n')
    wrapper.chmod(0o755)
    return wrapper


def read_trace(path):
    """Returns the lines of a team trace, each a dict: numbers as they are, the lists as lists."""
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("#"):
            continue
        row = {}
        for field in line.split():
            key, _, value = field.partition("=")
            row[key] = [int(v) for v in value.split(",")] if "," in value else float(value)
        rows.append(row)
    return rows


def read_gdb(path):
    """Returns what the stops printed, as (kind, values)."""
    stops = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"(START|TEST|RESTORE) (.*)", line)
        if match:
            stops.append((match[1], {k: float(v) for k, v in (f.split("=") for f in match[2].split())}))
    return stops


class Checks:
    """Collects results, so a run reports everything that failed and not only the first."""

    def __init__(self, name):
        self.name = name
        self.failed = 0

    def that(self, ok, text):
        print(f"wingman: {self.name}: {'ok  ' if ok else 'FAIL'} {text}")
        self.failed += 0 if ok else 1


def level_rows(rows, level):
    return [r for r in rows if r["gs"] == GS_PLAY and r["level"] == level]


def check_stops(checks, stops, level):
    """The one stop that must always show: without it, no other stop in the log means anything."""
    checks.that(any(kind == "START" and v["level"] == level for kind, v in stops),
                f"gdb saw the start of level {level}, so its log can be trusted")


def check_alive(checks, rows, stops):
    sector_x = level_rows(rows, LEVEL_SECTOR_X)
    check_stops(checks, stops, LEVEL_SECTOR_X)
    checks.that(sector_x and max(r["path"] for r in sector_x) > PAST_SCRIPT_94, "the run got past script 94")
    tests = [v for kind, v in stops if kind == "TEST"]
    checks.that(tests and all(v["slippy"] > 0 for v in tests), f"script 94 tested Slippy above 0: {tests[:1]}")
    checks.that(not [v for kind, v in stops if kind == "RESTORE"], "EVOP_RESTORE_TEAM did not run")
    checks.that(bool(sector_x) and all(r["ships"][SLIPPY] == 0 for r in sector_x), "Slippy had no ship in the level")


def check_restored(checks, rows, stops, before):
    """Slippy goes from `before` to 255 by script 94, and only that way, then flies in."""
    sector_x = level_rows(rows, LEVEL_SECTOR_X)
    check_stops(checks, stops, LEVEL_SECTOR_X)
    restores = [v for kind, v in stops if kind == "RESTORE"]
    checks.that(len(restores) == 1, f"EVOP_RESTORE_TEAM ran once, it ran {len(restores)} times")
    if restores:
        restore = restores[0]
        checks.that(restore["script"] == 94 and restore["id"] == TEAM_ID[SLIPPY], f"by script 94, on Slippy: {restore}")
        checks.that(restore["before"] == before, f"on a Slippy at {before}, he was at {restore['before']:.0f}")
        checks.that(abs(restore["path"] - SCRIPT_94_PATH) < SCRIPT_94_MARGIN, f"at path {restore['path']:.0f}")
    later = [r for r in sector_x if r["path"] > SCRIPT_94_PATH + SCRIPT_94_MARGIN]
    checks.that(bool(later) and later[0]["team"][SLIPPY] == 255, "he is at 255 after it")
    checks.that(any(r["ships"][SLIPPY] == 1 for r in later), "he has a ship after it")
    earlier = [r for r in sector_x if r["path"] < SCRIPT_94_PATH - SCRIPT_94_MARGIN]
    checks.that(all(r["ships"][SLIPPY] == 0 for r in earlier), "he had no ship before it")


def check_down(checks, rows, stops):
    checks.that(any(r["team"][SLIPPY] == -1 for r in level_rows(rows, LEVEL_SECTOR_X)), "the poke put Slippy at -1")
    check_restored(checks, rows, stops, -1)


def check_sitout(checks, rows, stops):
    first = next((r for r in level_starts(rows) if r["level"] == LEVEL_SECTOR_X), None)
    checks.that(first is not None and first["team"][SLIPPY] == 0,
                f"Slippy started Sector X at 0: {first and first['team']}")
    check_restored(checks, rows, stops, 0)


def level_starts(rows):
    """The first line of each level that runs. Until then gPlayer still points at the last level's player."""
    starts = []
    waiting = True
    for row in rows:
        if row["gs"] == GS_MAP:
            waiting = True
        elif waiting and row["gs"] == GS_PLAY and row["ps"] == PLAY_UPDATE:
            starts.append(row)
            waiting = False
    return starts


def leaving_map(rows, start):
    """The line before a level's players exist: Map_PlayLevel has run Play_Setup, the level has not loaded yet."""
    return next((r for r in reversed(rows) if r["frame"] < start["frame"] and r["gs"] == GS_PLAY), None)


def check_chain(checks, rows, stops):
    """The table of the entry from #61, for Falco: one line of it per check."""
    def stores(row):
        return row and [row[key][FALCO] for key in ("team", "saved", "prev")]

    def check_row(moment, row, want):
        checks.that(stores(row) == want, f"{moment}: {stores(row)}, want {want}")

    starts = level_starts(rows)
    checks.that(len(starts) >= 3, f"the run started three levels, it started {len(starts)}")
    if len(starts) < 3:
        return
    level_n, level_n1, level_n2 = starts[:3]
    courses = [row["level"] for row in starts[:3]]
    checks.that(len(set(courses)) == 3, f"three different levels: {courses}")
    maps = [r for r in rows if r["gs"] == GS_MAP]

    check_row("level N", level_n, [255, 255, 255])
    check_row("shot down", next((r for r in rows if r["team"][FALCO] == -1), None), [-1, 255, 255])
    check_row("map after N", next((r for r in maps if r["saved"][FALCO] == -1), None), [-1, -1, -1])
    check_row("leaving the map", leaving_map(rows, level_n1), [-1, 0, -1])
    check_row("level N+1", level_n1, [0, 0, -1])
    after_n1 = [r for r in maps if r["frame"] > level_n1["frame"]]
    check_row("map after N+1", next((r for r in after_n1 if r["prev"][FALCO] == 255), None), [0, 0, 255])
    check_row("leaving the map", leaving_map(rows, level_n2), [0, 255, 255])
    check_row("level N+2", level_n2, [255, 255, 255])
    checks.that(level_n1["map"][FALCO] == -1, f"the map stored -1 for a retry of N+1: {level_n1['map'][FALCO]}")
    checks.that(level_n2["map"][FALCO] == 0, f"the map stored 0 for a retry of N+2: {level_n2['map'][FALCO]}")


SCENARIOS = {
    "alive": dict(frames=6400, presses=TO_SECTOR_X, poke=None, gdb=True, check=check_alive),
    "down": dict(frames=6400, presses=TO_SECTOR_X, poke=f"{LEVEL_SECTOR_X},3000,2,-1", gdb=True, check=check_down),
    "sitout": dict(frames=11800, presses=THEN_SECTOR_X, poke=f"{LEVEL_METEO},100,2,-1", gdb=True, check=check_sitout),
    "chain": dict(frames=10700, presses=THEN_THIRD_LEVEL, poke=f"{LEVEL_METEO},100,1,-1", gdb=False, check=check_chain),
}


def check_run(name, folder):
    """Check a finished run. Returns the number of checks that failed."""
    checks = Checks(name)
    trace = folder / "team-trace.txt"
    if not trace.exists():
        checks.that(False, f"no team trace in {folder}: is the trace code in this build?")
        return 1
    log = folder / "gdb.log"
    SCENARIOS[name]["check"](checks, read_trace(trace), read_gdb(log) if log.exists() else [])
    return checks.failed


def run(name, exe, timeout):
    scenario = SCENARIOS[name]
    folder = ROOT / "local" / f"test-wingman-{name}"
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    replay = folder / "input.sfcr"
    write_replay(replay, scenario["frames"], scenario["presses"])
    env = {"SF64_COOP_TEAM_TRACE": str(folder / "team-trace.txt")}
    if scenario["poke"]:
        env["SF64_COOP_TEAM_POKE"] = scenario["poke"]
    start = gdb_wrapper(folder, exe) if scenario["gdb"] else exe
    values, _, elapsed = replay_tool.run_game("play", replay, start, timeout, tag=f"wingman-{name}", env_extra=env,
                                              config={"CVars": CVARS})
    result = values.get("result") if values else "NO_REPORT"
    # A replay by rule holds no checkpoints, so FAIL_MISMATCH at frame 0 is what playing it through looks like.
    print(f"wingman: {name}: replay {result} after {elapsed:.0f}s")
    if result != "FAIL_MISMATCH":
        print(f"wingman: {name}: FAIL the replay did not play through")
        return 1
    return check_run(name, folder)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    for command, help_text in (("all", "every scenario"), ("run", "one scenario")):
        p = sub.add_parser(command, help=help_text)
        if command == "run":
            p.add_argument("scenario", choices=SCENARIOS)
        p.add_argument("--exe", type=Path, help="default: the newest build")
        p.add_argument("--timeout", type=int, default=2400, help="seconds for one run (default 2400)")
    p = sub.add_parser("check", help="check a run that exists")
    p.add_argument("scenario", choices=SCENARIOS)
    p.add_argument("folder", type=Path)
    args = parser.parse_args()

    if args.command == "check":
        failed = check_run(args.scenario, args.folder.resolve())
    else:
        if shutil.which("gdb") is None:
            sys.exit("wingman: gdb not found on PATH")
        exe = replay_tool.find_exe(args.exe)
        names = list(SCENARIOS) if args.command == "all" else [args.scenario]
        failed = sum(run(name, exe, args.timeout) for name in names)
    print(f"wingman: {'PASS' if failed == 0 else f'FAIL, {failed} checks'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
