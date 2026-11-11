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
reported; that shows as "the game ended by itself". Reports are read from the game's stderr (a wrapper beside the game keeps it) and
deduplicated by kind and the first stack frame that lies in this repo (function, file:line); UBSan's by file:line and
the message without its numbers. Each line of the report table says in which levels it was seen. Everything is in
summary.json in the sweep's folder (local/test-w7asan-*), written after every level, so a stopped sweep keeps what it
has. A file named `stop` in that folder, or the one --stop-file names, ends the sweep before the next level.

--gdb <script> runs each game under gdb with a script of breakpoints that print (tools/coop/w7asan_slots.gdb: what
the Garoa's and the Gorgon's remembered effect slots hold each time they use them), far slower than without.

Exit code 0 when no report of AddressSanitizer's and no bounds report of UBSan's was found and every level reached
level complete; 1 otherwise. UBSan's other reports (shifts, signed overflow, casts) are listed and do not fail it:
`--strict` makes them.
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
W7ASAN_FRAME = re.compile(r"^\s+#\d+ 0x[0-9a-f]+ (?:in (\S+) )?\(?([^()\s:]*)(?::(\d+))?")
W7ASAN_UBSAN = re.compile(r"^(\S+?):(\d+):(\d+): runtime error: (.*)$")


def w7asan_exe(given):
    """The sanitizer build's game, behind a wrapper that sends its stderr to the file W7ASAN_STDERR names: the tools
    that start a game here throw its output away, and UBSan writes its reports nowhere else."""
    exe = Path(given).resolve() if given else ROOT / W7ASAN_BUILD_DIR / "Starship"
    if not exe.exists():
        sys.exit(f"w7asan: no {exe}; run python tools/coop/w7asan_sweep.py build first")
    wrapper = exe.with_name("w7asan-run.sh")  # beside the game, where the tools look for the game's archives
    wrapper.write_text(f'#!/bin/sh\n'
                       f'if [ -n "$W7ASAN_GDB" ]; then\n'
                       f'    exec gdb -q -batch -x "$W7ASAN_GDB" "{exe}" >>"${{W7ASAN_TRACE:-/dev/null}}" '
                       f'2>>"${{W7ASAN_STDERR:-/dev/null}}"\n'
                       f'fi\n'
                       f'exec "{exe}" 2>>"${{W7ASAN_STDERR:-/dev/null}}"\n', encoding="utf-8")
    wrapper.chmod(0o755)
    return wrapper


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
    """The reports in a game's stderr: a list of dicts with tool, kind, function, place (file:line), detail. The
    place is the first frame of the access's stack that lies in this repo: a report inside memcpy names its caller."""
    lines = text.splitlines()
    starts = [i for i, line in enumerate(lines) if W7ASAN_ASAN_HEAD.search(line) or W7ASAN_UBSAN.match(line)]
    reports = []
    for at, end in zip(starts, starts[1:] + [len(lines)]):
        head, ub = W7ASAN_ASAN_HEAD.search(lines[at]), W7ASAN_UBSAN.match(lines[at])
        frames, access, where = [], "", ""
        stack_over = False
        for line in lines[at + 1:end]:
            frame = W7ASAN_FRAME.match(line)
            if frame and not stack_over:  # the first stack is the access; later ones say where the object was made
                frames.append((frame.group(1) or "?", frame.group(2), frame.group(3)))
            elif frames:
                stack_over = True
            if W7ASAN_ASAN_ACCESS.match(line) and not access:
                access = " ".join(W7ASAN_ASAN_ACCESS.match(line).groups())
            if " is located " in line and not where:
                where = re.sub(r"0x[0-9a-f]+", "", line).replace("()", "").strip()
        own = next(((fn, w7asan_inside(path), no) for fn, path, no in frames if w7asan_inside(path)), None)
        fn, path, no = own or (frames[0] if frames else ("?", "?", None))
        if head:
            reports.append({"tool": "asan", "kind": head.group(2), "function": fn, "place": f"{path}:{no}",
                            "detail": "; ".join(part for part in (access, where) if part)})
        else:
            place = w7asan_inside(ub.group(1)) or ub.group(1)
            reports.append({"tool": "ubsan", "kind": w7asan_ubsan_kind(ub.group(4)), "function": fn,
                            "place": f"{place}:{ub.group(2)}", "detail": ub.group(4)})
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


def w7asan_collect(log):
    """{key: report with count} of a game's stderr file."""
    found = {}
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    for report in w7asan_parse(text):
        entry = found.setdefault(w7asan_key(report), {**report, "count": 0})
        entry["count"] += 1
    return found


def w7asan_set_env(log):
    """The sanitizers' options for the games this process starts, and where the wrapper puts their stderr: game_env
    copies this environment."""
    os.environ["ASAN_OPTIONS"] = W7ASAN_ASAN_OPTIONS
    os.environ["UBSAN_OPTIONS"] = W7ASAN_UBSAN_OPTIONS
    os.environ["W7ASAN_STDERR"] = str(log)
    os.environ["W7ASAN_TRACE"] = str(log.with_name("trace.txt"))


def w7asan_trace(path):
    """{name: [lines, lines with ok=0, the first of those]} of the `w7trace <name> ok=<0|1> ...` lines a gdb script
    printed (--gdb)."""
    counts = {}
    text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 3 or parts[0] != "w7trace":
            continue
        entry = counts.setdefault(parts[1], [0, 0, None])
        entry[0] += 1
        if parts[2] == "ok=0":
            entry[1] += 1
            entry[2] = entry[2] or line
    return counts


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


def w7asan_counts(merged):
    """(AddressSanitizer reports, UBSan bounds reports, other UBSan reports) of a merged report table."""
    asan = sum(1 for r in merged.values() if r["tool"] == "asan")
    bounds = sum(1 for r in merged.values() if r["tool"] == "ubsan" and r["kind"] == "bounds")
    return asan, bounds, len(merged) - asan - bounds


def w7asan_finish(folder, summary, strict):
    lines, merged = w7asan_table(summary)
    (folder / "table.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    asan, bounds, other = w7asan_counts(merged)
    open_levels = [name for name, row in summary["levels"].items() if row["complete"] is None and name != "baseline"]
    bad = asan or bounds or open_levels or (strict and other)
    print(f"w7asan: {len(summary['levels'])} runs, distinct reports: {asan} AddressSanitizer, {bounds} UBSan bounds, "
          f"{other} other UBSan; without level complete: {', '.join(open_levels) or 'none'}; folder {folder}")
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
    logs = folder / "sanitizer.txt"
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
    asan, bounds, other = w7asan_counts(merged)
    ok = hash_check.played_through(values) and not asan and not bounds
    print(f"w7asan: baseline, {frames} frames through the menus into Corneria, replay {result} (input by rule has no "
          f"checkpoints: FAIL_MISMATCH is its normal end), distinct reports: {asan} AddressSanitizer, {bounds} UBSan "
          f"bounds, {other} other UBSan; folder {folder}")
    print("w7asan: PASS" if ok else "w7asan: FAIL")
    return 0 if ok else 1


def w7asan_fly(name, exe, args, folder):
    """One level under the sanitizers. Returns its row of the summary."""
    work = folder / name
    work.mkdir()
    logs = work / "sanitizer.txt"
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
    if args.gdb:
        row["trace"] = w7asan_trace(logs.with_name("trace.txt"))
        for label, (total, strange, first) in sorted(row["trace"].items()):
            print(f"w7asan: {name}: trace {label}: {total} lines, {strange} with ok=0"
                  + (f", the first: {first}" if first else ""), flush=True)
    print(f"w7asan: {name}: {frames} frames, level complete at {complete}, {why}; {len(reports)} distinct reports, "
          f"{row['seconds']} s", flush=True)
    return row


def w7asan_cmd_sweep(args):
    exe = w7asan_exe(args.exe)
    os.environ.pop("W7ASAN_GDB", None)
    if args.gdb:
        os.environ["W7ASAN_GDB"] = str(Path(args.gdb).resolve())
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
    s.add_argument("--gdb", metavar="SCRIPT", help="run each game under gdb in batch mode with this script, which "
                   "ends in `run`; its `w7trace <name> ok=<0|1> ...` lines are counted (tools/coop/w7asan_slots.gdb)")
    t = sub.add_parser("table", help="print the tables of a sweep's folder again")
    t.add_argument("folder")
    t.add_argument("--strict", action="store_true", help="fail on UBSan reports too")
    args = p.parse_args()
    return {"build": w7asan_cmd_build, "baseline": w7asan_cmd_baseline, "sweep": w7asan_cmd_sweep,
            "table": w7asan_cmd_table}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
