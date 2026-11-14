#!/usr/bin/env python3
"""Tests of hash_check.py that need no game (#307).

    python tools/coop/hash_check_test.py                 # unattended; prints PASS or FAIL
    python tools/coop/hash_check_test.py --only pilot-default
    python tools/coop/hash_check_test.py --list

pilot-default: `views --pilot` without --level flies the levels of its default list and lists the others as not
covered, with the reason. Level 4 is one of those since the owner's decision of 2026-11-13 (#307), as level 15 and
Training are: no menu offers it, and two clients differ from its first frame. --level level-4 still flies it. The
scenario runs cmd_views with the pilot's game replaced: the levels it asks the pilot to fly are the levels it would fly.

No build, no ROM, under a second; check.py runs it. Exit code 0 when every scenario holds, 1 otherwise.
"""
import argparse
import contextlib
import io
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hash_check  # noqa: E402


def views_pilot(level, work):
    """cmd_views --pilot [--level level] with no game: (the levels the pilot was asked to fly, the printed output)."""
    flown = []

    def pilot_flight(name, *args):
        flown.append(name)
        return None, 0, None, "stand-in", None  # flew nothing: view_pilot ends there

    saved = (hash_check.pilot_flight, hash_check.make_run_folder, hash_check.replay_tool.find_exe)
    hash_check.pilot_flight = pilot_flight
    hash_check.make_run_folder = lambda parent, prefix="": work  # no run folder in local/
    hash_check.replay_tool.find_exe = lambda exe=None: work / "no-game"
    args = argparse.Namespace(pilot=True, level=level, players=2, cvar=[], mortal=False, exe=None, frames=None,
                              timeout=60, skew=hash_check.VIEW_SYS_FRAME_SKEW, audio="split", section="both")
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out):
            hash_check.cmd_views(args)
    finally:
        hash_check.pilot_flight, hash_check.make_run_folder, hash_check.replay_tool.find_exe = saved
    return flown, out.getvalue()


def summary_row(output, name):
    """The summary line of one level, or ''."""
    return next((line for line in output.splitlines() if line.split()[1:2] == [name]), "")


def check_pilot_default(work):
    failures = []
    flown, output = views_pilot(None, work)
    if "corneria" not in flown:
        failures.append(f"the default list flew no Corneria: {flown}")
    for name in ("level-4", "level-15", "training"):
        if name in flown:
            failures.append(f"the default list flies {name}")
        if "not covered" not in summary_row(output, name):
            failures.append(f"the summary does not list {name} as not covered: {summary_row(output, name)!r}")
    flown, output = views_pilot("level-4", work)
    if flown != ["level-4"]:
        failures.append(f"--level level-4 flew {flown}, not level 4")
    flown, output = views_pilot("level-15", work)
    if flown or "not covered" not in summary_row(output, "level-15"):
        failures.append(f"--level level-15 flew {flown}, or was not listed as not covered")
    return failures


SCENARIOS = {"pilot-default": check_pilot_default}


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
        sys.exit(f"hash_check_test: unknown scenario(s) {', '.join(unknown)}; known: {', '.join(SCENARIOS)}")
    failed = 0
    local = hash_check.replay_tool.ROOT / "local"
    local.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="test-hashcheck-", dir=local))  # in local/, as every test's: never /tmp
    try:
        for name in chosen:
            failures = SCENARIOS[name](work)
            failed += bool(failures)
            print(f"hash_check_test: {name}: " + (f"FAIL: {'; '.join(failures)}" if failures else "ok"), flush=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("hash_check_test: FAIL" if failed else "hash_check_test: PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
