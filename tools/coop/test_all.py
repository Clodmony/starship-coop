#!/usr/bin/env python3
"""Every test of this repo in one command, unattended (#147).

    python tools/coop/test_all.py --base-exe <file>                    # Debug in build-cmake; prints a short summary
    python tools/coop/test_all.py --config Release --base-exe <file>   # the same on a Release build, in build-release
    python tools/coop/test_all.py --no-build --only rails-2,views      # just those, on what is built
    python tools/coop/test_all.py --list

It builds first (build.py, with its checks), then runs the steps in lanes:

  serial  one step at a time: tools that bind ports or build test programs of their own, and the mutation checks, which
          change a source file while they run.
  game    --lanes steps at a time: tools that only start the game, each game in a run folder of its own. Longest first.

The serial lane runs beside the game lanes. Nothing goes to the screen while it runs. Each step's whole output is
<name>.log in a stamped folder local/test-all-<date>-<time>/, and SUMMARY.txt there gets one line per step as it ends.
At the end it prints the failures, the longest step and one RESULT line. A step that outlives --timeout is stopped, with
what it started, and counts as failed. When all steps pass, the run folders they left in local/ are deleted (--keep
keeps them); a failure keeps them for reading.

A build that is not Debug leaves out what only Debug can do: OPTIMISED below, printed at the end of a run.

Two steps need a second build, and without one they do not run: coop-off plays co-op off on this build and --base-exe,
the build before your change, and builds plays this build against the other config of the same commit, found by
itself in the other default folder. The last step checks that no tracked file changed during the run.

Exit code 0 when every step ran and passed, 1 when one failed, 2 when all that ran passed and a step could not run.
--only and --skip are a choice, so what they leave out does not count as not run.

On a machine shared with other work, --game-wrap, --net-wrap and --build-wrap each take a command that waits for a free
slot and then runs what it is given: they go in front of steps that start games, loopback tests and the build.

The lanes come from measurements, see docs/coop/NOTES.md (#147): GAME_LANES below. Serial steps that start
games want a slot too, so on a machine with five slots four lanes leave them one.
"""
import argparse
import json
import os
import platform
import queue
import shlex
import shutil
import signal
import subprocess
import sys
import threading
import time
from collections import namedtuple
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import replay as replay_tool  # noqa: E402
from run_folder import make_run_folder  # noqa: E402

ROOT = replay_tool.ROOT
TOOLS = ROOT / "tools" / "coop"
LOCAL = ROOT / "local"
IS_LINUX = platform.system() == "Linux"
IS_WINDOWS = platform.system() == "Windows"
GAME_LANES = 4  # 2, 3, 4 and 5 took 7m53s, 5m36s, 4m56s and 5m41s: see docs/coop/NOTES.md (#147)
LOCKSTEP_JOBS = 4  # scenarios of the lockstep step at a time: 14 min one after the other, 4m48s with 4 (#290)
STEP_TIMEOUT = 3600  # seconds; all of rates took 2500 at load 43, so it is two steps now
STOP_WAIT = 15  # seconds a stopped step has before it is killed
SERIAL, GAME, MUTANTS = "serial", "game", "mutants"
MUT_TREE = LOCAL / "mutation-tree"  # the second checkout of the mutation suites (mutation_tree.py, #290)
FOLDERS = {"Debug": "build-cmake", "Release": "build-release", "RelWithDebInfo": "build-relwithdebinfo"}
CONFIG_PAIR = {"Debug": "Release", "Release": "Debug"}

# What a build without Debug's symbols cannot do, and that a Debug run covers: step -> (its tool and arguments there,
# what that leaves out). push_frame is inlined and gdb has no lines to stop on. (steam-mutants left out
# message_released_too_early here until #158: the fake's poison of a released message was a memset before free, which
# -O2 drops.)
OPTIMISED = {
    "exit-test": (["exit_test.py", "--skip", "fault-main,fault-stuck"], "exit-test fault-main, fault-stuck"),
    "wingman": (["wingman_check.py", "run", "chain"], "wingman alive, down, sitout"),
}

# name, lane, the tool and its arguments, what it takes (see command_of), what goes in front of it, Linux only
Step = namedtuple("Step", "name lane command takes wrap linux", defaults=(None, None, False))


def all_steps():
    """Every step, in the order its lane takes them. The mutation checks break a source file while they run, which
    check.py would read and version_test.py would show as a dirty tree, so they run in a second checkout beside the
    other lanes (mutation_tree.py, #290); with --mutants-in-tree they come last in the serial lane."""
    steps = [
        Step("checks", SERIAL, ["check.py"]),
        Step("package", SERIAL, ["package_test.py"]),
        Step("math", SERIAL, ["math_test.py"]),
        Step("rand_order", SERIAL, ["rand_order_test.py"]),
        Step("handover", SERIAL, ["handover_test.py"]),
        Step("desync_dump", SERIAL, ["desync_dump_test.py"]),
        Step("net", SERIAL, ["net_test.py"], "build", "net"),
        Step("session", SERIAL, ["session_test.py"], "build", "net"),
        Step("steam", SERIAL, ["steam_test.py"], "build", "net"),
        Step("menu", SERIAL, ["menu_test.py", "--game"], "both", "game"),
        Step("input", SERIAL, ["input_test.py"], "both", "game"),
        Step("join", SERIAL, ["join_test.py"], "exe", "game"),
        Step("lockstep", SERIAL, ["lockstep_test.py", "--jobs", str(LOCKSTEP_JOBS)], "both", "game"),
        Step("lockstep-full", SERIAL, ["lockstep_test.py", "--only", "full"], "both", "game"),
        Step("version", SERIAL, ["version_test.py"], "exe", "game", True),
        Step("net-asan", SERIAL, ["net_test.py", "--asan"], "build", "net", True),
        Step("session-asan", SERIAL, ["session_test.py", "--asan"], "build", "net", True),
        Step("steam-asan", SERIAL, ["steam_test.py", "--asan"], "build", "net", True),
        Step("menu-asan", SERIAL, ["menu_test.py", "--asan"], "build", "net", True),
        Step("session-mutants", MUTANTS, ["session_mutants.py"], "build", "net"),
        Step("session-mutants-asan", MUTANTS, ["session_mutants.py", "--asan"], "build", "net", True),
        Step("net-mutants", MUTANTS, ["net_mutants.py"], "build", "net"),
        Step("net-mutants-asan", MUTANTS, ["net_mutants.py", "--asan"], "build", "net", True),
        Step("lockstep-mutants", MUTANTS, ["lockstep_mutants.py"], "build", "net"),
        Step("desync-mutants", MUTANTS, ["desync_mutants.py"], "build", "net"),
        Step("steam-mutants", MUTANTS, ["steam_mutants.py"], "build", "net"),
    ]
    # Longest first, so no lane is left with a long one at the end. exit-test goes last all the same: it waits on the
    # clock, so it costs no time there, and under load a game can hang in the crash handler (docs/coop/NOTES.md).
    game = [
        ("rates-rails", ["hash_check.py", "rates", "--section", "rails"], "exe", False),
        ("rates-all-range", ["hash_check.py", "rates", "--section", "all-range"], "exe", False),
        ("builds", ["hash_check.py", "builds"], "other", False),
        ("determinism", ["determinism.py"], "exe", False),
        ("view-check", ["view_check.py"], "exe", False),
        ("solar-heat", ["solar_check.py"], "exe", False),
        ("wingman", ["wingman_check.py", "all"], "exe", True),
        ("full-level", ["full_level.py"], "exe", False),
        ("lowhealth-allrange", ["player_trace.py", "lowhealth", "--players", "2", "--section", "all-range"], "exe",
         False),
        ("lowhealth-rails", ["player_trace.py", "lowhealth", "--players", "2"], "exe", False),
        ("views", ["hash_check.py", "views"], "exe", False),
        ("draw", ["hash_check.py", "draw"], "exe", False),
        ("replay-test", ["replay_test.py", "--jobs", "2"], "exe", False),
        ("rails-2", ["player_trace.py", "rails", "--players", "2"], "exe", False),
        ("rails-4", ["player_trace.py", "rails", "--players", "4"], "exe", False),
        ("shots", ["player_trace.py", "shots", "--players", "2"], "exe", False),
        ("coop-off", ["hash_check.py", "base"], "base", False),
        ("hash-run-2", ["hash_check.py", "run", "--players", "2", "--poke-player", "1"], "exe", False),
        ("hash-run-1", ["hash_check.py", "run"], "exe", False),
        ("effects", ["player_trace.py", "effects", "--players", "2"], "exe", False),
        ("effects-allrange", ["player_trace.py", "effects", "--players", "2", "--all-range"], "exe", False),
        ("hash-start-2", ["hash_check.py", "start", "--players", "2"], "exe", False),
        ("hash-start-1", ["hash_check.py", "start", "--players", "1"], "exe", False),
        ("hash-probe-2", ["hash_check.py", "probe", "--players", "2"], "exe", False),
        ("hash-probe-1", ["hash_check.py", "probe", "--players", "1"], "exe", False),
        ("exit-test", ["exit_test.py"], "exe", True),
    ]
    return steps + [Step(name, GAME, command, takes, "game", only_linux) for name, command, takes, only_linux in game]


def main_exe(build_dir, config):
    if IS_WINDOWS:
        return build_dir / config / "Starship.exe"
    return build_dir / "Starship"


def game_exe(args):
    return Path(args.exe).resolve() if args.exe else main_exe(args.build_dir, args.config)


def other_build(args):
    """The other config's executable of the same commit, or (None, why not)."""
    config = CONFIG_PAIR.get(args.config)
    if config is None:
        return None, f"builds compares Debug and Release, not {args.config}"
    folder = args.build_dir if IS_WINDOWS else ROOT / FOLDERS[config]
    exe = main_exe(folder, config)
    if not exe.exists():
        return None, f"no {config} build in {folder.name}"
    if read_id(folder) != read_id(args.build_dir):
        return None, f"the {config} build in {folder.name} is of another commit"
    return exe, None


def read_id(folder):
    header = Path(folder) / "coop" / "coop_build_id.h"
    return header.read_text(encoding="utf-8") if header.exists() else None


def command_of(step, args, exe, other, root=ROOT):
    """What to run for a step, or (None, why it cannot run). takes says which of the builds a tool gets, and how. root
    is the checkout the tool is run from: the mutation suites run from their own (MUT_TREE)."""
    tool = OPTIMISED[step.name][0] if args.config != "Debug" and step.name in OPTIMISED else step.command
    command = [sys.executable, "-u", str(root / "tools" / "coop" / tool[0]), *tool[1:]]
    build_dir = args.build_dir if root == ROOT else root / FOLDERS[args.config]
    if step.takes in ("exe", "both"):
        command += ["--exe", str(exe)]
    if step.takes in ("build", "both"):
        command += ["--build-dir", str(build_dir), "--config", args.config]
    if step.takes == "base":
        if args.base_exe is None:
            return None, "no --base-exe"
        command += ["--base", str(args.base_exe), "--exe", str(exe)]
    if step.takes == "other":
        if other is None:
            return None, args.other_why
        debug, release = (exe, other) if args.config == "Debug" else (other, exe)
        command += ["--debug", str(debug), "--release", str(release)]
    return [*wrap_of(step.wrap, args), *command], None


def wrap_of(wrap, args):
    return shlex.split({"game": args.game_wrap, "net": args.net_wrap, "build": args.build_wrap, None: ""}[wrap] or "")


class Run:
    """One run of test_all: what is running, how it went, and the summary file."""

    def __init__(self, folder, timeout):
        self.folder = folder
        self.timeout = timeout
        self.lock = threading.RLock()  # the signal handler takes it too, on the main thread
        self.results = []  # (name, ok, seconds, line)
        self.running = {}
        self.stopping = threading.Event()
        self.summary = folder / "SUMMARY.txt"
        self.top_load = 0.0

    def write(self, line):
        with self.lock, open(self.summary, "a", encoding="utf-8") as out:
            out.write(line + "\n")

    def note_load(self):
        if hasattr(os, "getloadavg"):
            self.top_load = max(self.top_load, os.getloadavg()[0])

    def run(self, name, command, cwd=None):
        """Run one step in a process group of its own, in cwd (this checkout by default). True if it passed."""
        log = self.folder / f"{name}.log"
        started = time.monotonic()
        with open(log, "wb") as out:
            proc = subprocess.Popen(command, cwd=cwd or ROOT, stdin=subprocess.DEVNULL, stdout=out,
                                    stderr=subprocess.STDOUT,
                                    env=step_env(), start_new_session=not IS_WINDOWS)
            with self.lock:
                self.running[name] = proc
            try:
                code = proc.wait(timeout=self.timeout)
            except subprocess.TimeoutExpired:
                stop_group(proc)
                code = "timeout"
            with self.lock:
                self.running.pop(name, None)
        seconds = time.monotonic() - started
        self.note_load()
        self.finish(name, code == 0, seconds, f"rc={code}", last_line(log))
        return code == 0

    def finish(self, name, passed, seconds, status, line):
        self.write(f"{'ok    ' if passed else 'FAILED'} {name} ({seconds:.0f}s"
                   f"{'' if passed else ', ' + status}): {line}")
        with self.lock:
            self.results.append((name, passed, seconds, line))

    def stop_all(self):
        self.stopping.set()
        with self.lock:
            running = list(self.running.values())
        for proc in running:
            stop_group(proc)


def step_env():
    env = {key: value for key, value in os.environ.items() if not key.startswith("SF64_COOP_") and key != "SHIP_HOME"}
    # What game_env gives a test game, for the few tools that start one by other means.
    env.setdefault("SDL_AUDIODRIVER", "dummy")
    if IS_LINUX:
        env.setdefault("SDL_VIDEODRIVER", "offscreen")
        env.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
        env.setdefault("GALLIUM_DRIVER", "llvmpipe")
    return env


def stop_group(proc):
    """Stop a step and what it started: only that, as it has a process group of its own. Games hang on SIGTERM (#71),
    so anything still there after STOP_WAIT seconds is killed."""
    if IS_WINDOWS:
        proc.kill()
        return
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        proc.wait(timeout=STOP_WAIT)
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    proc.wait()


def last_line(log):
    data = log.read_bytes()[-4096:].decode("utf-8", "replace") if log.exists() else ""
    lines = [line.strip() for line in data.splitlines() if line.strip()]
    return lines[-1][:110] if lines else "(no output)"


def lane(run, steps):
    """One worker of a lane: take steps until none are left."""
    while not run.stopping.is_set():
        try:
            name, command, *where = steps.get_nowait()
        except queue.Empty:
            return
        run.run(name, command, *where)


def mutants_lane(run, steps):
    """The mutation suites, one after the other in their own checkout. When making that checkout fails, the rest fail
    with it, each saying why."""
    while not run.stopping.is_set():
        try:
            name, command, *where = steps.get_nowait()
        except queue.Empty:
            return
        if not run.run(name, command, *where) and name == "mutants-setup":
            while True:
                try:
                    name = steps.get_nowait()[0]
                except queue.Empty:
                    return
                run.finish(name, False, 0, "no mutation tree", "mutants-setup failed")


def run_steps(run, serial, game, lanes, mutants=()):
    """serial, game and mutants are lists of (name, command) or (name, command, folder to run in)."""
    workers = []
    queues = (serial, 1, lane), (game, lanes, lane), (mutants, 1, mutants_lane)
    for steps, count, work in queues:
        waiting = queue.Queue()
        for step in steps:
            waiting.put(step)
        workers += [threading.Thread(target=work, args=(run, waiting), daemon=True) for _ in range(count if steps else 0)]
    for worker in workers:
        worker.start()
    for worker in workers:
        while worker.is_alive():
            worker.join(1)


def tracked_changes():
    done = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT, capture_output=True,
                          text=True)
    return done.stdout


def duration(seconds):
    return f"{int(seconds) // 60}m{int(seconds) % 60:02d}s"


def load_plan(path):
    """A list of steps from a file, in place of the real ones: test_all_test.py runs this tool on made-up steps."""
    return json.loads(Path(path).read_text(encoding="utf-8"))


def chosen_steps(args):
    """The steps --only and --skip leave. A typing error stops here, not after the build."""
    steps = [s for s in all_steps() if IS_LINUX or not s.linux]
    known = {s.name for s in steps}
    only = [n.strip() for n in args.only.split(",") if n.strip()] if args.only else None
    skip = [n.strip() for n in args.skip.split(",") if n.strip()] if args.skip else []
    unknown = [n for n in [*(only or []), *skip] if n not in known]
    if unknown:
        sys.exit(f"test_all: unknown step(s) {', '.join(unknown)}; known: {', '.join(sorted(known))}")
    return [s for s in steps if (only is None or s.name in only) and s.name not in skip]


def make_plan(args, chosen):
    """Returns (serial steps, game steps, mutation steps, steps that cannot run as (name, why))."""
    if args.plan:
        plan = load_plan(args.plan)
        pick = lambda lane_name: [(s["name"], s["command"]) for s in plan if s["lane"] == lane_name]  # noqa: E731
        return pick(SERIAL), pick(GAME), pick(MUTANTS), [(s["name"], s["why"]) for s in plan if s["lane"] == "none"]

    other, args.other_why = other_build(args)
    exe = game_exe(args)
    beside = not (args.mutants_in_tree or IS_WINDOWS)  # a second checkout needs symbolic links
    serial, game, mutants, missing = [], [], [], []
    for step in chosen:
        in_tree = step.lane == MUTANTS and beside
        command, why = command_of(step, args, exe, other, MUT_TREE if in_tree else ROOT)
        if command is None:
            missing.append((step.name, why))
        elif in_tree:
            mutants.append((step.name, command, MUT_TREE))
        else:
            (game if step.lane == GAME else serial).append((step.name, command))
    if mutants:
        setup = [sys.executable, "-u", str(TOOLS / "mutation_tree.py"), "make", "--path", str(MUT_TREE), "--config",
                 args.config, "--build-dir", FOLDERS[args.config]]
        mutants.insert(0, ("mutants-setup", setup, ROOT))
    return serial, game, mutants, missing


def parse_args():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--config", default="Debug", choices=sorted(FOLDERS), help="default: Debug")
    p.add_argument("--build-dir", type=Path, help="default: build-cmake for Debug, build-release for Release, "
                                                  "build/x64 on Windows")
    p.add_argument("--exe", help="Starship executable (default: the one in the build folder)")
    p.add_argument("--base-exe", help="Starship executable of the build before your change, for coop-off")
    p.add_argument("--lanes", type=int, default=GAME_LANES, help=f"game steps at a time (default {GAME_LANES})")
    p.add_argument("--no-build", action="store_true", help="test what is built")
    p.add_argument("--only", help="comma-separated steps (default: all)")
    p.add_argument("--skip", help="comma-separated steps to leave out")
    p.add_argument("--list", action="store_true", help="print the steps and stop")
    p.add_argument("--keep", action="store_true", help="keep the run folders of a run that passed")
    p.add_argument("--timeout", type=float, default=STEP_TIMEOUT, help=f"seconds a step may take ({STEP_TIMEOUT})")
    p.add_argument("--game-wrap", help="command in front of each step that starts games (see the top of this file)")
    p.add_argument("--net-wrap", help="command in front of each loopback test and mutation check")
    p.add_argument("--build-wrap", help="command in front of the build")
    p.add_argument("--mutants-in-tree", action="store_true",
                   help="run the mutation suites last in the serial lane of this checkout, not beside the rest in a "
                        "second one")
    p.add_argument("--plan", help=argparse.SUPPRESS)
    args = p.parse_args()
    folder = "build/x64" if IS_WINDOWS else FOLDERS[args.config]
    build_dir = args.build_dir or Path(folder)
    args.build_dir = build_dir if build_dir.is_absolute() else ROOT / build_dir
    args.base_exe = Path(args.base_exe).resolve() if args.base_exe else None
    return args


def print_list():
    for step in all_steps():
        print(f"{step.name}: {step.lane}, {' '.join(step.command)}" + (", Linux only" if step.linux else ""))


def run_build(run, args):
    command = [*wrap_of("build", args), sys.executable, "-u", str(TOOLS / "build.py"), "--quiet", "--config",
               args.config, "--build-dir", str(args.build_dir)]
    return run.run("build", command)


def usable_game(run, args):
    """False, with the reason in the results, when no game step could start. Only then: each step checks the rest."""
    exe = game_exe(args)
    why = (f"no game at {exe}" if not exe.exists() else
           "no sf64.o2r; run python tools/coop/build.py --assets" if replay_tool.find_data(exe, "sf64.o2r") is None
           else None)
    if why is not None:
        run.finish("setup", False, 0, "stopped", why)
    return why is None


def clean_run_folders(before, folder):
    """Delete what the steps left in local/: folders that are new, named test-*, and not this run's own."""
    for path in LOCAL.glob("test-*"):
        if path.name not in before and path != folder and path.is_dir():
            shutil.rmtree(path, ignore_errors=True)


def main():
    args = parse_args()
    if args.list:
        print_list()
        return 0

    chosen = chosen_steps(args)
    before = {path.name for path in LOCAL.glob("test-*")} if LOCAL.exists() else set()
    folder = make_run_folder(LOCAL, "test-all-")
    run = Run(folder, args.timeout)
    signal.signal(signal.SIGTERM, lambda *_: run.stop_all())
    signal.signal(signal.SIGINT, lambda *_: run.stop_all())
    started = time.monotonic()
    head = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True,
                          text=True).stdout.strip()
    tree = tracked_changes()
    load = f"{os.getloadavg()[0]:.0f}" if hasattr(os, "getloadavg") else "unknown"
    run.write(f"tree {head}, {args.config}, {args.lanes} lanes, load {load}, started {time.strftime('%H:%M:%S')}")

    built = args.no_build or args.plan or run_build(run, args)
    serial, game, mutants, missing = make_plan(args, chosen) if built else ([], [], [], [])
    if built and not args.plan and not usable_game(run, args):
        built = False
    if built:
        try:
            run_steps(run, serial, game, args.lanes, mutants)
        finally:
            if mutants:
                subprocess.run([sys.executable, str(TOOLS / "mutation_tree.py"), "remove", "--path", str(MUT_TREE)],
                               cwd=ROOT, capture_output=True)
        same = tree == tracked_changes()
        run.finish("tree", same, 0, "changed", "no tracked file changed" if same else "a tracked file changed")

    for name, why in missing:
        run.write(f"NOT RUN {name}: {why}")
    failed = [r for r in run.results if not r[1]]
    stopped = run.stopping.is_set()
    took = duration(time.monotonic() - started)
    longest = max(run.results, key=lambda r: r[2], default=None)
    passed = len(run.results) - len(failed)
    logs = folder.relative_to(ROOT) if folder.is_relative_to(ROOT) else folder

    lines = [f"test_all: {head}, {args.config}, {args.lanes} lanes, load {load} to {run.top_load:.0f}, {took}"]
    if longest is not None:
        lines.append(f"test_all: longest step {longest[0]}, {duration(longest[2])}")
    lines += [f"FAILED {name} ({seconds:.0f}s): {line}" for name, _, seconds, line in failed]
    lines += [f"NOT RUN {name}: {why}" for name, why in missing]
    left_out = [OPTIMISED[s.name][1] for s in chosen if s.name in OPTIMISED] if args.config != "Debug" else []
    if left_out:
        lines.append(f"test_all: left out on {args.config}, as a Debug run covers them: {'; '.join(left_out)}")
    if failed or stopped:
        verdict, code = "FAILED", 1
    elif missing:
        verdict, code = "INCOMPLETE", 2
    else:
        verdict, code = "PASS", 0
    note = ", stopped before the end" if stopped else f", {len(missing)} not run" if missing else ""
    lines.append(f"RESULT: {verdict}, {passed}/{len(run.results)} steps ok{note}, {took} (logs: {logs})")
    for line in lines:
        print(line)
    run.write("\n".join(lines))
    if code == 0 and not args.keep and not args.plan:
        clean_run_folders(before, folder)
    return code


if __name__ == "__main__":
    sys.exit(main())
