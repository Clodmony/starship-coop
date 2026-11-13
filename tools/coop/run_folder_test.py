#!/usr/bin/env python3
"""Tests that a run of a test tool gets a folder of its own, however close together runs start (#69, #104).

    python tools/coop/run_folder_test.py                  # unattended; prints PASS or FAIL
    python tools/coop/run_folder_test.py --only net,exit
    python tools/coop/run_folder_test.py --list

Each scenario starts the real tool as its own process, RUNS at a time, with time.strftime stopped at one stamp, so the
runs share a second however long the machine takes to start them. The tool only has to get as far as making its folder:
there is no game (--exe names a file that does not exist) and no test program (--build-dir is empty), so the runs end
there with an error. RUNS runs must leave RUNS folders. helper does the same with make_run_folder itself, in several
processes of several threads, all let go at one moment under a parent that does not exist yet. mutants has a lock, so
its runs go one after the other, on a table of its own in the test's folder: the real tools would restore a source file
a killed run left behind, and check.py must not write. Without cmake it is skipped, as the tool stops first.

The out-* scenarios are the other side: --out names a folder that runs may share, and it stays as it is.

owned and clean-crash are about the end of a run (#308). owned has a process note what the tools it starts make
(OwnedFolders): a folder two tools down is noted, at both levels, and delete() removes it. clean-crash runs
crash_test.py as test_all.py runs a step, with OWNED_ENV set, and every folder noted there must be gone when it passes:
the folders of the stand-in's runs too, which the tools it starts make.

No build, no ROM, a few seconds; check.py runs it. Exit code 0 when every scenario holds, 1 otherwise. Folders are named
after the stopped clock and deleted when all scenarios hold, and kept for reading when one fails.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "coop"
LOCAL = ROOT / "local"
RUNS = 4  # at once in a scenario
THREADS = 4  # per process in helper
GATHER = 0.5  # seconds for helper's processes to start before they are let go

sys.path.insert(0, str(TOOLS))
from run_folder import OWNED_ENV  # noqa: E402

# As mutants.py starts a test script, with -I: the tool has to find its own imports.
TOOL = """
import runpy, sys, time
stamp, tool, *rest = sys.argv[1:]
time.strftime = lambda *args: stamp
sys.argv = [tool, *rest]
runpy.run_path(tool, run_name="__main__")
"""

HELPER = """
import sys, threading, time
from pathlib import Path
stamp, tools, parent, threads, go = sys.argv[1:]
sys.path.insert(0, tools)
from run_folder import make_run_folder
time.strftime = lambda *args: stamp
def make():
    time.sleep(max(0, float(go) - time.time()))
    make_run_folder(Path(parent), "run-")
workers = [threading.Thread(target=make) for _ in range(int(threads))]
for worker in workers:
    worker.start()
for worker in workers:
    worker.join()
"""

# mutants.py on a table of its own: FOLDER and LOCK in the test's folder, a source nothing else reads.
MUTANTS = """
import sys, time
from pathlib import Path
stamp, tools, folder, *rest = sys.argv[1:]
sys.path.insert(0, tools)
time.strftime = lambda *args: stamp
import mutants
mutants.FOLDER = Path(folder)
mutants.LOCK = mutants.FOLDER / "lock"
source = mutants.FOLDER / "source.txt"
source.write_text("text")
table = mutants.Table(source=source, test_script=source, scenarios={"one": None}, target="no-such-target",
                      find_exe=lambda *args: None, breaks=[("break", "text", "broken", ["one"])])
sys.argv = ["mutants.py", *rest]
sys.exit(mutants.main("run_folder_test", lambda args: table))
"""


class Bench:
    """The test's own folder under local/, what the tools need to get going, and the stopped clock."""

    def __init__(self):
        LOCAL.mkdir(exist_ok=True)
        self.folder = Path(tempfile.mkdtemp(prefix="test-runfolder-", dir=LOCAL))
        self.key = self.folder.name.removeprefix("test-")
        self.stamp = self.key  # main gives each scenario its own, so none sees the folders of another
        self.exe = self.folder / "no-game"
        self.empty = self.folder / "empty"
        self.empty.mkdir()
        self.replay = self.folder / "any.sfcr"
        self.replay.touch()
        for name in ("sf64.o2r", "starship.o2r", "gamecontrollerdb.txt"):
            (self.folder / name).touch()

    def start(self, log, *command):
        with open(self.folder / f"{log}.log", "a", encoding="utf-8") as out:
            return subprocess.Popen([sys.executable, "-I", "-c", *map(str, command)], cwd=ROOT, stdout=out,
                                    stderr=subprocess.STDOUT)

    def tools_at_once(self, log, command):
        procs = [self.start(f"{log}-{i}", TOOL, self.stamp, TOOLS / command[0], *command[1:]) for i in range(RUNS)]
        for proc in procs:
            proc.wait()

    def made(self, parent, prefix=""):
        return sorted(parent.glob(f"{prefix}{self.stamp}*"))

    def clean(self):
        for parent, pattern in ((LOCAL, "test-*"), (LOCAL / "test-net", ""), (LOCAL / "test-session", "")):
            for path in parent.glob(f"{pattern}{self.key}*"):
                shutil.rmtree(path, ignore_errors=True)
        shutil.rmtree(self.folder, ignore_errors=True)


def count_folders(found, want):
    return [] if len(found) == want else [f"{want} runs made {len(found)} folders"]


def check_helper(bench, name):
    parent = bench.folder / name / "not" / "there" / "yet"
    go = time.time() + GATHER
    procs = [bench.start(f"{name}-{i}", HELPER, bench.stamp, TOOLS, parent, THREADS, go) for i in range(RUNS)]
    for proc in procs:
        proc.wait()
    return count_folders(bench.made(parent, "run-"), RUNS * THREADS)


def tool_scenario(command, parent, prefix=""):
    def check(bench, name):
        bench.tools_at_once(name, command(bench))
        return count_folders(bench.made(parent, prefix), RUNS)
    return check


def run_mutants(bench, name, folder, *args):
    """One mutants.py run on its table, to its end: a run that holds the lock stops the next."""
    folder.mkdir(exist_ok=True)
    bench.start(name, MUTANTS, bench.stamp, TOOLS, folder, "--build-dir", bench.empty, *args).wait()


def check_mutants(bench, name):
    folder = bench.folder / name
    for _ in range(RUNS):
        run_mutants(bench, name, folder)
    return count_folders(bench.made(folder), RUNS)


def check_out(bench, out, stamped):
    """What a tool must leave when --out is given: the folder there, what it held still in it, nothing stamped."""
    failures = [] if (out / "held.txt").exists() else ["--out folder lost what it held"]
    extra = [p.name for p in [*bench.made(bench.folder, "*"), *stamped]]
    extra += [p.name for p in out.iterdir() if p.is_dir()]
    return failures + ([f"--out was given, but these appeared: {', '.join(extra)}"] if extra else [])


def out_folder(bench, name):
    out = bench.folder / name
    out.mkdir()
    (out / "held.txt").touch()
    return out


def out_scenario(tool, default):
    def check(bench, name):
        out = out_folder(bench, name)
        new = bench.folder / f"{name}-new" / "deep"
        bench.tools_at_once(name, [tool, "--no-build", "--build-dir", bench.empty, "--out", out])
        bench.tools_at_once(f"{name}-new", [tool, "--no-build", "--build-dir", bench.empty, "--out", new])
        failures = [] if new.is_dir() else ["--out did not make a folder that was not there"]
        return failures + check_out(bench, out, bench.made(default))
    return check


def check_out_mutants(bench, name):
    folder = bench.folder / f"{name}-tool"
    out = out_folder(bench, name)
    for _ in range(RUNS):
        run_mutants(bench, name, folder, "--out", out)
    return check_out(bench, out, bench.made(folder))


def check_owned(bench, name):
    """A folder that a tool two levels down makes is noted by OwnedFolders and by the run above it; delete() removes
    it, and close() hands OWNED_ENV back as it was."""
    from run_folder import OwnedFolders, make_run_folder  # here: the scenarios before it need neither

    outer = bench.folder / f"{name}-outer"
    saved = os.environ.get(OWNED_ENV)
    os.environ[OWNED_ENV] = str(outer)
    try:
        owned = OwnedFolders(LOCAL)
        prefix = f"test-{bench.stamp}-{name}-"
        child = ("import subprocess, sys\n"
                 f"sys.path.insert(0, {str(TOOLS)!r})\n"
                 "from pathlib import Path\n"
                 "from run_folder import make_run_folder\n"
                 f"make_run_folder(Path({str(LOCAL)!r}), {prefix + 'deep-'!r})\n")
        parent = ("import subprocess, sys\n"
                  f"sys.path.insert(0, {str(TOOLS)!r})\n"
                  "from pathlib import Path\n"
                  "from run_folder import make_run_folder\n"
                  f"make_run_folder(Path({str(LOCAL)!r}), {prefix + 'top-'!r})\n"
                  f"subprocess.run([sys.executable, '-I', '-c', {child!r}], check=True)\n")
        subprocess.run([sys.executable, "-I", "-c", parent], check=True, cwd=ROOT)
        own = make_run_folder(LOCAL, prefix + "self-")
        made = owned.made()
        failures = []
        want = {f"{prefix}top-", f"{prefix}deep-", f"{prefix}self-"}
        got = {re.sub(r"\d{8}-\d{6}(-\d+)?$", "", path.name) for path in made if path.name.startswith(prefix)}
        if got != want:
            failures.append(f"noted {sorted(got)}, want {sorted(want)}")
        noted_above = {note.name for note in outer.glob("*")}
        if not {path.name for path in made} <= noted_above:
            failures.append("the run above did not note them too")
        owned.delete()
        left = [path.name for path in [*made, own] if path.exists()]
        if left:
            failures.append(f"delete() left {', '.join(left)}")
        if owned.notes.exists():
            failures.append("delete() left the notes")
        if os.environ.get(OWNED_ENV) != str(outer):
            failures.append(f"OWNED_ENV is {os.environ.get(OWNED_ENV)!r} after close(), want the run above's")
        return failures
    finally:
        if saved is None:
            os.environ.pop(OWNED_ENV, None)
        else:
            os.environ[OWNED_ENV] = saved


def clean_scenario(command):
    """A tool run as a step of test_all.py: with OWNED_ENV set. It must pass, and leave none of the folders noted."""
    def check(bench, name):
        notes = bench.folder / f"{name}-notes"
        with open(bench.folder / f"{name}.log", "w", encoding="utf-8") as out:
            code = subprocess.run([sys.executable, "-I", str(TOOLS / command[0]), *command[1:]], cwd=ROOT,
                                  env={**os.environ, OWNED_ENV: str(notes)}, stdout=out, stderr=subprocess.STDOUT,
                                  timeout=300).returncode
        noted = sorted(note.name for note in notes.glob("*"))
        left = [name for name in noted if (LOCAL / name).exists()]
        failures = [] if code == 0 else [f"{command[0]} failed, exit code {code}"]
        if not noted:
            failures.append(f"{command[0]} noted no folder: nothing to look at")
        if left:
            failures.append(f"{command[0]} left {len(left)} of the {len(noted)} folders it noted: {', '.join(left)}")
        return failures
    return check


SCENARIOS = {
    "helper": check_helper,
    "replay": tool_scenario(lambda b: ["replay.py", "play", b.replay, "--exe", b.exe], LOCAL, "test-replay-play-"),
    "trace": tool_scenario(lambda b: ["player_trace.py", "run", b.replay, "--players", "2", "--exe", b.exe], LOCAL,
                           "test-trace-2p-"),
    "exit": tool_scenario(lambda b: ["exit_test.py", "--only", "plain", "--repeat", "1", "--exe", b.exe], LOCAL,
                          "test-exit-"),
    "all": tool_scenario(lambda b: ["test_all.py", "--no-build", "--only", "rails-2", "--exe", b.exe], LOCAL,
                         "test-all-"),
    "net": tool_scenario(lambda b: ["net_test.py", "--no-build", "--build-dir", b.empty], LOCAL / "test-net"),
    "session": tool_scenario(lambda b: ["session_test.py", "--no-build", "--build-dir", b.empty],
                             LOCAL / "test-session"),
    "mutants": check_mutants,
    "out-net": out_scenario("net_test.py", LOCAL / "test-net"),
    "out-session": out_scenario("session_test.py", LOCAL / "test-session"),
    "out-mutants": check_out_mutants,
    "owned": check_owned,
    "clean-crash": clean_scenario(["crash_test.py", "--only", "play-crash,hash-crash,trace-stays,record-stays"]),
}
SKIPPED = {
    "exit": None if sys.platform.startswith("linux") else "exit_test.py is Linux only",
    "mutants": None if shutil.which("cmake") else "no cmake",
    "out-mutants": None if shutil.which("cmake") else "no cmake",
}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--only", help="comma-separated scenarios (default: all)")
    p.add_argument("--list", action="store_true", help="print the scenarios and stop")
    args = p.parse_args()

    if args.list:
        print("\n".join(SCENARIOS))
        return 0
    chosen = list(SCENARIOS) if not args.only else [s.strip() for s in args.only.split(",") if s.strip()]
    unknown = [s for s in chosen if s not in SCENARIOS]
    if unknown:
        sys.exit(f"run_folder_test: unknown scenario(s) {', '.join(unknown)}; known: {', '.join(SCENARIOS)}")

    bench = Bench()
    failed = 0
    for name in chosen:
        if SKIPPED.get(name):
            print(f"run_folder_test: {name}: skipped, {SKIPPED[name]}")
            continue
        bench.stamp = f"{bench.key}-{name}"
        began = time.monotonic()
        failures = SCENARIOS[name](bench, name)
        failed += bool(failures)
        print(f"run_folder_test: {name}: " + (f"FAIL: {'; '.join(failures)}" if failures else "ok")
              + f" ({time.monotonic() - began:.1f}s)", flush=True)
    if failed:
        print(f"run_folder_test: logs and folders kept: {bench.folder.relative_to(ROOT).as_posix()}")
        print("run_folder_test: FAIL")
        return 1
    bench.clean()
    print("run_folder_test: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
