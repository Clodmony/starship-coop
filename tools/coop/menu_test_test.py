#!/usr/bin/env python3
"""Tests of tools/coop/menu_test.py's log folders (#293): every scenario that starts games keeps their logs in a folder
of its own, so a later scenario's game-host.log does not overwrite the one of a scenario that failed.

No build, no game, no ROM; check.py runs it. Exit code 0 when every check holds.
"""
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import menu_test  # noqa: E402


def main():
    checks = 0
    failures = []

    def check(cond, what):
        nonlocal checks
        checks += 1
        if not cond:
            failures.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)
        folders = {name: menu_test.scenario_out(out, name) for name in menu_test.SCENARIOS}
        games = [folders[name] for name in menu_test.GAME_SCENARIOS]
        check(len(set(games)) == len(games), "two game scenarios share a log folder")
        for name in menu_test.GAME_SCENARIOS:
            check(folders[name] == out / name, f"{name}: logs in {folders[name]}, want <out>/{name}")
            check(folders[name].is_dir(), f"{name}: its log folder was not made")
        for name in ("sim", "udp"):
            check(folders[name] == out, f"{name}: a scenario without games moved its log to {folders[name]}")
        # Two games of one name in two scenarios: the first one's log is still there after the second wrote its own.
        first = menu_test.scenario_out(out, "game_leave") / "game-host.log"
        first.write_text("leave\n", encoding="utf-8")
        (menu_test.scenario_out(out, "game_names") / "game-host.log").write_text("names\n", encoding="utf-8")
        check(first.read_text(encoding="utf-8") == "leave\n", "a later scenario overwrote game_leave's game-host.log")

    if failures:
        print("menu_test_test: FAILED\n  " + "\n  ".join(failures))
        return 1
    print(f"menu_test_test: ok, {checks} checks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
