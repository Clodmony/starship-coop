#!/usr/bin/env python3
"""Tests of tools/coop/menu_test.py without a game:

- its log folders (#293): every scenario that starts games keeps their logs in a folder of its own, so a later
  scenario's game-host.log does not overwrite the one of a scenario that failed;
- Game.wait on a session that ended (#306): a wait stops at the first look after the status went to "Not in a session."
  while the scenario expected the session, and says so with the last lines of that game's own co-op log, instead of
  waiting out its time for a widget that a game in no session does not show. A fake status feed and a fake clock
  stand in for the game.

No build, no game, no ROM; check.py runs it. Exit code 0 when every check holds.
"""
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import menu_test  # noqa: E402

IN_SESSION = "In the session at 127.0.0.1:5000 as player 3."
HOSTING = "Hosting on port 5000, 2 of 4 players."
GONE = "Not in a session."
OWN_LOG = ["2026-11-13 20:00:00.000 - - session joined as player 3"] + [
    f"2026-11-13 20:00:{n:02d}.000 {n} 0 lockstep frame {n}" for n in range(1, 30)] + [
    "2026-11-13 20:00:30.000 30 0 session The host did not start the level again in time, so the session is over."]
OTHER_LOG = ["2026-11-13 20:00:30.000 30 0 session a line of player 1's log, streamed to player 3"]


class FakeProc:
    def poll(self):
        return None


class FakeTime:
    """The clock menu_test reads: each sleep moves it on and lets the game write its next block."""

    def __init__(self, game, blocks):
        self.now = 1000.0
        self.slept = 0.0
        self.game = game
        self.blocks = list(blocks)

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds
        self.slept += seconds
        if self.blocks:
            write_block(self.game, self.blocks.pop(0))


def write_block(game, texts, items=()):
    lines = ["frame 1"] + [f"item {item} 1 {item}" for item in items]
    lines += [f"text {key} {value}" for key, value in texts.items()] + ["end"]
    with open(game.state, "a", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def fake_game(folder, name="b"):
    """A Game with no process: its menu-state.txt is written by the test, its run folder holds co-op logs."""
    game = menu_test.Game.__new__(menu_test.Game)
    game.name = name
    game.run = folder / name
    game.run.mkdir()
    game.commands = game.run / "menu-commands.txt"
    game.state = game.run / "menu-state.txt"
    game.state.write_text("", encoding="utf-8")
    game.offset = 0
    game.items = {}
    game.texts = {}
    game.proc = FakeProc()
    return game


def wait_outcome(game, blocks, call):
    """Runs call(game) with the fake clock. Returns (the Fail text or None, the seconds the wait took)."""
    clock = FakeTime(game, blocks)
    real = menu_test.time
    menu_test.time = clock
    try:
        call(game)
        return None, clock.slept
    except menu_test.Fail as e:
        return str(e), clock.slept
    finally:
        menu_test.time = real


def check_folders(check):
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


def check_session_waits(check):
    ended = "The host did not start the level again in time, so the session is over."
    blocks_to_gone = [{"status": IN_SESSION}] * 3 + [{"status": GONE, "message": ended}]
    with tempfile.TemporaryDirectory() as tmp:
        folder = Path(tmp)

        # #306: player 3 is in the session, then the session ends while the scenario waits for its Leave button.
        game = fake_game(folder)
        (game.run / "coop-log-20261113-195959-player3.txt").write_text("\n".join(OWN_LOG) + "\n", encoding="utf-8")
        (game.run / "coop-log-20261113-195959-player1.txt").write_text("\n".join(OTHER_LOG) + "\n", encoding="utf-8")
        write_block(game, {"status": IN_SESSION})
        error, _ = wait_outcome(game, [], lambda g: g.wait_text("status", r" as player 3\."))
        check(error is None, f"a wait for the session's own status failed: {error}")
        error, took = wait_outcome(game, blocks_to_gone, lambda g: g.wait_item("leave"))
        check(error is not None, "a wait for a widget on a game whose session ended passed")
        error = error or ""
        check(took < 1, f"the wait went on {took:.1f} s after the session ended, of its {menu_test.STEP_SECONDS} s")
        check("session ended" in error and "widget leave" in error,
              f"the failure does not say the session ended during the wait for the widget: {error[:200]!r}")
        check(OWN_LOG[-1].split(" session ", 1)[1] in error and OWN_LOG[-2] in error,
              f"the failure lacks the last lines of the game's own co-op log: {error[-400:]!r}")
        check(OWN_LOG[0] not in error, "the failure holds the whole co-op log, not its last lines")
        check(OTHER_LOG[0] not in error, "the failure shows another player's log, not the game's own")

        # A wait for the end itself is no failure.
        game = fake_game(folder, "c")
        write_block(game, {"status": IN_SESSION})
        wait_outcome(game, [], lambda g: g.wait_text("status", r"as player 3"))
        error, _ = wait_outcome(game, blocks_to_gone, lambda g: g.wait_text("status", r"Not in a session\."))
        check(error is None, f"a wait for 'Not in a session.' failed: {error}")

        # The scenario ended it on purpose, by the game's own Leave click or by saying another player's action ends it:
        # a later wait for something else waits as before, here for a message one block after the status.
        for name, end in (("own", lambda g: g.click("leave")), ("said", lambda g: g.session_ends())):
            game = fake_game(folder, name)
            write_block(game, {"status": HOSTING}, items=["leave"])
            wait_outcome(game, [], lambda g: g.wait_text("status", r"Hosting on port"))
            error, _ = wait_outcome(game, [], end)
            check(error is None, f"{name}: ending the session on purpose failed: {error}")
            error, _ = wait_outcome(game, [{"status": GONE}, {"status": GONE, "message": "Player 3 left, so the "
                                                                                       "session is over."}],
                                    lambda g: g.wait_text("message", r"Player 3 left, so the session is over"))
            check(error is None, f"{name}: a wait after an end on purpose failed: {error}")

        # A game that never got into a session (a refused join) is no lost session; a closed window is no end.
        game = fake_game(folder, "odd")
        write_block(game, {"status": "Connecting to 127.0.0.1:5000..."})
        error, _ = wait_outcome(game, [{"status": GONE}, {"status": GONE, "message": "Setting Invincible differs"}],
                                lambda g: g.wait_text("message", "Invincible"))
        check(error is None, f"a refused join read as a lost session: {error}")
        game = fake_game(folder, "closed")
        write_block(game, {"status": IN_SESSION})
        wait_outcome(game, [], lambda g: g.wait_text("status", r"as player 3"))
        error, _ = wait_outcome(game, [{}, {}, {"status": IN_SESSION, "message": "back"}],
                                lambda g: g.wait_text("message", "back"))
        check(error is None, f"a closed window read as a lost session: {error}")

        # Without a co-op log the failure still comes at once and says there is none.
        game = fake_game(folder, "nolog")
        write_block(game, {"status": IN_SESSION})
        wait_outcome(game, [], lambda g: g.wait_text("status", r"as player 3"))
        error, took = wait_outcome(game, blocks_to_gone, lambda g: g.wait_item("leave"))
        check(error is not None and took < 1 and "no co-op log" in error,
              f"a game without a co-op log: {took:.1f} s, {error!r}")


def main():
    checks = 0
    failures = []

    def check(cond, what):
        nonlocal checks
        checks += 1
        if not cond:
            failures.append(what)

    check_folders(check)
    try:
        check_session_waits(check)
    except Exception as e:  # a missing method of the code under test is a failed check, not a crash
        failures.append(f"session waits: {type(e).__name__}: {e}")

    if failures:
        print("menu_test_test: FAILED\n  " + "\n  ".join(failures))
        return 1
    print(f"menu_test_test: ok, {checks} checks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
