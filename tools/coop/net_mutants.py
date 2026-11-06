#!/usr/bin/env python3
"""Mutation check for the co-op transport: break one behaviour of net_core.c, rebuild, require its tests to fail.

    python tools/coop/net_mutants.py                 # every break; about 10 minutes
    python tools/coop/net_mutants.py --only no_resend,no_goodbye
    python tools/coop/net_mutants.py --list
    python tools/coop/net_mutants.py --check         # only check that every pattern still matches; no build

A test suite that passes proves little until you know it fails when the code is wrong. Each break below replaces one
piece of text in src/port/coop/net/net_core.c with a broken variant and names the net_test.py scenarios that must
fail with it. The run goes:

1. Check every pattern: each must occur exactly once in net_core.c. A pattern that no longer matches is an error,
   never a silent pass; update the break when you change the code it guards.
2. Build coop_net_test from the unchanged source and run every named scenario. All must pass, or the run stops:
   a failure caused by a break only means something when the same test passes without it.
3. For each break: write the broken source, build, run its scenarios. "caught" means every named scenario failed.
4. Restore net_core.c and build once more, so the build directory holds an unbroken coop_net_test.

The source is changed in place, because that file is what the coop_net_test target compiles, so a run guards it:

- The original is kept in local/test-mutants/net_core.c.orig until it is restored.
- If a run was killed, the next run restores the original first, but only when the source still holds a text a run
  wrote. Anything else is someone's edit: the run stops and leaves both files for you to compare.
- If the source changes during a run, the run stops and leaves the file as it finds it. Do not edit net_core.c while
  a run is going.
- Only one run at a time; a second one stops at once.

Prints one line per break and a RESULT line, keeps logs in local/test-mutants/<stamp>/, and exits 0 only when the
unbroken run passed and every break was caught.

The breaks come from the check done by hand for #42 (16 breaks, 20 break-and-test pairs). Add one whenever you fix a
bug in net_core.c: the old, broken text as the replacement, and the scenario that found it.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src" / "port" / "coop" / "net" / "net_core.c"
NET_TEST = ROOT / "tools" / "coop" / "net_test.py"
BACKUP = ROOT / "local" / "test-mutants" / "net_core.c.orig"
LOCK = ROOT / "local" / "test-mutants" / "lock"
sys.path.insert(0, str(ROOT / "tools" / "coop"))
import net_test  # noqa: E402  (scenario names and how the test program is found)

# name, text in net_core.c (exactly once), replacement, scenarios that must fail. Newlines are "\n" here; files
# checked out with CRLF are handled.
BREAKS = [
    ("no_checksum",
     "if (Get32(data) != CoopNetCore_Crc32(data + 4, size - 4) || Get16(data + 4) != MAGIC) {",
     "if (Get16(data + 4) != MAGIC) {",
     ["sim", "corrupt"]),
    ("no_resend",
     "if (slot->used && now - slot->sentAt >= slot->rto) {",
     "if (0) {",
     ["sim", "bad_wifi"]),
    ("no_unreliable_dedupe",
     "if (Link_SeenUnreliable(l, p->seq)) {",
     "if (0) {",
     ["sim", "bad_wifi"]),
    ("reliable_as_unreliable",
     "if (p->flags & FLAG_RELIABLE) {",
     "if (0) {",
     ["sim"]),
    ("ack_frees_one_too_many",
     "for (seq = l->txBase; seq != ack; seq++) {",
     "for (seq = l->txBase; seq != (uint16_t) (ack + 1) && seq != l->txNext; seq++) {",
     ["sim"]),
    ("ack_ignores_bits_bound",
     "if ((bits & (1u << i)) && (uint16_t) (seq - l->txBase) < inFlight) {",
     "if (bits & (1u << (i ? i - 1 : 0))) {",
     ["sim"]),
    ("stale_rx_accepted",
     "if (ahead >= WINDOW) {",
     "if (ahead >= WINDOW && ahead < 0x8000) {",
     ["sim"]),
    ("ack_wrap_naive",
     "if (ahead > inFlight) {",
     "if (ack < l->txBase) {",
     ["sim"]),
    ("timeout_clock_wrap",
     "if (now - l->lastHeard > core->timeoutMs) {",
     "if (now > l->lastHeard + core->timeoutMs) {",
     ["sim"]),
    ("no_connect_retry",
     "l->nextConnect = now + CONNECT_RETRY_MS;",
     "l->nextConnect = now + 2000000000u;",
     ["sim"]),
    ("accept_not_resent",
     "// Our accept went missing.\n        l->lastHeard = now;\n"
     "        Link_Write(core, l, COOP_MSG_CONNECT_ACCEPT, 0, 0, NULL, 0);",
     "l->lastHeard = now;",
     ["sim"]),
    ("no_version_check",
     "if (p->version != COOP_PROTOCOL_VERSION) {\n        if (p->msgId == COOP_MSG_CONNECT_REQUEST) {",
     "if (0) {\n        if (p->msgId == COOP_MSG_CONNECT_REQUEST) {",
     ["sim"]),
    ("ack_never_sent",
     "if (l->ackDue) {",
     "if (0) {",
     ["sim"]),
    ("no_timeout",
     "if (now - l->lastHeard > core->timeoutMs) {",
     "if (0) {",
     ["blackout_timeout", "kill_client"]),
    ("no_goodbye",
     "for (i = 0; i < GOODBYE_COPIES; i++) {",
     "for (i = 0; i < 0; i++) {",
     ["clean_close"]),
    ("no_refuse_full",
     "Core_Refuse(core, from, p->connId, REFUSE_FULL);",
     "",
     ["host_full"]),
]


class Stop(Exception):
    """Ends the run with a RESULT line: a table error, a broken baseline, a build that did not build."""


def read_source():
    return SOURCE.read_bytes().decode("utf-8")


def write_source(text):
    SOURCE.write_bytes(text.encode("utf-8"))


def apply(text, find, replace):
    """The source with one break applied. Raises Stop unless the pattern occurs exactly once."""
    if "\r\n" in text:
        find, replace = find.replace("\n", "\r\n"), replace.replace("\n", "\r\n")
    count = text.count(find)
    if count != 1:
        raise Stop(f"pattern found {count} times in {SOURCE.relative_to(ROOT).as_posix()}, expected once")
    return text.replace(find, replace)


def check_table(names, text):
    """Every break: known scenarios, a pattern that matches exactly once in text. Returns a list of problems."""
    problems = []
    seen = set()
    for name, find, replace, tests in BREAKS:
        if name not in names:
            continue
        if name in seen:
            problems.append(f"{name}: listed twice")
        seen.add(name)
        unknown = [t for t in tests if t not in net_test.SCENARIOS]
        if unknown or not tests:
            problems.append(f"{name}: unknown or no scenarios: {', '.join(unknown) or '(none)'}")
        if find == replace:
            problems.append(f"{name}: the replacement is the same as the pattern")
        try:
            apply(text, find, replace)
        except Stop as e:
            problems.append(f"{name}: {e}")
    return problems


class Runner:
    def __init__(self, args, out):
        self.args = args
        self.out = out
        build_dir = args.build_dir or (Path("build/x64") if net_test.IS_WINDOWS else Path("build-cmake"))
        self.build_dir = build_dir if build_dir.is_absolute() else ROOT / build_dir

    def build(self, label, changed=True):
        """Build coop_net_test. After a source change (changed) the program must also be newer than before."""
        exe = net_test.find_exe(self.build_dir, self.args.config)
        before = exe.stat().st_mtime_ns if exe else 0
        log = self.out / f"build-{label}.log"
        with open(log, "w", encoding="utf-8") as f:
            code = subprocess.run(["cmake", "--build", str(self.build_dir), "--config", self.args.config, "--target",
                                   "coop_net_test"], stdout=f, stderr=subprocess.STDOUT).returncode
        if code != 0:
            return False, f"did not build, see {log.name}"
        exe = net_test.find_exe(self.build_dir, self.args.config)
        if exe is None:
            return False, f"no coop_net_test in {self.build_dir}"
        if changed and exe.stat().st_mtime_ns <= before:
            # A program from an earlier build would test the wrong source and its result would mean nothing.
            return False, "the build did not produce a new coop_net_test"
        return True, ""

    def scenarios(self, tests, label):
        """Run net_test.py for the given scenarios. Returns {scenario: "ok" | "FAILED"} and the failure texts."""
        folder = self.out / label
        cmd = [sys.executable, "-I", str(NET_TEST), "--no-build", "--only", ",".join(tests),
               "--build-dir", str(self.build_dir), "--config", self.args.config, "--seeds", str(self.args.seeds),
               "--out", str(folder)]
        proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                              timeout=self.args.timeout)
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
            # net_test.py itself went wrong (bad arguments, no program). That is not a failing test.
            raise Stop(f"{label}: net_test.py gave no result for {', '.join(missing)}, see {label}.txt")
        return results, details


def take_lock():
    """Hold an OS lock for the whole run, or return None if another run has it. The OS drops it when a run dies."""
    LOCK.parent.mkdir(parents=True, exist_ok=True)
    handle = open(LOCK, "a+")
    try:
        if os.name == "nt":
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


def texts_a_run_writes(original):
    """Every text a run from this original can leave in the source: the original and each break."""
    texts = {original}
    for _, find, replace, _ in BREAKS:
        try:
            texts.add(apply(original, find, replace))
        except Stop:
            pass
    return texts


def restore_leftover():
    """Undo a killed run. Only a text the run wrote is replaced; anything else is someone's work and stays."""
    if not BACKUP.exists():
        return
    original = BACKUP.read_bytes().decode("utf-8")
    source = SOURCE.relative_to(ROOT).as_posix()
    if read_source() not in texts_a_run_writes(original):
        raise Stop(f"a run did not finish and {source} has changed since, so it was not touched. Compare it with "
                   f"{BACKUP.relative_to(ROOT).as_posix()}, fix what needs fixing, then delete that backup")
    write_source(original)
    BACKUP.unlink()
    print(f"restored {source} from a run that did not finish")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", help="comma separated break names")
    ap.add_argument("--list", action="store_true", help="print the breaks and their scenarios, then stop")
    ap.add_argument("--check", action="store_true", help="only check that every pattern matches once; no build")
    ap.add_argument("--build-dir", type=Path, help="default: build/x64 on Windows, build-cmake elsewhere")
    ap.add_argument("--config", default="Debug", choices=["Debug", "Release", "RelWithDebInfo"])
    ap.add_argument("--seeds", type=int, default=200, help="seeds for the sim scenario (passed to net_test.py)")
    ap.add_argument("--timeout", type=int, default=900, help="seconds for one net_test.py call")
    ap.add_argument("--out", type=Path, help="log folder, default local/test-mutants/<stamp>")
    args = ap.parse_args()

    if args.list:
        for name, _, _, tests in BREAKS:
            print(f"{name}: {', '.join(tests)}")
        return 0

    if args.check:
        # Never write here: check.py may run while a full run has the source broken. The backup is the real source.
        text = BACKUP.read_bytes().decode("utf-8") if BACKUP.exists() else read_source()
    else:
        lock = take_lock()  # kept until the process ends
        if lock is None:
            print("RESULT: FAILED, another net_mutants.py run is going in this checkout")
            return 2
        try:
            restore_leftover()
        except Stop as e:
            print(f"RESULT: FAILED, {e}")
            return 2
        text = read_source()
    all_names = [b[0] for b in BREAKS]
    names = args.only.split(",") if args.only else all_names
    unknown = [n for n in names if n not in all_names]
    if unknown:
        print(f"RESULT: FAILED, unknown break {', '.join(unknown)}; --list shows them")
        return 2
    problems = check_table(names, text)
    if problems:
        print("\n".join(problems))
        print(f"RESULT: FAILED, {len(problems)} problems in the break table")
        return 2
    if args.check:
        print(f"RESULT: all {len(names)} patterns match net_core.c once")
        return 0
    if shutil.which("cmake") is None:
        print("RESULT: FAILED, cmake not found")
        return 1

    out = args.out or ROOT / "local" / "test-mutants" / time.strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True, exist_ok=True)
    runner = Runner(args, out)
    chosen = [b for b in BREAKS if b[0] in names]
    original = read_source()
    expected = original  # what the source must hold; anything else is someone's edit and must not be overwritten
    lines = []
    survived = []
    mutated = False
    disturbed = False

    def report(line):
        print(line, flush=True)
        lines.append(line)

    try:
        # The unbroken source first: every scenario a break relies on must pass without it.
        tests = sorted({t for b in chosen for t in b[3]}, key=list(net_test.SCENARIOS).index)
        started = time.monotonic()
        ok, why = runner.build("unbroken", changed=False)
        if not ok:
            raise Stop(f"the unbroken source {why}")
        results, details = runner.scenarios(tests, "unbroken")
        bad = [t for t in tests if results[t] != "ok"]
        if bad:
            raise Stop(f"without any break these scenarios fail: {', '.join(bad)} ({details[bad[0]]})")
        report(f"ok        unbroken: {', '.join(tests)} pass ({time.monotonic() - started:.1f} s)")

        BACKUP.parent.mkdir(parents=True, exist_ok=True)
        BACKUP.write_bytes(original.encode("utf-8"))
        for name, find, replace, tests in chosen:
            started = time.monotonic()
            if read_source() != expected:
                raise Stop(f"{SOURCE.name} changed during the run")
            expected = apply(original, find, replace)
            mutated = True
            write_source(expected)
            ok, why = runner.build(name)
            if not ok:
                # A break must compile and then fail its test; one that does not build tests nothing.
                survived.append(name)
                report(f"NO BUILD  {name}: {why} ({time.monotonic() - started:.1f} s)")
                continue
            results, _ = runner.scenarios(tests, name)
            passed = [t for t in tests if results[t] == "ok"]
            if passed:
                survived.append(name)
                report(f"SURVIVED  {name}: {', '.join(passed)} still pass ({time.monotonic() - started:.1f} s)")
            else:
                report(f"caught    {name}: {', '.join(tests)} failed ({time.monotonic() - started:.1f} s)")
    except Stop as e:
        report(f"RESULT: FAILED, {e}")
        return 1
    finally:
        if not mutated:
            BACKUP.unlink(missing_ok=True)
        elif read_source() == expected:
            write_source(original)
            BACKUP.unlink()
            ok, why = runner.build("restored")
            if not ok:
                report(f"warning: the restored source {why}; rebuild coop_net_test before you trust it")
        else:
            disturbed = True
            report(f"warning: {SOURCE.name} changed during the run, so it was left as it is. It may still hold a break: "
                   f"compare it with {BACKUP.relative_to(ROOT).as_posix()}, then delete that backup")
        (out / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"logs: {out}")

    if disturbed:
        report("RESULT: FAILED, the source changed during the run")
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


if __name__ == "__main__":
    sys.exit(main())
