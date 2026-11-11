#!/usr/bin/env python3
"""The game under AddressSanitizer and UBSan, level by level (#262, #145): what reads or writes outside an array.

    python tools/coop/w7asan_sweep.py build                  # the sanitizer build, in build-asan (Linux, GCC or Clang)
    python tools/coop/w7asan_sweep.py baseline               # one game through the menus into Corneria
    python tools/coop/w7asan_sweep.py sweep                  # every campaign level, the short ones first; unattended
    python tools/coop/w7asan_sweep.py sweep --levels aquas,area-6 --frames 9000
    python tools/coop/w7asan_sweep.py table <folder>         # the tables of a sweep's folder again

Why: two games of one compiler read the same stray bytes, so no hash comparison on one PC sees a read outside an
array; a sanitizer sees it in one game (docs/coop/NOTES.md, "2026-11-11, the Aquas boss desync"). AddressSanitizer
reports an access outside a global, a heap block or a stack object; UBSan's bounds check reports an index outside an
array that is a member of a larger object, which AddressSanitizer cannot see, and the rest of C's undefined behaviour.

build calls build.py with SANITIZE_FLAGS for C and C++, in its own folder: the whole game, libultraship included, in
Debug, as the other builds here are. It needs sf64.o2r and starship.o2r beside it, as every build does. One build takes
about as long as a clean build of the game, and its folder is about twice as large.

baseline plays the input by rule that `hash_check.py base` plays, with co-op off, through the menus and the prologue
into Corneria. sweep flies one level at a time with hash_check.py's pilot_flight (one game, 2 players through
gCoopDebugPlayerCount, a pilot that reads game state flying both; gInvincible but in Corneria, gInfiniteBombs in
Zoness and at Andross, as campaign.py), to level complete and what follows it, or a frame cap. One game at a time. The
levels are campaign.py's route `all`; they run shortest first and the tables list them in the route's order.

A game does not stop at a report (halt_on_error=0), but AddressSanitizer ends it once 25 different places have
reported; that shows as "the game ended by itself". Reports are read from the sanitizers' own log files and
deduplicated by kind and the first stack frame that lies in this repo (function, file:line); UBSan's by file:line and
the message without its numbers. Each line of the report table says in which levels it was seen. Everything is in
summary.json in the sweep's folder (local/test-w7asan-*), written after every level, so a stopped sweep keeps what it
has. A file named `stop` in that folder, or the one --stop-file names, ends the sweep before the next level.

Exit code 0 when no report of AddressSanitizer's was found and every level reached level complete; 1 otherwise. UBSan
reports alone do not fail it: `--strict` makes them.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import campaign  # noqa: E402
import hash_check  # noqa: E402
import replay as replay_tool  # noqa: E402
from run_folder import make_run_folder  # noqa: E402

ROOT = replay_tool.ROOT
W7ASAN_BUILD_DIR = "build-asan"
# recover: a game goes on after a report, so one run lists every place. Frame pointers: whole stacks in the reports.
# float-cast-overflow is not part of `undefined`: a float too large for the integer it is cast to gives what the
# processor's instruction gives, which is not the same on every processor.
W7ASAN_FLAGS = ("-fsanitize=address,undefined,float-cast-overflow "
                "-fsanitize-recover=address,undefined,float-cast-overflow -fno-omit-frame-pointer")
W7ASAN_ASAN_OPTIONS = "halt_on_error=0:detect_leaks=0:abort_on_error=0"
W7ASAN_UBSAN_OPTIONS = "halt_on_error=0:print_stacktrace=1"
# Lockstep frames to level complete in the campaign run of 2026-11-11 (docs/coop/NOTES.md), for the order only.
W7ASAN_LENGTH = {"fortuna": 7, "sector-z": 7, "aquas": 9, "venom-2": 9, "sector-y": 9, "solar": 10, "corneria": 11,
                 "macbeth": 11, "zoness": 12, "sector-x": 12, "venom-1": 12, "venom-andross": 14, "katina": 17,
                 "bolse": 17, "meteo": 21, "area-6": 22, "titania": 45}
W7ASAN_ASAN_HEAD = re.compile(r"==\d+==ERROR: (AddressSanitizer|LeakSanitizer): (\S+)")
W7ASAN_ASAN_ACCESS = re.compile(r"^(READ|WRITE) of size (\d+)")
W7ASAN_FRAME = re.compile(r"^\s+#(\d+) 0x[0-9a-f]+ (?:in (\S+) )?\(?([^()\s]*?)(?::(\d+))?(?::\d+)?\)?(?: \(.*\))?$")
W7ASAN_UBSAN = re.compile(r"^(\S+?):(\d+):(\d+): runtime error: (.*)$")
W7ASAN_WHERE = re.compile(r"is located (.*)$|^(0x[0-9a-f]+ is located .*)$")


def w7asan_exe(given):
    exe = Path(given).resolve() if given else ROOT / W7ASAN_BUILD_DIR / "Starship"
    if not exe.exists():
        sys.exit(f"w7asan: no {exe}; run python tools/coop/w7asan_sweep.py build first")
    return exe


def w7asan_inside(path):
    """A path of a stack frame as one inside this repo (`src/...`), or None for a library's."""
    if not path:
        return None
    try:
        inside = Path(path).resolve().relative_to(ROOT)
    except ValueError:
        return None
    return None if inside.parts[0].startswith("build") else inside.as_posix()


def w7asan_parse(text):
    """The reports in one sanitizer log: a list of dicts with tool, kind, function, place (file:line), detail."""
    reports = []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        head = W7ASAN_ASAN_HEAD.search(line)
        ub = W7ASAN_UBSAN.match(line) if not head else None
        if not head and not ub:
            i += 1
            continue
        i += 1
        frames, access, where = [], "", ""
        seen_stack = False
        while i < len(lines):
            nxt = lines[i]
            if W7ASAN_ASAN_HEAD.search(nxt) or W7ASAN_UBSAN.match(nxt):
                break
            frame = W7ASAN_FRAME.match(nxt)
            if frame:
                if not seen_stack or frames is not None:
                    frames.append((frame.group(2) or "?", frame.group(3), frame.group(4)))
                seen_stack = True
            elif seen_stack and frames and not nxt.strip() and head:
                # The first stack of an ASan report is the access; the next ones are where the object was made.
                rest = "\n".join(lines[i:i + 12])
                found = re.search(r"(0x[0-9a-f]+ is located [^\n]*)", rest)
                where = found.group(1) if found else ""
                i += 1
                while i < len(lines) and not W7ASAN_ASAN_HEAD.search(lines[i]) and not W7ASAN_UBSAN.match(lines[i]):
                    i += 1
                break
            elif W7ASAN_ASAN_ACCESS.match(nxt):
                access = " ".join(W7ASAN_ASAN_ACCESS.match(nxt).groups())
            i += 1
        own = next(((fn, w7asan_inside(path), no) for fn, path, no in frames if w7asan_inside(path)), None)
        if head:
            fn, path, no = own or (frames[0] if frames else ("?", "?", None))
            where = re.sub(r"0x[0-9a-f]+", "ADDR", where)
            reports.append({"tool": "asan", "kind": head.group(2), "function": fn, "place": f"{path}:{no}",
                            "detail": f"{access}; {where}".strip("; ")})
        else:
            path = w7asan_inside(ub.group(1)) or ub.group(1)
            fn = own[0] if own else (frames[0][0] if frames else "?")
            reports.append({"tool": "ubsan", "kind": w7asan_ubsan_kind(ub.group(4)), "function": fn,
                            "place": f"{path}:{ub.group(2)}", "detail": ub.group(4)})
    return reports


def w7asan_ubsan_kind(message):
    """UBSan's message as a kind: the same words for every value that made it."""
    for words, kind in (("out of bounds", "bounds"), ("signed integer overflow", "signed-overflow"),
                        ("shift exponent", "shift"), ("left shift of", "shift"), ("misaligned address", "alignment"),
                        ("null pointer", "null"), ("is outside the range of representable", "float-cast"),
                        ("not a valid value for type", "invalid-value"), ("division by zero", "div-zero"),
                        ("cannot be represented in type", "overflow"), ("applying", "pointer-overflow"),
                        ("insufficient space", "object-size"), ("through pointer to incorrect", "function-type"),
                        ("negation of", "signed-overflow")):
        if words in message:
            return kind
    return re.sub(r"[-0-9]+", "N", message)[:40]


def w7asan_key(report):
    return f"{report['tool']}|{report['kind']}|{report['function']}|{report['place']}"


def w7asan_collect(logs):
    """{key: report with count} of the sanitizer logs with this path prefix."""
    found = {}
    for path in sorted(logs.parent.glob(logs.name + "*")):
        for report in w7asan_parse(path.read_text(encoding="utf-8", errors="replace")):
            entry = found.setdefault(w7asan_key(report), {**report, "count": 0})
            entry["count"] += 1
    return found


def w7asan_set_env(logs):
    """The sanitizers' options for the games this process starts: game_env copies this environment."""
    os.environ["ASAN_OPTIONS"] = f"{W7ASAN_ASAN_OPTIONS}:log_path={logs}"
    os.environ["UBSAN_OPTIONS"] = f"{W7ASAN_UBSAN_OPTIONS}:log_path={logs}"


def w7asan_table(summary):
    """The two tables of a summary, as Markdown lines."""
    out = ["| Level | Frames flown | Level complete at | Complete | Why it stopped | ASan | UBSan bounds | UBSan other |",
           "|---|---|---|---|---|---|---|---|"]
    order = [name for name in ("baseline", *campaign.ROUTES["all"]) if name in summary["levels"]]
    order += [name for name in summary["levels"] if name not in order]
    for name in order:
        row = summary["levels"][name]
        kinds = [key.split("|")[:2] for key in row["reports"]]
        asan = sum(1 for tool, _ in kinds if tool == "asan")
        bounds = sum(1 for tool, kind in kinds if tool == "ubsan" and kind == "bounds")
        out.append(f"| {name} | {row['frames']} | {row['complete'] if row['complete'] is not None else '-'} | "
                   f"{'yes' if row['complete'] is not None else 'no'} | {row['why']} | {asan} | {bounds} | "
                   f"{len(kinds) - asan - bounds} |")
    merged = {}
    for name in order:
        for key, report in summary["levels"][name]["reports"].items():
            entry = merged.setdefault(key, {**report, "count": 0, "levels": []})
            entry["count"] += report["count"]
            entry["levels"].append(name)
    out += ["", "| Tool | Kind | Function | Place | Levels | Detail |", "|---|---|---|---|---|---|"]
    rank = {"asan": 0, "ubsan": 1}
    for key, r in sorted(merged.items(), key=lambda kv: (rank[kv[1]["tool"]], kv[1]["kind"] != "bounds",
                                                         kv[1]["kind"], kv[1]["place"])):
        levels = ", ".join(r["levels"]) if len(r["levels"]) <= 4 else f"{len(r['levels'])} levels"
        out.append(f"| {r['tool']} | {r['kind']} | `{r['function']}` | `{r['place']}` | {levels} | {r['detail']} |")
    return out, merged


def w7asan_save(folder, summary):
    temp = folder / "summary.json.new"
    temp.write_text(json.dumps(summary, indent=1), encoding="utf-8")
    temp.replace(folder / "summary.json")


def w7asan_finish(folder, summary, strict):
    lines, merged = w7asan_table(summary)
    (folder / "table.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    asan = sum(1 for r in merged.values() if r["tool"] == "asan")
    ubsan = len(merged) - asan
    open_levels = [name for name, row in summary["levels"].items() if row["complete"] is None and name != "baseline"]
    bad = asan or open_levels or (strict and ubsan)
    print(f"w7asan: {len(summary['levels'])} runs, {asan} distinct AddressSanitizer reports, {ubsan} distinct UBSan "
          f"reports; without level complete: {', '.join(open_levels) or 'none'}; folder {folder}")
    print("w7asan: FAIL" if bad else "w7asan: PASS")
    return 1 if bad else 0


def w7asan_cmd_build(args):
    flags = W7ASAN_FLAGS
    cmd = [sys.executable, str(ROOT / "tools/coop/build.py"), "--build-dir", W7ASAN_BUILD_DIR, "--no-check",
           "-D", f"CMAKE_C_FLAGS={flags}", "-D", f"CMAKE_CXX_FLAGS={flags}"]
    if args.jobs:
        cmd += ["--jobs", str(args.jobs)]
    if args.quiet:
        cmd.append("--quiet")
    return subprocess.run(cmd, cwd=ROOT).returncode


def w7asan_cmd_baseline(args):
    exe = w7asan_exe(args.exe)
    folder = make_run_folder(ROOT / "local", "test-w7asan-baseline-")
    logs = folder / "sanitizer"
    w7asan_set_env(logs)
    start = replay_tool.MENUS
    replay = folder / "input.sfcr"
    hash_check.write_generated_replay(replay, start=start)
    values, names, rows = hash_check.play(replay, exe, args.timeout, "w7asan-baseline",
                                          config={"CVars": {"gDebugJumpToAllRange": 1}}, start=start)
    frames = len(rows) if rows else 0
    result = values.get("result") if values else "no report"
    summary = {"exe": str(exe), "levels": {"baseline": {
        "frames": frames, "complete": None, "why": f"replay {result}", "reports": w7asan_collect(logs)}}}
    w7asan_save(folder, summary)
    lines, merged = w7asan_table(summary)
    print("\n".join(lines))
    ok = hash_check.played_through(values) and not merged
    print(f"w7asan: baseline, {frames} frames through the menus into Corneria, replay {result}, "
          f"{len(merged)} distinct reports; folder {folder}")
    print("w7asan: PASS" if ok else "w7asan: FAIL")
    return 0 if ok else 1


def w7asan_fly(name, exe, args, folder):
    """One level under the sanitizers. Returns its row of the summary."""
    work = folder / name
    work.mkdir()
    logs = work / "sanitizer"
    w7asan_set_env(logs)
    level = hash_check.PILOT_LEVELS[name]
    start = replay_tool.Start(f"flying-{name}", level, True, replay_tool.FLYING.level, replay_tool.FLYING.active,
                              replay_tool.START_SEEDS)
    cvars = {"gCoopDebugPlayerCount": args.players}
    if name not in campaign.MORTAL:
        cvars["gInvincible"] = 1
    if name in campaign.BOMBS:
        cvars["gInfiniteBombs"] = 1
    began = time.monotonic()
    flight = SimpleNamespace(frames=args.frames, timeout=args.timeout)
    _, frames, complete, why, _ = hash_check.pilot_flight(name, exe, flight, work, {"CVars": cvars}, start)
    reports = w7asan_collect(logs)
    row = {"frames": frames, "complete": complete, "why": why, "seconds": round(time.monotonic() - began),
           "cvars": cvars, "reports": reports}
    print(f"w7asan: {name}: {frames} frames, level complete at {complete}, {why}; {len(reports)} distinct reports, "
          f"{row['seconds']} s", flush=True)
    return row


def w7asan_cmd_sweep(args):
    exe = w7asan_exe(args.exe)
    names = args.levels.split(",") if args.levels else sorted(campaign.ROUTES["all"],
                                                              key=lambda n: W7ASAN_LENGTH.get(n, 99))
    unknown = [name for name in names if name not in hash_check.PILOT_LEVELS]
    if unknown:
        sys.exit(f"w7asan: no level {', '.join(unknown)}; the choices are {', '.join(hash_check.PILOT_LEVELS)}")
    folder = Path(args.out).resolve() if args.out else make_run_folder(ROOT / "local", "test-w7asan-")
    folder.mkdir(parents=True, exist_ok=True)
    stops = [folder / "stop"] + ([Path(args.stop_file)] if args.stop_file else [])
    summary = {"exe": str(exe), "levels": {}}
    if (folder / "summary.json").exists():  # --out of an earlier sweep: go on where it stopped
        summary = json.loads((folder / "summary.json").read_text(encoding="utf-8"))
    print(f"w7asan: {len(names)} levels, one game at a time, folder {folder}", flush=True)
    for name in names:
        if any(stop.exists() for stop in stops):
            print(f"w7asan: stop file found, not starting {name}")
            break
        if name in summary["levels"]:
            print(f"w7asan: {name}: already in this folder's summary")
            continue
        summary["levels"][name] = w7asan_fly(name, exe, args, folder)
        w7asan_save(folder, summary)
    return w7asan_finish(folder, summary, args.strict)


def w7asan_cmd_table(args):
    folder = Path(args.folder).resolve()
    summary = json.loads((folder / "summary.json").read_text(encoding="utf-8"))
    return w7asan_finish(folder, summary, args.strict)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build", help=f"the sanitizer build, in {W7ASAN_BUILD_DIR}")
    b.add_argument("--jobs", type=int, help="parallel build jobs (default: all cores)")
    b.add_argument("--quiet", action="store_true", help="build.py --quiet: a summary line only")
    e = sub.add_parser("baseline", help="one game through the menus into Corneria, co-op off")
    e.add_argument("--exe", help=f"the sanitizer build's Starship (default: {W7ASAN_BUILD_DIR}/Starship)")
    e.add_argument("--timeout", type=float, default=3600, help="give up after this many seconds")
    s = sub.add_parser("sweep", help="every campaign level with a pilot, one game at a time")
    s.add_argument("--levels", help="these levels in this order (default: campaign.py's route all, shortest first)")
    s.add_argument("--players", type=int, default=2, help="players, through gCoopDebugPlayerCount (default 2)")
    s.add_argument("--frames", type=int, help="most frames a level is flown (default: hash_check.py's pilot caps)")
    s.add_argument("--timeout", type=float, default=4 * 3600, help="per level: give up after this many seconds")
    s.add_argument("--exe", help=f"the sanitizer build's Starship (default: {W7ASAN_BUILD_DIR}/Starship)")
    s.add_argument("--out", help="folder for the logs (default: a new one under local/; an old one is continued)")
    s.add_argument("--stop-file", help="end the sweep before the next level once this file exists")
    s.add_argument("--strict", action="store_true", help="fail on UBSan reports too")
    t = sub.add_parser("table", help="print the tables of a sweep's folder again")
    t.add_argument("folder")
    t.add_argument("--strict", action="store_true", help="fail on UBSan reports too")
    args = p.parse_args()
    return {"build": w7asan_cmd_build, "baseline": w7asan_cmd_baseline, "sweep": w7asan_cmd_sweep,
            "table": w7asan_cmd_table}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
