#!/usr/bin/env python3
"""Frame interpolation: a skip region gives back the recording state it found (#153). Linux only, as it needs gdb.

    python tools/coop/interp_check.py                              # both scenarios, on the newest build
    python tools/coop/interp_check.py --exe <Starship> --only fps20
    python tools/coop/interp_check.py --exe <build before the fix> --expect-leaks   # the check turned round

`FrameInterpolation_ShouldInterpolateFrame(false)` ... `(true)` is how 23 draws skip interpolation. Upstream's (true)
turned recording on whatever it was, so in a frame that records nothing it turned it on mid-frame, and a CloseChild
then popped a path it had not opened: that aborted the game (#100) until #169 stopped the pop at the root. Each
scenario plays one game under gdb, which logs the frames where recording leaked on:

  fps20  Corneria from its level start, through the intro, at gInterpolationFPS 20, where FrameInterpolation_StartRecord
         records nothing: a frame is a leak when FrameInterpolation_StopRecord finds recording on, or when
         RecordCloseChild, recording, refuses to pop the root (#169), the pop that aborted a build without that guard.
         #136's run stopped in that intro (Windows, Debug, a build before #169).
  skips  Corneria flying, Start pressed four times: the level start's camera jump, each pause and each unpause is a
         camera skip (src/engine/fox_display.c), and recording on at the skip's own (true) is a leak.

A run must show what it measured (StopRecord calls at 20 FPS, camera skips), so a run that saw none fails instead of
passing. --expect-leaks wants a leak in each scenario instead: run it on a build before the fix, to show the test can
fail. One game at a time, no network, about a minute each. Folders: local/test-interp-<scenario>/ for the gdb script
and log, the game's own under local/test-replay-*. Prints one line per check and a RESULT line; exit 0 when all hold.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import replay as replay_tool  # noqa: E402

ROOT = replay_tool.ROOT
BUTTON_START = 0x1000  # OSContPad.button
DISPLAY = ROOT / "src" / "engine" / "fox_display.c"
INTERPOLATION = ROOT / "src" / "port" / "interpolation" / "FrameInterpolation.cpp"

GDB_HEAD = """set pagination off
set confirm off
set logging file {log}
set logging overwrite on
set logging redirect on
set logging enabled on
handle SIGPIPE pass nostop noprint
handle SIGTERM pass nostop noprint
break Play_Init
commands
silent
printf "START level=%d\\n", gCurrentLevel
continue
end
"""
GDB_FPS20 = """break FrameInterpolation_StopRecord
commands
silent
printf "STOP recording=%d frame=%d\\n", '(anonymous namespace)::is_recording', gGameFrameCount
continue
end
break FrameInterpolation.cpp:{guard_line}
commands
silent
printf "GUARD frame=%d\\n", gGameFrameCount
continue
end
run
"""
GDB_PAUSE = """break fox_display.c:{skip_line}
commands
silent
printf "SKIP frame=%d\\n", gGameFrameCount
continue
end
break fox_display.c:{end_line}
commands
silent
printf "SKIPEND recording=%d frame=%d\\n", '(anonymous namespace)::is_recording', gGameFrameCount
continue
end
run
"""

# name: (start, frames, presses, CVars, gdb part)
SCENARIOS = {
    "fps20": (replay_tool.LEVEL, 800, {}, {"gInterpolationFPS": 20, "gVsyncEnabled": 0}, GDB_FPS20),
    "skips": (replay_tool.FLYING, 420, {100: BUTTON_START, 160: BUTTON_START, 240: BUTTON_START, 300: BUTTON_START},
              {}, GDB_PAUSE),
}


def line_of(path, text, after=None):
    """The number of the first line holding text, after the line holding `after` if given."""
    lines = path.read_text(encoding="utf-8").splitlines()
    start = next(n for n, line in enumerate(lines) if after in line) if after else 0
    for number in range(start, len(lines)):
        if text in lines[number]:
            return number + 1
    sys.exit(f"interp_check: {text!r} not found in {path}")


def write_replay(path, start, frames, presses):
    """Input that presses only what it is told; the layout is ReplayHeader and ReplayFrame in src/port/coop/test/replay.c."""
    data = bytearray(b"SFCR" + struct.pack("<III6i", 2, 20, 204, *start.seeds))
    idle = bytes(20)
    for frame in range(frames):
        pads = struct.pack("<H18x", presses.get(frame, 0)) + idle * 3
        data += struct.pack("<I", frame) + pads + pads + bytes(40)
    path.write_bytes(data)


def gdb_wrapper(folder, exe, part):
    """A script that starts exe under gdb, standing in for exe in replay.run_game. Returns it."""
    skip = line_of(DISPLAY, "FrameInterpolation_ShouldInterpolateFrame(false);", after="if (bigJump) {")
    end = line_of(DISPLAY, "FrameInterpolation_ShouldInterpolateFrame(true);", after="// @port Re-enable Interpolation")
    guard = line_of(INTERPOLATION, "return;", after="if (current_path.size() <= 1) {")
    log = folder / "gdb.log"
    commands = folder / "interp.gdb"
    commands.write_text(GDB_HEAD.format(log=log) + part.format(skip_line=skip, end_line=end, guard_line=guard),
                        encoding="utf-8")
    wrapper = folder / "interp-gdb.sh"
    wrapper.write_text(f'#!/bin/sh\nexec gdb -q -batch -x "{commands}" --args "{exe}"\n', encoding="utf-8")
    wrapper.chmod(0o755)
    return wrapper, log


def read_log(path):
    """[(kind, {field: int})] of what the stops printed."""
    stops = []
    text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
    for match in re.finditer(r"^(START|STOP|SKIPEND|SKIP|GUARD) (.*)$", text, re.MULTILINE):
        stops.append((match[1], {k: int(v) for k, v in (f.split("=") for f in match[2].split())}))
    return stops


def run(name, exe, expect_leaks):
    start, frames, presses, cvars, part = SCENARIOS[name]
    folder = ROOT / "local" / f"test-interp-{name}"
    folder.mkdir(parents=True, exist_ok=True)
    replay = folder / "input.sfcr"
    write_replay(replay, start, frames, presses)
    wrapper, log = gdb_wrapper(folder, exe, part)
    config = {"CVars": cvars} if cvars else None
    values, game, seconds = replay_tool.run_game("play", replay, wrapper, 900, tag=f"interp-{name}", config=config,
                                                 start=start)
    stops = read_log(log)
    results = [(any(k == "START" for k, _ in stops), f"gdb saw the level start ({seconds:.0f} s, folder {game})")]
    if name == "fps20":
        seen = [v for k, v in stops if k == "STOP"]
        guards = {v["frame"] for k, v in stops if k == "GUARD"}
        leaks = sorted({v["frame"] for v in seen if v["recording"]} | guards)
        results.append((len(seen) > 300, f"{len(seen)} frames recorded at gInterpolationFPS 20"))
        print(f"interp_check: {name}: info: {len(guards)} frames with a pop refused at the root (#169)")
    else:
        seen = [v for k, v in stops if k == "SKIP"]
        leaks = [v["frame"] for k, v in stops if k == "SKIPEND" and v["recording"]]
        results.append((len(seen) >= 4, f"{len(seen)} camera skips, from the pauses"))
    where = f" (game frames {leaks[0]} to {leaks[-1]})" if leaks else ""
    if expect_leaks:
        results.append((bool(leaks), f"recording leaked on in {len(leaks)} frames{where}: this build has the bug"))
    else:
        results.append((not leaks, f"recording leaked on in {len(leaks)} frames{where}"))
    return results


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--exe", help="Starship executable (default: newest build found)")
    p.add_argument("--only", help="scenarios, comma-separated: " + ",".join(SCENARIOS))
    p.add_argument("--expect-leaks", action="store_true", help="each scenario must leak: for a build before the fix")
    args = p.parse_args()
    if sys.platform != "linux":
        sys.exit("interp_check: Linux only, it runs the game under gdb")
    exe = replay_tool.find_exe(args.exe)
    names = args.only.split(",") if args.only else list(SCENARIOS)
    failed = 0
    for name in names:
        if name not in SCENARIOS:
            sys.exit(f"interp_check: no scenario {name}")
        for ok, text in run(name, exe, args.expect_leaks):
            print(f"interp_check: {name}: {'ok  ' if ok else 'FAIL'} {text}", flush=True)
            failed += 0 if ok else 1
    print(f"RESULT: {'PASS' if not failed else f'FAIL, {failed} checks'} on {exe}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
