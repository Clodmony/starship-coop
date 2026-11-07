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
COPIED = ("test_all.py", "replay.py", "run_folder.py")
MAX_SUMMARY_LINES = 6  # printed by a run of a few failures: this is what a person reads
HELD = 3  # seconds a slow step stays, more than a scenario needs to look at it
ENV = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}

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
# Notes when it started and ended, for the lanes.
TIMED = """
import sys, time
with open(sys.argv[1], 'a') as out:
    out.write(f'{sys.argv[2]} {time.monotonic()} ')
    time.sleep(0.4)
    out.write(f'{time.monotonic()}\\n')
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
    times = bench.work / "times.txt"
    plan = [step(f"serial-{i}", "serial", TIMED, times, "serial") for i in range(3)]
    plan += [step(f"game-{i}", "game", TIMED, times, "game") for i in range(6)]
    code, output = bench.run(plan, "--lanes", 2)
    want_result(code, output, 0, "PASS, 10/10 steps ok")

    def most_at_once(lanes):
        points = []
        for line in times.read_text(encoding="utf-8").splitlines():
            lane, start, end = line.split()
            if lane in lanes:
                points += [(float(start), 1), (float(end), -1)]
        running = peak = 0
        for _, change in sorted(points, key=lambda p: (p[0], p[1])):
            running += change
            peak = max(peak, running)
        return peak

    want(most_at_once({"serial"}) == 1, f"{most_at_once({'serial'})} serial steps ran at once")
    want(most_at_once({"game"}) == 2, f"{most_at_once({'game'})} game steps ran at once in 2 lanes")
    want(most_at_once({"serial", "game"}) == 3, "the serial lane did not run beside the game lanes")


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


SCENARIOS = {
    "pass": scenario_pass,
    "fail": scenario_fail,
    "not-run": scenario_not_run,
    "failed-not-run": scenario_failed_not_run,
    "lanes": scenario_lanes,
    "timeout": scenario_timeout,
    "stop": scenario_stop,
    "stop-mutants": scenario_stop_mutants,
    "tree": scenario_tree,
}
if not sys.platform.startswith("linux"):  # these read /proc, or need a stop signal a process can catch
    del SCENARIOS["timeout"], SCENARIOS["stop"], SCENARIOS["stop-mutants"]


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
