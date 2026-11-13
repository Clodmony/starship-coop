#!/usr/bin/env python3
"""Tests of how tools/coop/replay_test.py waits (#308), with a stand-in for the game. Linux only; no build, no ROM.

    python tools/coop/replay_test_test.py
    python tools/coop/replay_test_test.py --only late-handler

replay_test.py's person starts replay.py record without --frames and stops its game with SIGTERM, as a person closes
the window. The game writes its config early in its start (InitConfiguration, src/port/Engine.cpp:146) and puts in the
stop handler of a test run later (CoopStopSignal_Init, src/port/Engine.cpp:316). A SIGTERM in between meets upstream's
handler, which hangs in exit() for good (#71), and replay.py and replay_test.py waited on that game with no limit.
The stand-in does the same on its clock: it writes its config at once, hangs for HANG seconds on a SIGTERM that comes
before its handler is in, LATE seconds after it started, and draws (writes READY_FILE, imgui.ini) after that.

  late-handler  the stand-in as above: person must wait until it draws, so that the signal finds the handler, and pass.
  stop-limit    a stand-in whose handler never comes: person must stop it by its PID STOP_LIMIT seconds after the
                signal and fail, not wait out the hang.
  tool-limit    a tool that does not end, with a game of its own: the tool's wait ends TOOL_LIMIT seconds after it
                started, and the tool and its game are gone.
  stop-wait     wait_stopped, which tool-limit relies on to see every child of the tool: it returns only once the
                process is stopped, also for one that forks as fast as it can when the signal comes.
The limits are short here (TOOL_LIMIT counts from the tool's start, so person's scenarios get a longer one than
tool-limit), and every hang is longer: a run that waits one out, as the tool did before, fails on the
time it took, whatever the timing of the machine. Exit code 0 when every scenario holds, 1 otherwise; check.py runs it.
Its folders are deleted when every scenario holds and kept for reading when one fails.
"""
import argparse
import os
import signal
import subprocess
import sys
import textwrap
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import replay_test  # noqa: E402
from run_folder import OwnedFolders, make_run_folder  # noqa: E402

ROOT = replay_test.ROOT
LATE = 1.0  # seconds until the stand-in's stop handler is in
DRAW_AFTER = 0.3  # seconds after that until it draws
READY_FILE = "imgui.ini"  # what the game writes once it draws, as exit_test.py and replay_test.py read it
HANG = 15  # seconds a hang lasts; then the stand-in ends itself, so that nothing is left running
LIMITS = {"READY_LIMIT": 20, "STOP_LIMIT": 3, "TOOL_LIMIT": 12}  # for replay_test.py while this runs
STUCK_LIMIT = 3  # TOOL_LIMIT in tool-limit: the others need more, as the limit counts from the tool's start

STAND_IN = textwrap.dedent(f"""\
    import os, signal, sys, time
    mode, notes = sys.argv[1], sys.argv[2]
    def mark(name, text=""):
        with open(os.path.join(notes, name), "w") as f:
            f.write(text)
    mark("pid", str(os.getpid()))
    with open("starship.cfg.json", "w") as f:  # as InitConfiguration, early in the start
        f.write("{{}}")
    def hang(*_):  # upstream's handler, whose exit() never returns (#71); this one ends in the end
        mark("hung")
        time.sleep({HANG})
        os._exit(3)
    def stop(*_):  # the stop handler of a test run: the game ends as when its window is closed
        mark("stopped")
        report = os.environ.get("SF64_COOP_REPLAY_REPORT")
        if report:
            with open(report, "w") as f:
                f.write("mode=record\\nframes=0\\nrecorded_frames=0\\nresult=RECORDED\\n")
        os._exit(0)
    signal.signal(signal.SIGTERM, hang)
    time.sleep({LATE})
    if mode == "late":
        signal.signal(signal.SIGTERM, stop)
    time.sleep({DRAW_AFTER})
    open("{READY_FILE}", "w").close()  # it draws
    time.sleep({HANG})
    os._exit(4)
""")

# A tool that starts a game and does not end: it waits for HANG seconds, and its game would wait longer.
STUCK_TOOL = textwrap.dedent(f"""\
    import subprocess, sys, time
    game, cwd = sys.argv[1], sys.argv[2]
    subprocess.Popen([game], cwd=cwd)
    time.sleep({HANG})
""")


class Stand:
    """The test's folder under local/, the stand-in and one wrapper per mode, as a tool starts an --exe."""

    def __init__(self):
        self.folder = make_run_folder(ROOT / "local", "test-replaytooltest-")
        (self.folder / "stand_in.py").write_text(STAND_IN, encoding="utf-8")
        for name in ("sf64.o2r", "starship.o2r"):
            (self.folder / name).write_bytes(b"")
        self.pids = []

    def exe(self, mode):
        """The wrapper of mode, and the folder its stand-in leaves its notes in."""
        notes = self.folder / f"notes-{mode}"
        notes.mkdir(exist_ok=True)
        exe = self.folder / f"starship-{mode}"
        exe.write_text(f'#!/bin/sh\nexec "{sys.executable}" -I "{self.folder / "stand_in.py"}" {mode} "{notes}"\n',
                       encoding="utf-8")
        exe.chmod(0o755)
        return exe, notes

    def games_of(self, tool, exe):
        """replay_test.games_of for the stand-in, which runs Python, not exe: a child of the tool that runs it."""
        found = {}
        for pid in filter(str.isdigit, os.listdir("/proc")):
            try:
                parent = int(Path(f"/proc/{pid}/stat").read_text().rpartition(")")[2].split()[1])
                command = Path(f"/proc/{pid}/cmdline").read_bytes().split(b"\0")
                if parent == tool.pid and str(self.folder / "stand_in.py").encode() in command:
                    data = Path(f"/proc/{pid}/environ").read_bytes().decode("utf-8", "replace").split("\0")
                    found[int(pid)] = dict(pair.partition("=")[::2] for pair in data if pair)
            except (OSError, ValueError, IndexError):
                pass
        return found

    def pid_of(self, notes):
        """The PID the stand-in wrote, or None."""
        text = (notes / "pid").read_text() if (notes / "pid").exists() else ""
        if text:
            self.pids.append(int(text))
        return int(text) if text else None

    def end(self):
        """Stop any stand-in still running: only those this test started, which name its own folder."""
        for pid in self.pids:
            try:
                if str(self.folder).encode() in Path(f"/proc/{pid}/cmdline").read_bytes():
                    os.kill(pid, signal.SIGKILL)
            except OSError:
                pass


def running(pid):
    """Whether pid still runs: there and not a zombie, which has ended and waits for its parent."""
    try:
        return pid is not None and Path(f"/proc/{pid}/stat").read_text().rpartition(")")[2].split()[0] != "Z"
    except OSError:
        return False


def check_late_handler(stand, benches):
    exe, notes = stand.exe("late")
    bench = replay_test.Bench(str(exe))
    benches.append(bench)
    began = time.monotonic()
    failures = [f"person: {f}" for f in replay_test.check_person(bench)]
    took = time.monotonic() - began
    if (notes / "hung").exists():
        failures.append("the stand-in got SIGTERM before its handler was in, and hung")
    if not (notes / "stopped").exists():
        failures.append("the stand-in's stop handler never ran")
    if took >= HANG:
        failures.append(f"took {took:.0f}s: it waited out a hang of {HANG}s")
    if running(stand.pid_of(notes)):
        failures.append("the stand-in still runs")
    return failures


def check_stop_limit(stand, benches):
    exe, notes = stand.exe("never")
    bench = replay_test.Bench(str(exe))
    benches.append(bench)
    began = time.monotonic()
    found = replay_test.check_person(bench)
    took = time.monotonic() - began
    failures = [] if any("still running" in f for f in found) else [f"person did not fail on the limit: {found}"]
    if took >= HANG:
        failures.append(f"took {took:.0f}s: it waited out a hang of {HANG}s, not a limit of {LIMITS['STOP_LIMIT']}s")
    if running(stand.pid_of(notes)):
        failures.append("the stand-in still runs")
    return failures


def check_tool_limit(stand, benches):
    exe, notes = stand.exe("never")
    bench = replay_test.Bench(str(exe))
    benches.append(bench)
    tool = stand.folder / "stuck_tool.py"
    tool.write_text(STUCK_TOOL, encoding="utf-8")
    cwd = stand.folder / "stuck-game"
    cwd.mkdir()
    began = time.monotonic()
    replay_test.TOOL_LIMIT = STUCK_LIMIT
    try:
        _, _, found = bench.tool("stuck", [str(tool), str(exe), str(cwd)])
    finally:
        replay_test.TOOL_LIMIT = LIMITS["TOOL_LIMIT"]
    took = time.monotonic() - began
    failures = [] if any("still running" in f for f in found) else [f"the tool's wait did not fail: {found}"]
    if took >= HANG:
        failures.append(f"took {took:.0f}s: it waited for the tool, not for a limit of {STUCK_LIMIT}s")
    pid = stand.pid_of(notes)
    if pid is None:
        failures.append("the tool's game did not start")
    elif running(pid):
        failures.append("the tool's game still runs")
    return failures


def check_stop_wait(stand, benches):
    """A process that forks all the time is stopped when wait_stopped returns, so a look at its children after that
    sees every one it will ever have (#308; the race that made tool-limit fail once in a gate under load)."""
    forker = stand.folder / "forker.py"
    forker.write_text("import os, sys\nwhile True:\n    pid = os.fork()\n    if pid == 0:\n        os._exit(0)\n"
                      "    os.waitpid(pid, 0)\n", encoding="utf-8")
    proc = subprocess.Popen([sys.executable, "-I", str(forker)])
    failures = []
    try:
        time.sleep(0.2)
        for _ in range(20):
            proc.send_signal(signal.SIGSTOP)
            replay_test.wait_stopped(proc.pid)
            state = Path(f"/proc/{proc.pid}/stat").read_text().rpartition(")")[2].split()[0]
            if state != "T":
                failures.append(f"wait_stopped returned with the process in state {state}")
                break
            proc.send_signal(signal.SIGCONT)
            time.sleep(0.01)
    finally:
        proc.kill()
        proc.wait()
    return failures


SCENARIOS = {"late-handler": check_late_handler, "stop-limit": check_stop_limit, "tool-limit": check_tool_limit,
             "stop-wait": check_stop_wait}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--only", help="comma-separated scenarios (default: all)")
    args = p.parse_args()
    chosen = list(SCENARIOS) if not args.only else [s.strip() for s in args.only.split(",") if s.strip()]
    unknown = [s for s in chosen if s not in SCENARIOS]
    if unknown:
        sys.exit(f"replay_test_test: unknown scenario(s) {', '.join(unknown)}; known: {', '.join(SCENARIOS)}")
    if not sys.platform.startswith("linux"):
        print("replay_test_test: skipped, Linux only: person and the stop of a tool's games read /proc")
        return 0

    for name, value in LIMITS.items():
        setattr(replay_test, name, value)
    owned = OwnedFolders(ROOT / "local")
    stand = Stand()
    replay_test.games_of = stand.games_of
    benches = []
    failed = 0
    try:
        for name in chosen:
            began = time.monotonic()
            failures = SCENARIOS[name](stand, benches)
            failed += bool(failures)
            print(f"replay_test_test: {name}: " + (f"FAIL: {'; '.join(failures)}" if failures else "ok")
                  + f" ({time.monotonic() - began:.1f}s)", flush=True)
    finally:
        stand.end()
    if failed:
        print(f"replay_test_test: folders kept: {', '.join(path.name for path in owned.made())}")
        owned.close()
        print("replay_test_test: FAIL")
        return 1
    for bench in benches:
        bench.clean()
    owned.delete()
    print("replay_test_test: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
