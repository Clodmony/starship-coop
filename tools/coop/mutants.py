"""How net_mutants.py and session_mutants.py run: break a source file one behaviour at a time and require its tests to
fail. Each of those tools holds a table of breaks and calls main() here; this file is not run on its own.

A run goes:

1. Check every pattern: each must occur exactly once in the source. A pattern that no longer matches is an error,
   never a silent pass; update the break when you change the code it guards.
2. Build the test program from the unchanged source and run every named scenario. All must pass, or the run stops:
   a failure caused by a break only means something when the same test passes without it.
3. For each break: write the broken source, build, run its scenarios. "caught" means every named scenario failed.
4. Restore the source and build once more, so the build directory holds an unbroken test program.

The source is changed in place, because that file is what the test program compiles, so a run guards it:

- The original is kept in local/test-mutants/<source name>.orig until it is restored.
- If a run was killed, the next run restores the original first, but only when the source still holds a text a run
  wrote. Anything else is someone's edit: the run stops and leaves both files for you to compare. Until then --check,
  and so check.py and build.py, fails, because the source may still hold a break.
- If the source changes during a run, the run stops and leaves the file as it finds it. Do not edit the source while
  a run is going.
- Only one run at a time in a checkout, of either tool; a second one stops at once.

Prints one line per break and a RESULT line, keeps logs in local/test-mutants/<stamp>/, and exits 0 only when the
unbroken run passed and every break was caught.
"""
import argparse
import os
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, List, Optional

from run_folder import make_run_folder

ROOT = Path(__file__).resolve().parents[2]
FOLDER = ROOT / "local" / "test-mutants"
LOCK = FOLDER / "lock"
IS_WINDOWS = os.name == "nt"


@dataclass
class Table:
    """What one tool checks. A break is (name, text in the source, replacement, scenarios that must fail). The text
    must occur exactly once. Newlines are "\\n" in a break; files checked out with CRLF are handled."""
    source: Path
    test_script: Path
    scenarios: Dict
    target: str
    find_exe: Callable
    breaks: List
    test_args: List = None
    known: Optional[List] = None  # every break of the tool, when this run only takes some of them

    def __post_init__(self):
        self.test_args = self.test_args or []
        self.known = self.known or self.breaks
        self.backup = FOLDER / f"{self.source.name}.orig"

    def relative(self, path):
        return path.relative_to(ROOT).as_posix()


class Stop(Exception):
    """Ends the run with a RESULT line: a table error, a broken baseline, a build that did not build."""


def read_source(table):
    return table.source.read_bytes().decode("utf-8")


def write_source(table, text):
    table.source.write_bytes(text.encode("utf-8"))


def apply(table, text, find, replace):
    """The source with one break applied. Raises Stop unless the pattern occurs exactly once."""
    if "\r\n" in text:
        find, replace = find.replace("\n", "\r\n"), replace.replace("\n", "\r\n")
    count = text.count(find)
    if count != 1:
        raise Stop(f"pattern found {count} times in {table.relative(table.source)}, expected once")
    return text.replace(find, replace)


def check_table(table, rows, names, text):
    """Every break: known scenarios, a pattern that matches exactly once in text. Returns a list of problems."""
    problems = []
    seen = set()
    for name, find, replace, tests in rows:
        if name not in names:
            continue
        if name in seen:
            problems.append(f"{name}: listed twice")
        seen.add(name)
        unknown = [t for t in tests if t not in table.scenarios]
        if unknown or not tests:
            problems.append(f"{name}: unknown or no scenarios: {', '.join(unknown) or '(none)'}")
        if find == replace:
            problems.append(f"{name}: the replacement is the same as the pattern")
        try:
            apply(table, text, find, replace)
        except Stop as e:
            problems.append(f"{name}: {e}")
    return problems


class Runner:
    def __init__(self, table, args, out):
        self.table = table
        self.args = args
        self.out = out
        build_dir = args.build_dir or (Path("build/x64") if IS_WINDOWS else Path("build-cmake"))
        self.build_dir = build_dir if build_dir.is_absolute() else ROOT / build_dir

    def build(self, label, changed=True):
        """Build the test program. After a source change (changed) it must also be newer than before."""
        target = self.table.target
        exe = self.table.find_exe(self.build_dir, self.args.config)
        before = exe.stat().st_mtime_ns if exe else 0
        log = self.out / f"build-{label}.log"
        with open(log, "w", encoding="utf-8") as f:
            code = subprocess.run(["cmake", "--build", str(self.build_dir), "--config", self.args.config, "--target",
                                   target], stdout=f, stderr=subprocess.STDOUT).returncode
        if code != 0:
            return False, f"did not build, see {log.name}"
        exe = self.table.find_exe(self.build_dir, self.args.config)
        if exe is None:
            return False, f"no {target} in {self.build_dir}"
        if changed and exe.stat().st_mtime_ns <= before:
            # A program from an earlier build would test the wrong source and its result would mean nothing.
            return False, f"the build did not produce a new {target}"
        return True, ""

    def scenarios(self, tests, label):
        """Run the test script for the given scenarios. Returns {scenario: "ok" | "FAILED"} and the failure texts."""
        script = self.table.test_script.name
        folder = self.out / label
        cmd = [sys.executable, "-I", str(self.table.test_script), "--no-build", "--only", ",".join(tests),
               "--build-dir", str(self.build_dir), "--config", self.args.config, *self.table.test_args,
               "--out", str(folder)]
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                                  timeout=self.args.timeout)
        except subprocess.TimeoutExpired:
            raise Stop(f"{label}: {script} took longer than {self.args.timeout} s")
        (self.out / f"{label}.txt").write_text(proc.stdout + proc.stderr, encoding="utf-8")
        results, details = {}, {}
        for line in proc.stdout.splitlines():
            for status in ("ok", "FAILED"):
                if line.startswith(status + " "):
                    name, _, rest = line[len(status):].strip().partition(": ")
                    results[name] = status
                    details[name] = rest
        missing = [t for t in tests if t not in results]
        if missing:
            # The test script itself went wrong (bad arguments, no program). That is not a failing test.
            raise Stop(f"{label}: {script} gave no result for {', '.join(missing)}, see {label}.txt")
        return results, details


def take_lock():
    """Hold an OS lock for the whole run, or return None if another run has it. The OS drops it when a run dies."""
    LOCK.parent.mkdir(parents=True, exist_ok=True)
    handle = open(LOCK, "a+")
    try:
        if IS_WINDOWS:
            import msvcrt
            handle.seek(0)
            msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        handle.close()
        return None
    return handle


def texts_a_run_writes(table, original):
    """Every text a run from this original can leave in the source: the original and each break."""
    texts = {original}
    for _, find, replace, _ in table.known:
        try:
            texts.add(apply(table, original, find, replace))
        except Stop:
            pass
    return texts


def restore_leftover(table):
    """Undo a killed run. Only a text the run wrote is replaced; anything else is someone's work and stays."""
    if not table.backup.exists():
        return
    original = table.backup.read_bytes().decode("utf-8")
    source = table.relative(table.source)
    if read_source(table) not in texts_a_run_writes(table, original):
        raise Stop(f"a run did not finish and {source} has changed since, so it was not touched. Compare it with "
                   f"{table.relative(table.backup)}, fix what needs fixing, then delete that backup")
    write_source(table, original)
    table.backup.unlink()
    print(f"restored {source} from a run that did not finish")


def main(description, make_table, add_options=None):
    ap = argparse.ArgumentParser(description=description)
    ap.add_argument("--only", help="comma separated break names")
    ap.add_argument("--skip", default="", help="comma separated break names to leave out")
    ap.add_argument("--list", action="store_true", help="print the breaks and their scenarios, then stop")
    ap.add_argument("--check", action="store_true", help="only check that every pattern matches once; no build")
    ap.add_argument("--build-dir", type=Path, help="default: build/x64 on Windows, build-cmake elsewhere")
    ap.add_argument("--config", default="Debug", choices=["Debug", "Release", "RelWithDebInfo"])
    ap.add_argument("--timeout", type=int, default=900, help="seconds for one call of the test script")
    ap.add_argument("--out", type=Path, help="log folder, default local/test-mutants/<stamp>")
    if add_options:
        add_options(ap)
    args = ap.parse_args()
    table = make_table(args)
    source_name = table.source.name

    if args.list:
        for name, _, _, tests in table.breaks:
            print(f"{name}: {', '.join(tests)}")
        return 0

    if args.check:
        # Never write here: check.py may run while a full run has the source broken. The backup is the real source
        # then. With no run holding the lock, a backup means a killed run, and the source may still hold a break.
        lock = take_lock() if table.backup.exists() else None
        if lock is not None:
            print(f"RESULT: FAILED, a run did not finish and {source_name} may still hold a break; "
                  f"run {Path(sys.argv[0]).name} to restore it")
            return 2
        text = table.backup.read_bytes().decode("utf-8") if table.backup.exists() else read_source(table)
    else:
        lock = take_lock()  # kept until the process ends
        if lock is None:
            print("RESULT: FAILED, another mutation run is going in this checkout")
            return 2
        try:
            restore_leftover(table)
        except Stop as e:
            print(f"RESULT: FAILED, {e}")
            return 2
        text = read_source(table)
    rows = table.known if args.check else table.breaks
    all_names = [b[0] for b in rows]
    names = args.only.split(",") if args.only else all_names
    skipped = [n for n in args.skip.split(",") if n]
    unknown = [n for n in [*names, *skipped] if n not in all_names]
    names = [n for n in names if n not in skipped]
    if unknown:
        print(f"RESULT: FAILED, unknown break {', '.join(unknown)}; --list shows them")
        return 2
    problems = check_table(table, rows, names, text)
    if problems:
        print("\n".join(problems))
        print(f"RESULT: FAILED, {len(problems)} problems in the break table")
        return 2
    if args.check:
        print(f"RESULT: all {len(names)} patterns match {source_name} once")
        return 0
    if shutil.which("cmake") is None:
        print("RESULT: FAILED, cmake not found")
        return 1

    out = args.out or make_run_folder(FOLDER)
    out.mkdir(parents=True, exist_ok=True)
    runner = Runner(table, args, out)
    chosen = [b for b in rows if b[0] in names]
    original = read_source(table)
    expected = original  # what the source must hold; anything else is someone's edit and must not be overwritten
    lines = []
    survived = []
    mutated = False
    problem = None

    def report(line):
        print(line, flush=True)
        lines.append(line)

    # A stop goes through the finally below, as SIGINT does, or the source keeps its break until the next run (#147).
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(1))
    try:
        # The unbroken source first: every scenario a break relies on must pass without it.
        tests = sorted({t for b in chosen for t in b[3]}, key=list(table.scenarios).index)
        started = time.monotonic()
        ok, why = runner.build("unbroken", changed=False)
        if not ok:
            raise Stop(f"the unbroken source {why}")
        results, details = runner.scenarios(tests, "unbroken")
        bad = [t for t in tests if results[t] != "ok"]
        if bad:
            raise Stop(f"without any break these scenarios fail: {', '.join(bad)} ({details[bad[0]]})")
        report(f"ok        unbroken: {', '.join(tests)} pass ({time.monotonic() - started:.1f} s)")

        table.backup.parent.mkdir(parents=True, exist_ok=True)
        table.backup.write_bytes(original.encode("utf-8"))
        for name, find, replace, tests in chosen:
            started = time.monotonic()
            if read_source(table) != expected:
                raise Stop(f"{source_name} changed during the run")
            expected = apply(table, original, find, replace)
            mutated = True
            write_source(table, expected)
            ok, why = runner.build(name)
            if not ok:
                # A break must compile and then fail its test; one that does not build tests nothing.
                survived.append(name)
                report(f"NO BUILD  {name}: {why} ({time.monotonic() - started:.1f} s)")
                continue
            results, details = runner.scenarios(tests, name)
            passed = [t for t in tests if results[t] == "ok"]
            if passed:
                survived.append(name)
                report(f"SURVIVED  {name}: {', '.join(passed)} still pass ({time.monotonic() - started:.1f} s)")
            else:
                report(f"caught    {name}: {', '.join(tests)} failed ({time.monotonic() - started:.1f} s): "
                       f"{details[tests[0]]}")
    except Stop as e:
        report(f"RESULT: FAILED, {e}")
        return 1
    finally:
        if not mutated:
            table.backup.unlink(missing_ok=True)
        elif read_source(table) == expected:
            write_source(table, original)
            table.backup.unlink()
            ok, why = runner.build("restored")
            if not ok:
                problem = f"the restored source {why}; rebuild {table.target} before you trust it"
        else:
            problem = "the source changed during the run"
            report(f"warning: {source_name} changed during the run, so it was left as it is. It may still hold a break: "
                   f"compare it with {table.relative(table.backup)}, then delete that backup")
        (out / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"logs: {out}")

    if problem:
        report(f"RESULT: FAILED, {problem}")
        (out / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        return 1

    caught = len(chosen) - len(survived)
    pairs = sum(len(b[3]) for b in chosen)
    if survived:
        report(f"RESULT: FAILED, {caught}/{len(chosen)} breaks caught; not caught: {', '.join(survived)}")
    else:
        report(f"RESULT: all {len(chosen)} breaks caught ({pairs} break-and-test pairs)")
    (out / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0 if not survived else 1
