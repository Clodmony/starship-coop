#!/usr/bin/env python3
"""Tests of the rule checks of tools/coop/lockstep_test.py with a third and a fourth player (#329), without a game.

The checks that read p0 and p1 of a trace now read every player of the session. Each case here gives a check rows as
a trace of three or four players would hold them, once as the rule has it and once broken for a player after the
second, and the broken rows have to fail:

- macbeth-bombs: w8_mabomb_story counts the cases of every target; a case on player 3 that did not hit it is counted
  as not hit, and one that hit another tank as that. w8_mabomb_off: a bomb that hit nobody is off its target only
  when no update had the target in the fire's box; one with the target in the box that hit nobody stays a failure.
- turret: w10_turret_story and w10_turret_check; a turret that steps to player 0 while player 2 is alone in its window,
  one that holds its fire with the last player alone in it, a run in which no turret turns to player 2, one in which
  the turrets fire at player 0 alone, and a first shot with the gun on a human outside the window, fail; a run without
  a first shot at player 2 passes and says so.
- plasma: w9_plasma_story; a shot that passes the nearest human, player 3, too far off fails, and so does a run in
  which player 2 is never the nearest (it says nothing about player 2).
- sy-robot: w8_syrobot_story; a spread shot straight at player 2 fails.
- pose: w9_sypose_story; robots that turn head and gun to player 0 while player 2 is the nearest fail, and so does a
  run without a frame with the last player the nearest.
- escape: w9_escape_rule; blast3 that does not take player 3 down, blast2 that hurts player 3, blast0 that hurts
  player 2, and exit3 that ends the level, fail. w9_escape_case_story: a case whose object never came into its
  target's reach fails as that, not as a broken rule.
- two: the same checks on two-player rows give the counts of the two-player text.
- names: bars_over_names_342 (#342) on rows of a view trace's `tagbox`; a bar drawn after the name it lies on is found
  (the order of a build before #342's fix: each ship's bar and name together), the same boxes with every bar drawn
  before every name are not, and neither are bars and names that do not meet, a name off the screen, or a trace without
  the places.

No build, no game, no ROM; check.py runs it. Exit code 0 when every check holds.
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lockstep_test as lt  # noqa: E402

FAILED = []


def check(name, condition, text=""):
    if not condition:
        FAILED.append(f"{name}: {text}")


def fails(name, call, word):
    """call() has to raise lockstep_test.Fail with `word` in its text."""
    try:
        call()
    except lt.Fail as failure:
        check(name, word in str(failure), f"failed with other words: {failure}")
        return
    check(name, False, "the broken rows passed")


# ---- Macbeth's hazards ----

def mabomb_rows(players, broken=None):
    rows = []
    for round_ in range(lt.W8_MABOMB_ROUNDS):
        for case in lt.W8_MABOMB_CASES:
            for target in range(players):
                hit = 0 if case in lt.W8_MABOMB_DEFLECTS else 1 << target
                if broken == (case, target):
                    hit = 1  # the hazard on `target` hit player 0, as upstream's test would
                rows.append({"frame": len(rows) * 50, "case": case, "target": target, "hit": hit,
                             "first": [0] * players, "lost": False, "dx": 0.0,
                             "deflected": case in lt.W8_MABOMB_DEFLECTS})
    return rows


def test_mabomb():
    story = lt.w8_mabomb_story(mabomb_rows(4), 4)
    check("macbeth-bombs", len(story) == 4 * len(lt.W8_MABOMB_CASES)
          and all(ran == done == lt.W8_MABOMB_ROUNDS and other == 0 for ran, done, other in story.values()), str(story))
    story = lt.w8_mabomb_story(mabomb_rows(4, ("beam", 3)), 4)
    check("macbeth-bombs broken", story[("beam", 3)] == [2, 0, 2] and story[("beam", 2)] == [2, 2, 0], str(story))
    # A bomb at (0, 0, 0): the tank 47 behind it on every update is never in the fire's box, the tank beside it is.
    away = [(0.0, 0.0, 0.0, 10.0, -3.0, 47.0 + 11.0 * n) for n in range(30)]
    under = [(0.0, 41.0, 0.0, 6.0, -3.0, 0.0)] * 30
    inside = away[:5] + [(0.0, 0.0, 0.0, 79.0, -3.0, 39.0)]
    check("macbeth-bombs off", lt.w8_mabomb_off("bomb", 3, 0, away) and lt.w8_mabomb_off("bomb", 3, 0, under), "")
    check("macbeth-bombs in the box and no hit", not lt.w8_mabomb_off("bomb", 3, 0, inside), "")
    check("macbeth-bombs off: hit, another kind, no update lines",
          not lt.w8_mabomb_off("bomb", 3, 8, away) and not lt.w8_mabomb_off("beam", 3, 0, away)
          and not lt.w8_mabomb_off("bomb", 3, 0, []), "")
    rows = mabomb_rows(4, ("bomb", 3))
    for row in rows:
        row["off"] = row["case"] == "bomb" and row["target"] == 3 and row["frame"] < 1000
    story = lt.w8_mabomb_story(rows, 4)
    check("macbeth-bombs off is not counted", story[("bomb", 3)] == [1, 0, 1], str(story[("bomb", 3)]))
    story = lt.w8_mabomb_story(mabomb_rows(2), 2)
    check("macbeth-bombs two", sorted(story) == sorted((c, t) for c in lt.W8_MABOMB_CASES for t in (0, 1)), str(story))


# ---- Macbeth's turrets ----

def turret_row(frame, players, alone=None, to=None, fires=False, ready=False, slot=0):
    """A waiting turret's frame: player `alone` alone and well in its window (None: nobody in it), its yaw steps to
    player `to`'s direction. The players' yaws are 30 degrees apart, player n's is 30 * n."""
    row = {"frame": frame, "slot": slot, "car": 1, "last": 0, "boulder": 0, "step": (0, 1 if fires else 0), "hp": 10,
           "wait": 0 if ready else 5, "at": (0.0, 0.0, 0.0), "line": f"turret frame={frame}",
           "players": list(range(players)), "near": 0 if alone is None else alone, "sure": True}
    for n in range(players):
        row[f"w{n}"] = 30.0 * n
        row[f"in{n}"] = int(n == alone)
        row[f"well{n}"] = n == alone
        row[f"out{n}"] = n != alone
        row[f"flies{n}"] = 1
        row[f"p{n}"] = (500.0 * (n + 1), 0.0, 1000.0)
    want = 30.0 * (to if to is not None else 0)
    # A tenth of the way to `want`; at its first shot the gun is nearly on it.
    row["yaw"] = (want - 1.0, want - 0.9) if fires else (want - 10.0, want - 9.0)
    return row


def turret_rows(players, step_to=None, idle=None, no_fire=None):
    """Turret frames as the rule has them for every player; step_to, idle and no_fire break it for one player."""
    rows = []
    for n in range(players):
        for _ in range(30):
            rows.append(turret_row(len(rows), players, alone=n, to=step_to if (step_to is not None and n == 2) else n))
        for _ in range(4):
            if no_fire != n:
                rows.append(turret_row(len(rows), players, alone=n, to=n, fires=True))
        for _ in range(10):
            rows.append(turret_row(len(rows), players, alone=None, to=0))
    if idle is not None:
        rows.append(turret_row(len(rows), players, alone=idle, to=idle, ready=True))
    return rows


def test_turret():
    for players in (3, 4):
        count = lt.w10_turret_story(turret_rows(players), players)
        text = lt.w10_turret_check(count, players)
        check(f"turret {players}", count["one_in_of"] == [30 + 4] * players and count["one_out"] == 0
              and count["fire_only"] == [4] * players and count["nobody_near"] == 10 * players, text)
        fails(f"turret {players} steps to player 0",
              lambda: lt.w10_turret_check(lt.w10_turret_story(turret_rows(players, step_to=0), players), players),
              "do not turn to the human that is alone in the window")
        fails(f"turret {players} holds its fire",
              lambda: lt.w10_turret_check(lt.w10_turret_story(turret_rows(players, idle=players - 1), players),
                                          players), "holds its fire")
        text = lt.w10_turret_check(lt.w10_turret_story(turret_rows(players, no_fire=2), players), players)
        check(f"turret {players} never fires at player 2", "no first shot tells of player 2" in text, text)
        fails(f"turret {players} fires at player 0 alone",
              lambda: lt.w10_turret_check(lt.w10_turret_story(
                  [r for r in turret_rows(players) if r["step"][1] == 0 or r["well0"]]
                  + [turret_row(9000 + n, players, alone=0, to=0, fires=True) for n in range(2)], players), players),
              "no turret fires with a player after the first")
        fails(f"turret {players} never turns to player 2",
              lambda: lt.w10_turret_check(lt.w10_turret_story(
                  [r for r in turret_rows(players) if not r["well2"]], players), players),
              "no turret turned to player 2")
        fails(f"turret {players} fires with its gun on a human outside the window",
              lambda: lt.w10_turret_check(lt.w10_turret_story(
                  turret_rows(players) + [turret_row(9000, players, alone=1, to=2, fires=True)], players), players),
              "with its gun on a human outside it")
    fails("turret lines of two players in a session of three",
          lambda: lt.w10_turret_story(turret_rows(2), 3), "the session has 3")
    count = lt.w10_turret_story(turret_rows(2), 2)
    text = lt.w10_turret_check(count, 2)
    check("turret two", "player 1 the nearer on 34" in text and "3 with only player 1" not in text
          and "4 with only player 1 in the window, 4 with only player 0" in text, text)


# ---- the Granga's plasma, Sector Y's robots ----

def shot_row(frame, players, nearest, off):
    """A shot from (0, 0, 0) that crosses the depth of player `nearest`, the nearest to its start, `off` from it in x
    and y. Player n flies at x 300 * n, 2000 from the shot's start when it is the nearest and 4000 otherwise."""
    places = {}
    for n in range(players):
        x, far = 300.0 * n, 2000.0 if n == nearest else 4000.0
        places[n] = ([x, 0.0, -math.sqrt(far * far - x * x)], True)
    aim = [places[nearest][0][0] + off[0], off[1], places[nearest][0][2]]
    return {"level": 0, "frame": frame, "script": 66, "spread": 1, "start": [0.0, 0.0, 0.0], "dir": aim,
            "players": places, "line": f"shot frame={frame}"}


def shot_rows(players, skip=None, wrong=None, straight=None):
    rows = []
    for n in range(players):
        if n == skip:
            continue
        for k in range(8):
            off = (40.0 * (k - 3.5), 20.0 * (3.5 - k))
            if n == wrong and k == 0:
                off = (-300.0 * n, 0.0)  # at player 0's x, not around the nearest
            if n == straight and k == 0:
                off = (0.0, 0.0)
            rows.append(shot_row(len(rows), players, n, off))
    return rows


def test_spread():
    for players in (3, 4):
        for name, story in (("plasma", lt.w9_plasma_story), ("sy-robot", lt.w8_syrobot_story)):
            problems, text = story(shot_rows(players), players)
            check(f"{name} {players}", not problems, f"{problems} {text}")
            problems, _ = story(shot_rows(players, wrong=players - 1), players)
            check(f"{name} {players} not around the nearest", any("not around the nearest human" in p
                                                                   for p in problems), str(problems))
            problems, _ = story(shot_rows(players, skip=2), players)
            check(f"{name} {players} says nothing about player 2",
                  any("with player 2 the nearest: the run says nothing" in p for p in problems), str(problems))
            problems, _ = story(shot_rows(players, straight=2), players)
            check(f"{name} {players} straight at player 2", any("straight at" in p for p in problems), str(problems))
    problems, text = lt.w9_plasma_story(shot_rows(2), 2)
    check("plasma two", not problems and "8 with player 1 the nearer" in text, f"{problems} {text}")
    problems, text = lt.w8_syrobot_story(shot_rows(2, skip=1), 2)
    check("sy-robot two, nothing around player 1", any("too few spread shots around player 1" in p for p in problems),
          str(problems))


# ---- Sector Y's robots: head and gun ----

def pose_row(frame, players, nearest, to):
    """A robot's aiming frame with player `nearest` the nearest: head and gun step to player `to`'s direction. Player
    n's yaw is 20 * n and its pitch 10 * n."""
    row = shot_row(frame, players, nearest, (0.0, 0.0))
    yaw, pitch = 20.0 * to, 10.0 * to
    row.update({"slot": 5, "body": 0, "wants": {n: [20.0 * n, 10.0 * n] for n in range(players)},
                "before": [pitch - 10.0, yaw - 10.0, 0.0, pitch - 10.0, 0.0],
                "after": [pitch - 9.0, yaw - 9.0, 0.0, pitch - 8.0, 0.0]})
    return row


def pose_rows(players, wrong=None, skip=None):
    return [pose_row(n * 100 + k, players, n, 0 if n == wrong else n)
            for n in range(players) if n != skip for k in range(40)]


def test_pose():
    for players in (3, 4):
        problems, text = lt.w9_sypose_story(pose_rows(players), players)
        check(f"pose {players}", not problems and f"with player {players - 1} the nearest 120, 0, 0" in text,
              f"{problems} {text}")
        problems, text = lt.w9_sypose_story(pose_rows(players, wrong=2), players)
        check(f"pose {players} turns to player 0 with player 2 the nearest",
              any("turn head and gun to player 0" in p for p in problems)
              and any("no angle step tells with player 2" in p for p in problems), f"{problems} {text}")
        problems, _ = lt.w9_sypose_story(pose_rows(players, skip=players - 1), players)
        check(f"pose {players} says nothing about the last player",
              any(f"with player {players - 1} the nearest: the run says nothing" in p for p in problems), str(problems))
    problems, text = lt.w9_sypose_story(pose_rows(2), 2)
    check("pose two", not problems and "120 angle steps go to player 1's direction, 0 to player 0's" in text,
          f"{problems} {text}")
    problems, _ = lt.w9_sypose_story(pose_rows(2, wrong=1), 2)
    check("pose two turns to player 0", any("turn head and gun to player 0 while player 1" in p for p in problems),
          str(problems))


# ---- Andross' escape ----

def escape_values(players, hit=0, down=0, exit_=0, low=None, lives="5/5"):
    values = {"frame": "100", "hit": str(hit), "down": str(down), "exit": str(exit_), "lives": lives}
    for n in range(players):
        least = (low or {}).get(n, 255)
        values[f"p{n}"] = f"450/600,255/{least}/{least},3"
    return values


def escape_back(target, lives=4):
    return [("player", {"player": str(target), "frame": "110", "level": "9", "down": "1"}, ""),
            ("player", {"player": str(target), "frame": str(110 + lt.W9_ESCAPE_RETURN), "level": "9", "down": "0",
                        "parked": "0", "state": "3", "shields": str(lt.W9_ESCAPE_FULL), "lives": str(lives)}, "")]


def test_escape():
    check("escape cases 3", lt.w9_escape_cases(3) == ("blast0", "miss2", "blast2", "exit2"), str(lt.w9_escape_cases(3)))
    check("escape cases 4", lt.w9_escape_cases(4) == ("blast0", "miss3", "blast3", "exit3"), str(lt.w9_escape_cases(4)))
    # A case line as the probe writes it: the object 450 from player 3, and one that waited 3000 away.
    line = ("escape case=blast3 target=3 run=1 frame=631 end=691 updates=60 off={off} lost=0 exit=0 hit={hit} "
            "down={hit} lives=5/5 backdrop=7 p0=533/3512,255/255/255,3 p1=767/3277,255/255/255,3 "
            "p2=530/3514,255/255/255,3 p3={reach}/3000,255/0/0,3")

    def case_rows(**values):
        text = line.format(**values)
        return [("case", dict(part.split("=", 1) for part in text.split()[1:]), text)]

    values, story = lt.w9_escape_case_story(case_rows(off=15, hit=8, reach=450), "blast3")
    check("escape case story", values["target"] == "3" and "player 3 at 450 to 3000" in story, story)
    fails("escape case never in reach",
          lambda: lt.w9_escape_case_story(case_rows(off=60, hit=0, reach=3000), "blast3"),
          "was never in reach of player 3")
    good = escape_values(4, hit=8, down=8, low={3: 0})
    text = lt.w9_escape_rule(escape_back(3), "blast3", good)
    check("escape blast3", "player 3 down at frame 110" in text, text)
    fails("escape blast3 takes nobody down", lambda: lt.w9_escape_rule(escape_back(3), "blast3", escape_values(4)),
          "in reach of player 3 did not take it down")
    fails("escape blast3 never returns", lambda: lt.w9_escape_rule([], "blast3", good), "no line says player 3 is down")
    fails("escape blast2 hurts player 3",
          lambda: lt.w9_escape_rule(escape_back(2), "blast2", escape_values(4, hit=12, down=4, low={2: 0, 3: 200})),
          "player 3, out of reach")
    fails("escape blast0 hurts player 2",
          lambda: lt.w9_escape_rule([], "blast0", escape_values(3, hit=5, down=1, low={0: 0, 2: 100})),
          "player 2, out of reach")
    check("escape blast0", lt.w9_escape_rule([], "blast0", escape_values(4, hit=1, down=1, low={0: 0}))
          == "player 1, 2, 3 unhurt", "")
    fails("escape exit3 ends the level", lambda: lt.w9_escape_rule([], "exit3", escape_values(4, exit_=1)),
          "the exit is player 0's")
    fails("escape miss2 hurts", lambda: lt.w9_escape_rule([], "miss2", escape_values(3, hit=4, low={2: 200})),
          "out of everyone's reach hurt someone")
    # Two players: the texts of #282.
    check("escape two blast0", lt.w9_escape_rule([], "blast0", escape_values(2, hit=1, down=1, low={0: 0}))
          == "player 1 unhurt", "")
    text = lt.w9_escape_rule(escape_back(1), "blast1", escape_values(2, hit=2, down=2, low={1: 0}))
    check("escape two blast1", text.startswith("player 1 down at frame 110, back at 260"), text)


# ---- The bars and the names of the other ships (#342) ----

def tagbox_line(frame, *ships):
    """A view trace line; each ship is (player, name box, bar box, bar place, name place)."""
    items = [f"{p}:" + "/".join(f"{v:.1f}" for v in (*name, *bar, 0.0)) + f"/{bar_at}/{name_at}"
             for p, name, bar, bar_at, name_at in ships]
    return f"frame={frame} tagbox={','.join(items) or '-'}"


def test_names():
    # Ship 1's bar lies on ship 0's name (the boxes are left/top/right/bottom on the 320x240 screen); each ship's own
    # bar lies below its name.
    ship0 = (0, (100.0, 40.0, 140.0, 50.0), (90.0, 52.0, 150.0, 60.0))
    ship1 = (1, (200.0, 100.0, 240.0, 110.0), (95.0, 44.0, 155.0, 52.0))
    ship2 = (2, (10.0, 200.0, 50.0, 210.0), (5.0, 212.0, 65.0, 220.0))

    def rows(*ships):
        return name_rows(tagbox_line(1, *ships))

    def name_rows(line, on_screen_only=False):
        (frame, boxes), = lt.name_box_rows_332(line, on_screen_only)
        return boxes

    together = rows((*ship0, 0, 1), (*ship1, 2, 3), (*ship2, 4, 5))  # each ship's bar, then its name
    check("names on", lt.bars_on_names_342(together) == [(1, 0)], f"{lt.bars_on_names_342(together)}")
    check("names over", lt.bars_over_names_342(together) == [(1, 0)],
          f"a bar drawn after the name it lies on was not found: {lt.bars_over_names_342(together)}")
    apart = rows((*ship0, 2, 3), (*ship1, 0, 4), (*ship2, 1, 5))  # every bar, then every name
    check("names on, bars first", lt.bars_on_names_342(apart) == [(1, 0)], f"{lt.bars_on_names_342(apart)}")
    check("names bars first", lt.bars_over_names_342(apart) == [], f"{lt.bars_over_names_342(apart)}")
    # the same two ships, the lower index drawn second: ship 0's bar lies on nothing, so nothing is covered
    behind = rows((*ship0, 2, 3), (*ship1, 0, 1))
    check("names bar before name", lt.bars_over_names_342(behind) == [], f"{lt.bars_over_names_342(behind)}")
    check("names none", lt.bars_on_names_342(rows((*ship0, 0, 1), (*ship2, 2, 3))) == [], "boxes that do not meet")
    # a name off the screen is not covered by anything that counts; the bar of that ship, on the screen, is
    off = (3, (400.0, 40.0, 440.0, 50.0), (96.0, 44.0, 156.0, 52.0))
    seen = rows((*ship0, 0, 1), (*off, 2, 3))
    check("names off screen", lt.bars_over_names_342(seen) == [(3, 0)], f"{lt.bars_over_names_342(seen)}")
    check("names off screen listed", sorted(seen) == [0, 3], f"{sorted(seen)}")
    on_screen = name_rows(tagbox_line(1, (*ship0, 0, 1), (*off, 2, 3)), True)
    check("names on screen only", sorted(on_screen) == [0], f"{sorted(on_screen)}")
    # a trace of a build before #342 has no places: the reader says so, and the scenario refuses it
    old = name_rows("frame=1 tagbox=0:100.0/40.0/140.0/50.0/90.0/52.0/150.0/60.0/0.0")
    check("names old trace", old[0][3] is None, f"{old[0]}")


def main():
    for test in (test_mabomb, test_turret, test_spread, test_pose, test_escape, test_names):
        test()
    for line in FAILED:
        print(f"FAILED  {line}")
    print("FAIL" if FAILED else "PASS: the rule checks of lockstep_test.py read every player (#329) and the bars and names of the view (#342)")
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
