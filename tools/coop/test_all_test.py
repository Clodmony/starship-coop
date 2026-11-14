#!/usr/bin/env python3
"""Tests test_all.py on made-up steps (#147).

    python tools/coop/test_all_test.py                   # unattended; prints PASS or FAIL
    python tools/coop/test_all_test.py --only fail,stop
    python tools/coop/test_all_test.py --list

Each scenario runs the real tool as its own process, from a copy in a small git repository of its own, on a plan of
steps that are short Python programs. So what a scenario sees is what a person sees: the exit code, the printed summary,
the logs, and which processes are left. A failing step must not stop the others, a step that outlives its time must go
with what it started, a stop signal must end the run with the same result, and only a run where all ran and passed may
exit 0.

No build, no ROM, a few seconds; check.py runs it. Exit code 0 when every scenario holds, 1 otherwise. Folders are in
local/test-testall-*, deleted when all scenarios hold, kept for reading when one fails.
"""
import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "coop"
COPIED = ("test_all.py", "replay.py", "run_folder.py", "mutation_tree.py")
MAX_SUMMARY_LINES = 6  # printed by a run of a few failures: this is what a person reads
HELD = 3  # seconds a slow step stays, more than a scenario needs to look at it
# Not the note of an outer test_all run (run_folder.py, #283): the bench runs are runs of their own.
ENV = {key: value for key, value in os.environ.items() if not key.startswith("GIT_") and key != "COOP_TEST_ALL_OWNED"}

OK = "print('step output')"
BAD = "print('it broke'); raise SystemExit(3)"
SLEEP = f"import time; print('waiting', flush=True); time.sleep({HELD})"
# Starts a child that stays, writes its pid, and stays too.
PARENT = """
import subprocess, sys, time
child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])
open(sys.argv[1], 'w').write(str(child.pid))
time.sleep(60)
"""
# mutants.py on a table of its own, whose first break hangs: the moment a source holds a break.
MUTATION = """
import sys, time
from pathlib import Path
tools, folder = sys.argv[1:]
sys.path.insert(0, tools)
import mutants
mutants.FOLDER = Path(folder)
mutants.LOCK = mutants.FOLDER / "lock"
source = mutants.FOLDER / "source.txt"
table = mutants.Table(source=source, test_script=source, scenarios={"one": None}, target="no-such-target",
                      find_exe=lambda *args: None, breaks=[("break", "text", "broken", ["one"])])
def build(self, label, changed=True):
    if label == "break":
        time.sleep(60)
    return True, ""
mutants.Runner.build = build
mutants.Runner.scenarios = lambda self, tests, label: ({t: "ok" for t in tests}, {t: "" for t in tests})
sys.argv = ["mutants.py", "--out", str(mutants.FOLDER / "out")]
sys.exit(mutants.main("test_all_test", lambda args: table))
"""
# Notes when it started and ended, for the lanes, in a file of its own: appends from several processes to one file can
# overwrite each other on Windows and lose a line (#168).
TIMED = """
import sys, time
start = time.monotonic()
time.sleep(0.4)
open(sys.argv[1], 'w').write(f'{sys.argv[2]} {start} {time.monotonic()}\\n')
"""

# Makes a run folder in local/ as a test tool does (run_folder.make_run_folder), then waits: it writes the file
# argv[4] once the folder is there, and ends when the file argv[5] is there (a name of "-" for neither).
MAKES = """
import sys, time
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from run_folder import make_run_folder
folder = make_run_folder(Path(sys.argv[2]), sys.argv[3])
print(folder.name, flush=True)
if sys.argv[4] != '-':
    Path(sys.argv[4]).write_text(folder.name)
end = time.monotonic() + 30
while sys.argv[5] != '-' and not Path(sys.argv[5]).exists() and time.monotonic() < end:
    time.sleep(0.05)
"""

# A step whose tool crashes once it made its run folder (os._exit: no cleanup of its own); the step itself passes.
CRASHES = """
import subprocess, sys
from pathlib import Path
child = ("import os, sys; sys.path.insert(0, sys.argv[1]); from pathlib import Path; "
         "from run_folder import make_run_folder; Path(sys.argv[4]).write_text(make_run_folder(Path(sys.argv[2]), "
         "sys.argv[3]).name); os._exit(3)")
code = subprocess.run([sys.executable, '-c', child, *sys.argv[1:5]]).returncode
print('the tool crashed with exit code', code)
"""


class Failed(Exception):
    pass


def step(name, lane, program, *args):
    return {"name": name, "lane": lane, "command": [sys.executable, "-c", program, *map(str, args)]}


class Bench:
    """A repository with a copy of the tool in it. run starts the tool on a plan and waits for it."""

    def __init__(self, work):
        self.work = work
        self.repo = work / "repo"
        (self.repo / "tools" / "coop").mkdir(parents=True)
        for name in COPIED:
            shutil.copy2(TOOLS / name, self.repo / "tools" / "coop" / name)
        self.tracked = self.repo / "tracked.txt"
        self.tracked.write_text("as committed\n", encoding="utf-8")
        git = ["git", "-C", str(self.repo), "-c", "user.name=test", "-c", "user.email=test@invalid"]
        subprocess.run(["git", "init", "-q", str(self.repo)], env=ENV, check=True)
        subprocess.run([*git, "add", "tracked.txt"], env=ENV, check=True)
        subprocess.run([*git, "commit", "-q", "-m", "test"], env=ENV, check=True)

    def start(self, plan, *args):
        path = self.work / "plan.json"
        path.write_text(json.dumps(plan), encoding="utf-8")
        return subprocess.Popen([sys.executable, str(self.repo / "tools" / "coop" / "test_all.py"), "--plan", str(path),
                                 *map(str, args)], cwd=self.repo, env=ENV, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, encoding="utf-8")

    def run(self, plan, *args):
        proc = self.start(plan, *args)
        output = proc.communicate(timeout=60)[0]
        return proc.returncode, output

    def folder(self):
        return next((self.repo / "local").glob("test-all-*"))


def want(condition, message):
    if not condition:
        raise Failed(message)


def want_result(code, output, wanted_code, wanted):
    want(code == wanted_code, f"exit code {code}, want {wanted_code}: {output!r}")
    results = re.findall(r"^RESULT: .*$", output, re.MULTILINE)
    want(len(results) == 1, f"{len(results)} RESULT lines: {output!r}")
    want(results[0].startswith(f"RESULT: {wanted}"), f"{results[0]}, want {wanted}")
    want(output.strip().splitlines()[-1] == results[0], "RESULT is not the last line")
    want(len(output.strip().splitlines()) <= MAX_SUMMARY_LINES, f"printed too much: {output!r}")


def gone(pid):
    """True when a process is gone. A dead child of a live process stays as a zombie, which counts as gone."""
    try:
        return Path(f"/proc/{pid}/stat").read_text().rpartition(")")[2].split()[0] == "Z"
    except OSError:
        return True


def wait_gone(pid):
    end = time.monotonic() + HELD
    while time.monotonic() < end and not gone(pid):
        time.sleep(0.05)
    return gone(pid)


def scenario_pass(bench):
    plan = [step("one", "serial", OK), step("two", "serial", OK), step("three", "game", OK), step("four", "game", OK)]
    code, output = bench.run(plan)
    want_result(code, output, 0, "PASS, 5/5 steps ok")
    folder = bench.folder()
    want("step output" in (folder / "three.log").read_text(encoding="utf-8"), "a step's output is not in its log")
    summary = (folder / "SUMMARY.txt").read_text(encoding="utf-8")
    for name in ("one", "two", "three", "four", "tree"):
        want(re.search(rf"^ok +{name} ", summary, re.MULTILINE), f"no line for {name} in SUMMARY.txt")
    want("step output" not in output, "a step's output reached the summary")


def scenario_fail(bench):
    plan = [step("first", "serial", BAD), step("second", "serial", OK), step("broken", "game", BAD),
            step("third", "game", OK)]
    code, output = bench.run(plan)
    want_result(code, output, 1, "FAILED, 3/5 steps ok")
    want(re.search(r"^FAILED broken \(\d+s\): it broke$", output, re.MULTILINE), f"broken not named: {output!r}")
    want(re.search(r"^FAILED first ", output, re.MULTILINE), f"first not named: {output!r}")
    for name in ("second", "third"):
        want((bench.folder() / f"{name}.log").exists(), f"a failure stopped {name}")
    summary = (bench.folder() / "SUMMARY.txt").read_text(encoding="utf-8")
    want("rc=3" in summary, "the exit code is not in SUMMARY.txt")


def scenario_not_run(bench):
    plan = [step("ran", "game", OK), {"name": "needs-more", "lane": "none", "why": "no second build"}]
    code, output = bench.run(plan)
    want_result(code, output, 2, "INCOMPLETE, 2/2 steps ok, 1 not run")
    want("NOT RUN needs-more: no second build" in output, f"no reason: {output!r}")


def scenario_failed_not_run(bench):
    plan = [step("broken", "game", BAD), {"name": "needs-more", "lane": "none", "why": "no second build"}]
    code, output = bench.run(plan)
    want_result(code, output, 1, "FAILED, 1/2 steps ok, 1 not run")


def scenario_lanes(bench):
    times = bench.work / "times"
    times.mkdir()
    names = [(f"serial-{i}", "serial") for i in range(3)] + [(f"game-{i}", "game") for i in range(6)]
    plan = [step(name, lane, TIMED, times / f"{name}.txt", lane) for name, lane in names]
    code, output = bench.run(plan, "--lanes", 2)
    want_result(code, output, 0, "PASS, 10/10 steps ok")
    lines = [(times / f"{name}.txt").read_text(encoding="utf-8").split() for name, _ in names]
    want(all(len(fields) == 3 for fields in lines), f"a step did not note its times: {lines!r}")
    want(most_at_once(lines, {"serial"}) == 1, f"{most_at_once(lines, {'serial'})} serial steps ran at once")
    want(most_at_once(lines, {"game"}) == 2, f"{most_at_once(lines, {'game'})} game steps ran at once in 2 lanes")
    want(most_at_once(lines, {"serial", "game"}) == 3, "the serial lane did not run beside the game lanes")


def most_at_once(lines, lanes):
    """The most steps of these lanes that were running at one moment; lines are (lane, start, end) as TIMED notes them."""
    points = []
    for lane, start, end in lines:
        if lane in lanes:
            points += [(float(start), 1), (float(end), -1)]
    running = peak = 0
    for _, change in sorted(points, key=lambda p: (p[0], p[1])):
        running += change
        peak = max(peak, running)
    return peak


def scenario_pure_beside(bench):
    """The pure lane (checks and tools that start no game) is one at a time, and beside the serial, the game and the
    mutation lanes (#290)."""
    times = bench.work / "times"
    times.mkdir()
    names = [(f"pure-{i}", "pure") for i in range(3)] + [(f"serial-{i}", "serial") for i in range(2)] + \
            [(f"game-{i}", "game") for i in range(2)] + [("mutants-setup", "mutants"), ("mut-1", "mutants")]
    plan = [step(name, lane, TIMED, times / f"{name}.txt", lane) for name, lane in names]
    code, output = bench.run(plan, "--lanes", 2)
    want_result(code, output, 0, "PASS, 10/10 steps ok")
    lines = [(times / f"{name}.txt").read_text(encoding="utf-8").split() for name, _ in names]
    want(all(len(fields) == 3 for fields in lines), f"a step did not note its times: {lines!r}")
    want(most_at_once(lines, {"pure"}) == 1, f"{most_at_once(lines, {'pure'})} pure steps ran at once")
    want(most_at_once(lines, {"serial"}) == 1, f"{most_at_once(lines, {'serial'})} serial steps ran at once")
    every = {"pure", "serial", "game", "mutants"}
    want(most_at_once(lines, every) == 5, f"{most_at_once(lines, every)} steps at once, want 1 + 1 + 2 + 1 in the 4 lanes")


def dry_run(bench, *args, code=0):
    """test_all.py --dry-run in the bench: (its output, {step: (lane, command)}), or its error output when the exit
    code wanted is not 0."""
    done = subprocess.run([sys.executable, str(bench.repo / "tools" / "coop" / "test_all.py"), "--dry-run", *map(str, args)],
                          cwd=bench.repo, env=ENV, capture_output=True, text=True, timeout=60)
    want(done.returncode == code, f"--dry-run {args} exit code {done.returncode}: {done.stdout!r} {done.stderr!r}")
    if code:
        return done.stderr, {}
    plan = {}
    for line in done.stdout.splitlines():
        lane, _, rest = line.partition(": ")
        name, _, command = rest.partition(": ")
        if lane in ("pure", "serial", "game", "mutants", "base"):
            plan[name] = (lane, command)
    return done.stdout, plan


def scenario_dry_run(bench):
    """What each step would run (#290): the lanes, --no-build for the tools whose programs were built once, and the old
    single lane of --mutants-in-tree."""
    output, plan = dry_run(bench)
    for name, lane in (("checks", "pure"), ("net", "pure"), ("menu-asan", "pure"), ("menu", "serial"),
                       ("lockstep", "serial"), ("join", "serial"), ("rates-rails", "game"),
                       ("session-mutants", "mutants")):
        want(plan.get(name, (None,))[0] == lane, f"{name} is in lane {plan.get(name, (None,))[0]}, want {lane}")
    for name in ("net", "session", "menu", "input", "lockstep", "net-asan"):
        want("--no-build" in plan[name][1], f"{name} is not given --no-build: {plan[name][1]!r}")
    for name in ("join", "version", "session-mutants", "net-mutants-asan", "rates-rails"):
        want("--no-build" not in plan[name][1], f"{name} is given --no-build: {plan[name][1]!r}")
    programs = [line for line in output.splitlines() if line.startswith("programs: ")]
    want(len(programs) == 1, f"{len(programs)} lines for the programs: {output!r}")
    for target in ("coop_net_test", "coop_session_test", "coop_menu_test", "coop_menu_test_asan", "coop_input_test",
                   "coop_lockstep_test", "coop_desync_test"):
        want(re.search(rf"\b{target}\b", programs[0]), f"{target} is not built up front: {programs[0]!r}")
    want("mutation" not in programs[0], "the mutation suites' programs are built up front")
    game = [name for name, (lane, _) in plan.items() if lane == "game"]
    # (builds is not planned here: the bench has no Release build)
    want(game[:4] == ["rates-rails", "rates-all-range", "determinism", "exit-test"] and game[4] == "view-check",
         f"exit-test does not come right after the longest game steps: {game[:6]}")
    want("exit_test.py --jobs 4" in plan["exit-test"][1], f"exit-test plays one game at a time: {plan['exit-test'][1]!r}")
    only_output, only_plan = dry_run(bench, "--only", "rates-rails,join")
    want(list(only_plan) == ["join", "rates-rails"] and "programs:" not in only_output,
         f"--only rates-rails,join plans {list(only_plan)}: {only_output!r}")

    _, single = dry_run(bench, "--mutants-in-tree")
    want({lane for lane, _ in single.values()} == {"serial", "game"}, "--mutants-in-tree has other lanes than serial and game")
    serial = [name for name, (lane, _) in single.items() if lane == "serial"]
    want(serial[0] == "checks" and serial[-1] == "steam-mutants" and "mutants-setup" not in single,
         f"the one lane is {serial}")
    want(serial.index("menu") < serial.index("net-asan") < serial.index("session-mutants"), f"the one lane is {serial}")


def scenario_dry_run_mixed(bench):
    """--mixed (#290): every step that starts the game plays it on the Release build, except the ones that need Debug,
    and without a Release build of the same commit those are not run."""
    output, _ = dry_run(bench, "--mixed")
    for name in ("rates-rails", "hash-run-1", "replay-test", "menu", "input", "join", "lockstep", "lockstep-full"):
        want(f"NOT RUN {name}: no Release build" in output, f"{name} is not reported as not run: {output!r}")
    for name in ("wingman", "exit-test", "net", "version"):  # builds needs it anyway: it plays both
        want(f"NOT RUN {name}" not in output, f"{name} needs the Release build: {output!r}")
    release = bench.repo / "build-release"
    release.mkdir()
    (release / "Starship").write_text("", encoding="utf-8")  # the folders carry no build id, so they are of one commit
    _, plan = dry_run(bench, "--mixed")
    for name in ("rates-rails", "hash-run-1", "replay-test", "full-level", "campaign-4", "rails-4", "hash-probe-1", "menu", "input",
                 "join", "lockstep", "lockstep-full"):
        want("--exe " in plan[name][1] and f"{release}/Starship" in plan[name][1],
             f"{name} does not play on Release: {plan[name][1]!r}")
    for name in ("menu", "input", "lockstep"):  # the test programs of these come from Debug's folder
        want("--build-dir " + str(bench.repo / "build-cmake") in plan[name][1] and "--config Debug" in plan[name][1],
             f"{name} does not take Debug's test programs: {plan[name][1]!r}")
    for name in ("wingman", "exit-test", "version", "net"):
        want("build-release" not in plan[name][1], f"{name} plays on Release: {plan[name][1]!r}")
    want("build-release" in plan["builds"][1] and "build-cmake" in plan["builds"][1], "builds does not take both")
    # Debug's build, then Release's without the repository checks: they ran in Debug's (#290)
    builds = [line.split(": ", 2)[1:] for line in output.splitlines() if line.startswith("build: ")]
    want([name for name, _ in builds] == ["build", "build-release"], f"--mixed builds: {builds!r}")
    want(builds[0][1].endswith("--build-dir " + str(bench.repo / "build-cmake")) and
         builds[1][1].endswith("--build-dir " + str(release) + " --no-check"), f"--mixed runs check.py twice: {builds!r}")
    output, _ = dry_run(bench)
    builds = [line for line in output.splitlines() if line.startswith("build: ")]
    want(len(builds) == 1 and builds[0].endswith(" --config Debug --build-dir " + str(bench.repo / "build-cmake")),
         f"Debug alone builds otherwise: {builds!r}")
    done = subprocess.run([sys.executable, str(bench.repo / "tools" / "coop" / "test_all.py"), "--dry-run", "--mixed",
                           "--config", "Release"], cwd=bench.repo, env=ENV, capture_output=True, text=True, timeout=60)
    want(done.returncode == 2 and "use it with --config Debug" in done.stderr, f"--mixed --config Release: {done.stderr!r}")


def scenario_mutants_beside(bench):
    """The mutation suites are a lane of their own (#290): one at a time, and beside the serial and the game lanes."""
    times = bench.work / "times"
    times.mkdir()
    names = [("serial-0", "serial"), ("game-0", "game"), ("mutants-setup", "mutants"), ("mut-1", "mutants"),
             ("mut-2", "mutants")]
    plan = [step(name, lane, TIMED, times / f"{name}.txt", lane) for name, lane in names]
    code, output = bench.run(plan, "--lanes", 1)
    want_result(code, output, 0, "PASS, 6/6 steps ok")
    lines = [(times / f"{name}.txt").read_text(encoding="utf-8").split() for name, _ in names]
    want(all(len(fields) == 3 for fields in lines), f"a step did not note its times: {lines!r}")
    spans = {lane: [(float(a), float(b)) for ln, a, b in lines if ln == lane] for lane in ("serial", "game", "mutants")}
    mutants = sorted(spans["mutants"])
    want(len(mutants) == 3, f"{len(mutants)} mutation steps noted their times")
    want(all(mutants[i][1] <= mutants[i + 1][0] for i in range(2)), "two mutation steps ran at once")
    want(mutants[0][0] < spans["serial"][0][1] and mutants[0][0] < spans["game"][0][1],
         "the mutation lane did not start beside the serial and the game lanes")


def scenario_mutants_setup_fails(bench):
    """When the second checkout cannot be made, the mutation steps fail one by one and say why."""
    plan = [step("mutants-setup", "mutants", BAD), step("net-mutants", "mutants", OK),
            step("session-mutants", "mutants", OK), step("fine", "game", OK)]
    code, output = bench.run(plan)
    want_result(code, output, 1, "FAILED, 2/5 steps ok")
    for name in ("mutants-setup", "net-mutants", "session-mutants"):
        want(re.search(rf"^FAILED {name} ", output, re.MULTILINE), f"{name} not named: {output!r}")
    want("mutants-setup failed" in output, f"no reason given: {output!r}")


def scenario_dry_run_base(bench):
    """--base-ref (#290): coop-off gets a build of that commit, made in a lane of its own first; --base-exe keeps the
    old way, and a ref that names no commit stops the run before it builds."""
    output, plan = dry_run(bench)
    want("NOT RUN coop-off: no --base-exe or --base-ref" in output and "base-setup" not in plan, f"no base: {output!r}")
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=bench.repo, env=ENV, capture_output=True, text=True).stdout
    _, plan = dry_run(bench, "--base-ref", "HEAD")
    tree = bench.repo / "local" / "base-tree"
    lanes = [lane for lane, _ in plan.values()]
    want(list(plan).index("base-setup") + 1 == list(plan).index("coop-off") and lanes.count("base") == 2,
         f"the base lane is not base-setup then coop-off: {plan!r}")
    setup = plan["base-setup"][1]
    for part in ("mutation_tree.py make", f"--path {tree}", f"--ref {head.strip()}", "--build", "--jobs "):
        want(part in setup, f"base-setup lacks {part!r}: {setup!r}")
    want(f"--base {tree}/build-cmake/Starship" in plan["coop-off"][1], f"coop-off plays another base: {plan['coop-off']!r}")
    _, plan = dry_run(bench, "--base-exe", "base/Starship")
    want(plan["coop-off"][0] == "game" and "base-setup" not in plan, f"--base-exe: {plan.get('coop-off')!r}")
    error, _ = dry_run(bench, "--base-ref", "no-such-ref", code=2)
    want("no such commit" in error, f"--base-ref no-such-ref: {error!r}")
    error, _ = dry_run(bench, "--base-ref", "HEAD", "--base-exe", "base/Starship", code=2)
    want("not both" in error, f"--base-ref with --base-exe: {error!r}")


def base_setup(bench):
    """A made-up base-setup step: a real worktree where test_all.py --base-ref puts its checkout of the base."""
    tree = bench.repo / "local" / "base-tree"
    program = "import subprocess, sys; subprocess.run(['git', 'worktree', 'add', '-q', '--detach', sys.argv[1], 'HEAD'], check=True)"
    return tree, step("base-setup", "base", program, tree)


def worktrees(bench):
    done = subprocess.run(["git", "worktree", "list", "--porcelain"], cwd=bench.repo, env=ENV, capture_output=True, text=True)
    return [line for line in done.stdout.splitlines() if line.startswith("worktree ")]


def scenario_base_tree(bench):
    """The checkout of --base-ref goes at the end of the run, and when it cannot be built, coop-off fails and says why."""
    tree, setup = base_setup(bench)
    code, output = bench.run([setup, step("coop-off", "base", f"import os; assert os.path.isdir({str(tree)!r})"),
                              step("fine", "game", OK)])
    want_result(code, output, 0, "PASS, 4/4 steps ok")
    want(not tree.exists() and len(worktrees(bench)) == 1, f"the base tree is left: {worktrees(bench)!r}")
    code, output = bench.run([step("base-setup", "base", BAD), step("coop-off", "base", OK)])
    want_result(code, output, 1, "FAILED, 1/3 steps ok")
    want(re.search(r"^FAILED coop-off \(0s\): base-setup failed$", output, re.MULTILINE), f"no reason: {output!r}")


def scenario_stop_base(bench):
    """A run stopped while coop-off plays on the base removes the base's checkout, and what the step started."""
    tree, setup = base_setup(bench)
    pid_file = bench.work / "child.txt"
    proc = bench.start([setup, step("coop-off", "base", PARENT, pid_file)])
    end = time.monotonic() + 10
    while time.monotonic() < end and not (pid_file.exists() and pid_file.read_text()):
        time.sleep(0.05)
    want(tree.is_dir(), "the base tree was not made")
    proc.send_signal(signal.SIGTERM)
    output = proc.communicate(timeout=60)[0]
    want_result(proc.returncode, output, 1, "FAILED")
    want(wait_gone(int(pid_file.read_text())), "what coop-off started is still running")
    want(not tree.exists() and len(worktrees(bench)) == 1, f"the stop left the base tree: {worktrees(bench)!r}")


def scenario_timeout(bench):
    pid_file = bench.work / "child.txt"
    plan = [step("stuck", "game", PARENT, pid_file), step("fine", "game", OK)]
    code, output = bench.run(plan, "--timeout", 1)
    want_result(code, output, 1, "FAILED, 2/3 steps ok")
    want(re.search(r"^FAILED stuck ", output, re.MULTILINE), f"stuck not named: {output!r}")
    summary = (bench.folder() / "SUMMARY.txt").read_text(encoding="utf-8")
    want("rc=timeout" in summary, "SUMMARY.txt does not say it timed out")
    want(wait_gone(int(pid_file.read_text())), "what the stuck step started is still running")


def scenario_stop(bench):
    pid_file = bench.work / "child.txt"
    plan = [step("running", "game", PARENT, pid_file), step("later", "game", OK)]
    proc = bench.start(plan, "--lanes", 1)
    end = time.monotonic() + HELD
    while time.monotonic() < end and not (pid_file.exists() and pid_file.read_text()):
        time.sleep(0.05)
    want(pid_file.exists(), "the step did not start")
    proc.send_signal(signal.SIGTERM)
    output = proc.communicate(timeout=60)[0]
    want_result(proc.returncode, output, 1, "FAILED")
    want("stopped before the end" in output, f"no word of the stop: {output!r}")
    want(wait_gone(int(pid_file.read_text())), "what the running step started is still running")
    want(not (bench.folder() / "later.log").exists(), "a step started after the stop")


def scenario_stop_mutants(bench):
    """A run stopped during a mutation check must not leave a break in the source: test_all.py runs them all."""
    folder = bench.work / "mutants"
    folder.mkdir()
    source = folder / "source.txt"
    source.write_text("text", encoding="utf-8")
    proc = bench.start([step("mutants", "serial", MUTATION, TOOLS, folder)])
    end = time.monotonic() + 10
    while time.monotonic() < end and source.read_text(encoding="utf-8") != "broken":
        time.sleep(0.05)
    want(source.read_text(encoding="utf-8") == "broken", "the mutation check did not get to its break")
    proc.send_signal(signal.SIGTERM)
    output = proc.communicate(timeout=60)[0]
    want_result(proc.returncode, output, 1, "FAILED")
    want(source.read_text(encoding="utf-8") == "text", "the stop left the break in the source")
    want(not (folder / "source.txt.orig").exists(), "the stop left the backup of the source")


def scenario_tree(bench):
    plan = [step("writes", "game", f"open({str(bench.tracked)!r}, 'a').write('changed\\n')")]
    code, output = bench.run(plan)
    want_result(code, output, 1, "FAILED, 1/2 steps ok")
    want(re.search(r"^FAILED tree ", output, re.MULTILINE), f"tree not named: {output!r}")


def wait_for(path):
    end = time.monotonic() + 30
    while time.monotonic() < end and not path.exists():
        time.sleep(0.05)
    return path.exists()


def scenario_beside(bench):
    """Two runs in one checkout (#283): the one that ends first deletes the run folders its own steps left in local/,
    and not the other run's folder or the folders of its steps, which are still in use."""
    local = bench.repo / "local"
    tools = bench.repo / "tools" / "coop"
    a_made, b_made, a_ended = bench.work / "a-made", bench.work / "b-made", bench.work / "a-ended"
    first = bench.start([step("makes-a", "game", MAKES, tools, local, "test-step-a-", a_made, b_made)])
    want(wait_for(a_made), "the first run's step made no folder")
    second = bench.start([step("makes-b", "game", MAKES, tools, local, "test-step-b-", b_made, a_ended)])
    output = first.communicate(timeout=60)[0]
    a_ended.write_text("")
    want_result(first.returncode, output, 0, "PASS, 2/2 steps ok")
    want(not (local / a_made.read_text()).exists(), "the first run kept the folder its step made")
    runs = sorted(local.glob("test-all-*"))
    want(len(runs) == 2, f"run folders after the first run ended: {[p.name for p in runs]}")
    want((local / b_made.read_text()).exists(), "the first run deleted the folder of the second run's step")
    output = second.communicate(timeout=60)[0]
    want_result(second.returncode, output, 0, "PASS, 2/2 steps ok")
    want(not (local / b_made.read_text()).exists(), "the second run kept the folder its step made")
    want(len(list(local.glob("test-all-*"))) == 2, "a run deleted a run folder of test_all")


def scenario_by_hand(bench):
    """A tool started by hand beside a run in one checkout (#317): the folders it makes after the run started are not
    the run's steps', and a run that passes leaves them. What the run's own steps left goes, the folder of a tool a step
    started that crashed too."""
    local = bench.repo / "local"
    tools = bench.repo / "tools" / "coop"
    step_made, hand_made, crashed, go = (bench.work / name for name in ("step-made", "hand-made", "crashed", "go"))
    run = bench.start([step("makes", "game", MAKES, tools, local, "test-step-", step_made, go),
                       step("crashes", "game", CRASHES, tools, local, "test-crashed-", crashed)])
    want(wait_for(step_made), "the run's step made no folder")
    # as a person starts a tool: from the checkout, with no run above it (ENV has no OWNED_ENV)
    subprocess.run([sys.executable, "-c", MAKES, tools, local, "test-by-hand-", hand_made, "-"], cwd=bench.repo,
                   env=ENV, check=True, timeout=60, capture_output=True)
    go.write_text("")
    output = run.communicate(timeout=60)[0]
    want_result(run.returncode, output, 0, "PASS, 3/3 steps ok")
    want(not (local / step_made.read_text()).exists(), "the run kept the folder its step made")
    want(not (local / crashed.read_text()).exists(), "the run kept the folder of a tool of its step that crashed")
    want((local / hand_made.read_text()).exists(), "the run deleted the folder of a tool started by hand beside it")


SCENARIOS = {
    "pass": scenario_pass,
    "fail": scenario_fail,
    "not-run": scenario_not_run,
    "failed-not-run": scenario_failed_not_run,
    "lanes": scenario_lanes,
    "pure-beside": scenario_pure_beside,
    "dry-run": scenario_dry_run,
    "dry-run-mixed": scenario_dry_run_mixed,
    "mutants-beside": scenario_mutants_beside,
    "mutants-setup-fails": scenario_mutants_setup_fails,
    "dry-run-base": scenario_dry_run_base,
    "base-tree": scenario_base_tree,
    "stop-base": scenario_stop_base,
    "timeout": scenario_timeout,
    "stop": scenario_stop,
    "stop-mutants": scenario_stop_mutants,
    "tree": scenario_tree,
    "beside": scenario_beside,
    "by-hand": scenario_by_hand,
}
if not sys.platform.startswith("linux"):  # these read /proc, or need a stop signal a process can catch, or plan Linux's steps
    del SCENARIOS["timeout"], SCENARIOS["stop"], SCENARIOS["stop-mutants"], SCENARIOS["dry-run"], SCENARIOS["dry-run-mixed"]
    del SCENARIOS["dry-run-base"], SCENARIOS["stop-base"]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--only", help="comma-separated scenarios (default: all)")
    p.add_argument("--list", action="store_true", help="print the scenarios and stop")
    args = p.parse_args()
    if args.list:
        print("\n".join(SCENARIOS))
        return 0
    names = args.only.split(",") if args.only else list(SCENARIOS)
    unknown = [name for name in names if name not in SCENARIOS]
    if unknown:
        print(f"unknown scenario: {', '.join(unknown)}")
        return 1

    (ROOT / "local").mkdir(exist_ok=True)
    base = Path(tempfile.mkdtemp(prefix="test-testall-", dir=ROOT / "local"))
    failed = 0
    for name in names:
        work = base / name
        work.mkdir()
        try:
            SCENARIOS[name](Bench(work))
            print(f"ok    {name}")
        except Exception as failure:  # a crash in one scenario must not hide the others
            failed += 1
            print(f"FAIL  {name}: {failure}" if isinstance(failure, Failed) else f"FAIL  {name}: {failure!r}")
    if failed:
        print(f"test_all_test: FAIL ({failed} of {len(names)}), folders in {base.relative_to(ROOT)}")
        return 1
    shutil.rmtree(base, ignore_errors=True)
    print(f"test_all_test: PASS ({len(names)} scenarios)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
