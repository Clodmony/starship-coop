#!/usr/bin/env python3
"""What each client of one session shows and hears, on Corneria (#30).

    python tools/coop/view_check.py                     # unattended; prints PASS or FAIL
    python tools/coop/view_check.py --section rails     # or all-range; default both

Plays one replay per section twice, with SF64_COOP_TEST_LOCAL_INDEX 0 and 1: two clients of one session. Each game
starts straight in Corneria, before its intro (replay.py, Start). Input by rule flies both players (player_trace.py),
and buttons and pokes give them different numbers, so a client that shows the wrong player shows the wrong number. Each
game writes a view trace (src/port/coop/test/view_trace.h) and a state hash log. The trace says what the client holds to
show, not what reached the screen: the HUD numbers without a value of their own (lives, the boost gauge) and the look of
everything are for a picture, see docs/coop/QA.md.

Rules, on the frames they talk about:

  camera-parked   while a client's own player is parked it looks through player 0: the same eye as client 0
  camera-own      while it flies, the client looks through it, and the two clients draw different cameras
  local-index     who a client is does not change while it looks through player 0: its controller steers by this
  hud-shield      the shield gauge holds the shields of the client's own player
  hud-bombs       the bomb counter reaches the bombs of the client's own player; it may trail for the few frames of
                  its own step (HUD_BombCounter_Draw)
  cockpit         only the client of a player in cockpit view draws the glass
  flash           a hit turns the screen red on the client of the player hit, and on no other
  shield-bar      the other player, never the own one, has a bar with its shields, and a parked player has none. In
                  all-range the bar shows exactly when the other player is ahead of the camera
  sound-own       the view change and shield alarm of a player are heard on its own client only
  sound-engine    a player who joins starts its engine, each join after leaving starts it again, and it is off while
                  the player is parked
  sound-source    all-range: a client's own player sounds from in front of its camera
  player-down     all-range: when player 1 goes down its client looks through player 0, and its engine is off
  pause           pause is part of the shared game: player 0's pad pauses both clients on the same frames, player 1's
                  pad pauses neither
  hash            both clients played one game, pokes and all

Exit code 0 on PASS, 1 otherwise.
"""
import argparse
import math
import sys
from collections import namedtuple
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hash_check  # noqa: E402
import player_trace  # noqa: E402
import replay as replay_tool  # noqa: E402

BUTTON_B = 0x4000
BUTTON_START = 0x1000
BUTTON_C_UP = 0x0008  # U_CBUTTONS

# include/sfx.h
SFX_ENGINE = 0x0100F005  # NA_SE_ARWING_ENGIN_GRD
SFX_VIEW_IN = 0x4900002C  # NA_SE_VIEW_MOVE_IN
SFX_SHIELD_WARNING = 0x49008010  # NA_SE_SHIELD_WARNING0
FORM_NONE = 255  # include/sf64player.h, what sPlayerNoise holds while a player has no engine sound

# Through the intro, not past it: what a client looks through while its player waits is part of what this checks.
START = replay_tool.LEVEL
ACTIVE = 1680  # level frame by which player 0 flies Corneria's rails

# The level frames things happen on. Pads press on the frame given; pokes land after it.
POKE_NUMBERS = ACTIVE + 60
PRESS_P1_VIEW = (ACTIVE + 200, ACTIVE + 320)
PRESS_P0_VIEW = (ACTIVE + 420, ACTIVE + 540)
HIT_P1 = ACTIVE + 640
HIT_P0 = ACTIVE + 700
LOW_SHIELDS = ACTIVE + 760
HIT_LOW = ACTIVE + 770
FLASH_FRAMES = 20
BOMB_STEP_FRAMES = 3
BESIDE_CAMERA = 0.05
PRESS_P1_START = ACTIVE + 800  # must do nothing
PRESS_P0_START = ACTIVE + 830
PRESS_P0_RESUME = ACTIVE + 870  # B, in the pause menu
ALL_RANGE_NUMBERS = 2300  # both players fly all-range by then
ALL_RANGE_DOWN = 3060  # player 1 is ACTIVE here; a U-turn has no down check

SHIELDS_P0, SHIELDS_P1 = 200, 120
BOMBS_P0, BOMBS_P1 = 5, 7

# Section -> (replay frames, when L goes into all-range, from when player 0 flies). All-range ends up with both players
# flying again.
SECTIONS = {"rails": (START.level + ACTIVE + 930, None), "all-range": (START.frame(1525), hash_check.GENERATED_L)}
TIMEOUT = 1500

Client = namedtuple("Client", "index rows by_frame")
Slot = namedtuple("Slot", "state parked shields bombs lives alternate sfx pos")
Row = namedtuple("Row", "frame mode local view eye at shield bombs cockpit fill flash bars players noise paused sfx")


def floats(text):
    return tuple(float(v) for v in text.split(","))


def parse_row(line):
    f = dict(kv.split("=", 1) for kv in line.split())
    players = []
    i = 0
    while f"p{i}" in f:
        s = f[f"p{i}"].split(",")
        players.append(Slot(int(s[0]), int(s[1]) != 0, int(s[2]), int(s[3]), int(s[4]), int(s[5]) != 0,
                            tuple(float(v) for v in s[6:9]), tuple(float(v) for v in s[9:12])))
        i += 1
    bars = {} if f["bars"] == "-" else {int(b.split(":")[0]): float(b.split(":")[1]) for b in f["bars"].split(",")}
    sfx = [] if f["sfx"] == "-" else [(int(s.split("@")[0], 16), s.split("@")[1]) for s in f["sfx"].split(",")]
    return Row(int(f["frame"]), int(f["mode"]), int(f["local"]), int(f["view"]), floats(f["eye"]), floats(f["at"]), float(f["shield"]),
               int(f["bombs"]), int(f["cockpit"]) != 0, tuple(int(v) for v in f["fill"].split(",")), int(f["flash"]),
               bars, players, tuple(int(v) for v in f["noise"].split(",")), int(f["pause"]) != 0, sfx)


def read_trace(index, path):
    rows = []
    lines = Path(path).read_text(encoding="utf-8", errors="replace").splitlines()
    for n, line in enumerate(lines):
        try:
            rows.append(parse_row(line))
        except (KeyError, ValueError, IndexError):
            if n != len(lines) - 1:  # stopping the game can cut the last line
                raise
    return Client(index, rows, {row.frame: row for row in rows})


def pairs(clients):
    """(row of client 0, row of client 1) of every frame both logged, in order."""
    a, b = clients
    return [(row, b.by_frame[row.frame]) for row in a.rows if row.frame in b.by_frame]


def joined(row, i=1):
    """Player i is flying, not parked."""
    return len(row.players) > i and not row.players[i].parked


def red(row):
    """A red the screen shows on this frame, from either source."""
    return row.flash > 0 or (row.fill[0] == 255 and row.fill[1] == 0 and row.fill[2] == 0 and row.fill[3] > 0)


def rule_camera_parked(clients):
    frames = [(a, b) for a, b in pairs(clients) if not joined(b)]
    wrong = [b.frame for a, b in frames if b.view != 0 or a.eye != b.eye]
    if not frames:
        return False, "player 1 was never parked"
    return not wrong, f"{len(frames) - len(wrong)} of {len(frames)} parked frames look through player 0" + \
        (f", first wrong {wrong[0]}" if wrong else "")


def rule_camera_own(clients):
    frames = [(a, b) for a, b in pairs(clients) if joined(b)]
    if not frames:
        return False, "player 1 never joined"
    wrong = [b.frame for a, b in frames if a.view != 0 or b.view != 1]
    # The join copies player 0's camera, so the first frames can be alike.
    apart = sum(1 for a, b in frames if a.eye != b.eye)
    good = apart >= len(frames) * 0.9 and not wrong
    return good, f"view right on {len(frames) - len(wrong)} of {len(frames)} frames, cameras differ on {apart}"


def rule_local_index(clients):
    wrong = [(client.index, row.frame, row.local) for client in clients for row in client.rows
             if row.local != client.index]
    parked = sum(1 for row in clients[1].rows if not joined(row))
    detail = f"client 1 stays player 1 on all its frames, {parked} of them parked"
    return not wrong, detail if not wrong else "first wrong: client {} frame {} says {}".format(*wrong[0])


def rule_hud_shield(clients):
    wrong = []
    checked = 0
    for client in clients:
        for row in client.rows:
            if row.frame <= POKE_NUMBERS + 2 or not joined(row):
                continue
            checked += 1
            shown = row.players[client.index].shields / 255.0
            if abs(row.shield - shown) > 0.01:
                wrong.append((client.index, row.frame, row.shield, shown))
    if not checked:
        return False, "no frame after the poke with both players flying"
    detail = f"{checked - len(wrong)} of {checked} frames right"
    if wrong:
        detail += ", first wrong: client {} frame {} shows {:.3f}, its player has {:.3f}".format(*wrong[0])
    return not wrong, detail


def rule_hud_bombs(clients):
    wrong = []
    checked = 0
    for client in clients:
        trailing = 0
        for row in client.rows:
            if row.frame <= POKE_NUMBERS + 90 or not joined(row):
                continue
            checked += 1
            trailing = trailing + 1 if row.bombs != row.players[client.index].bombs else 0
            if trailing > BOMB_STEP_FRAMES:
                wrong.append((client.index, row.frame, row.bombs, row.players[client.index].bombs))
    if not checked:
        return False, "no frame after the poke with both players flying"
    detail = f"{checked - len(wrong)} of {checked} frames right"
    if wrong:
        detail += ", first wrong: client {} frame {} shows {}, its player has {}".format(*wrong[0])
    return not wrong, detail


def rule_cockpit(clients):
    results = []
    good = True
    for owner in (0, 1):
        other = 1 - owner
        frames = [row.frame for row in clients[owner].rows if joined(row) and row.players[owner].alternate]
        own = sum(1 for f in frames if f in clients[owner].by_frame and clients[owner].by_frame[f].cockpit)
        leak = sum(1 for f in frames if f in clients[other].by_frame and clients[other].by_frame[f].cockpit)
        results.append(f"player {owner} in cockpit view {len(frames)} frames: own client {own}, other client {leak}")
        good = good and own >= 30 and leak == 0
    return good, "; ".join(results)


def rule_flash(clients):
    results = []
    good = True
    for hit, owner in ((HIT_P1, 1), (HIT_P0, 0)):
        window = range(hit, hit + FLASH_FRAMES)
        own = sum(1 for f in window if f in clients[owner].by_frame and red(clients[owner].by_frame[f]))
        other = sum(1 for f in window if f in clients[1 - owner].by_frame and red(clients[1 - owner].by_frame[f]))
        results.append(f"hit on player {owner}: own client {own} red frames, other client {other}")
        good = good and own >= 8 and other == 0
    return good, "; ".join(results)


def ahead_of_camera(row, other):
    """Cosine between where the camera looks and where player `other` is. All-range only: there the world z of a player
    is its trueZpos, on rails it also needs zPath, which the trace does not hold."""
    look = [a - e for a, e in zip(row.at, row.eye)]
    to = [p - e for p, e in zip(row.players[other].pos, row.eye)]
    size = math.sqrt(sum(v * v for v in look) * sum(v * v for v in to))
    return sum(a * b for a, b in zip(look, to)) / size if size else 0.0


def rule_shield_bar(clients):
    results = []
    good = True
    for client in clients:
        other = 1 - client.index
        flying = [row for row in client.rows if joined(row)]
        shown = [row for row in flying if other in row.bars]
        own = [row.frame for row in client.rows if client.index in row.bars]
        parked = [row.frame for row in client.rows if not joined(row) and row.bars]
        wrong = [row.frame for row in shown
                 if abs(row.bars[other] - min(row.players[other].shields, 255) / 255.0) > 0.01]
        # Beside the camera the two sums can round either way, so those frames say nothing.
        free_flight = [row for row in flying if row.mode == player_trace.ALL_RANGE
                       and abs(ahead_of_camera(row, other)) >= BESIDE_CAMERA]
        misplaced = [row.frame for row in free_flight if (ahead_of_camera(row, other) > 0.0) != (other in row.bars)]
        results.append(f"client {client.index}: bar on the other player on {len(shown)} of {len(flying)} flying frames,"
                       f" {len(wrong)} wrong, own {len(own)}, while parked {len(parked)},"
                       f" against the camera {len(misplaced)} of {len(free_flight)} wrong")
        enough = free_flight or len(shown) >= len(flying) * 0.5
        good = good and flying and shown and enough and not wrong and not own and not parked and not misplaced
    return bool(good), "; ".join(results)


def count_sfx(client, sfx_id, window):
    return sum(1 for f in window if f in client.by_frame for s, _ in client.by_frame[f].sfx if s == sfx_id)


def rule_sound_own(clients):
    results = []
    good = True
    for frame, owner in ((PRESS_P1_VIEW[0], 1), (PRESS_P0_VIEW[0], 0)):
        window = range(frame - 5, frame + 15)
        own, other = count_sfx(clients[owner], SFX_VIEW_IN, window), count_sfx(clients[1 - owner], SFX_VIEW_IN, window)
        results.append(f"player {owner} view change: own client {own}, other client {other}")
        good = good and own == 1 and other == 0
    window = range(HIT_LOW - 2, HIT_LOW + 10)
    own, other = count_sfx(clients[1], SFX_SHIELD_WARNING, window), count_sfx(clients[0], SFX_SHIELD_WARNING, window)
    results.append(f"player 1 shield alarm: own client {own}, other client {other}")
    return good and own == 1 and other == 0, "; ".join(results)


def joins(client):
    """Frames on which player 1 went from parked to flying."""
    return [b.frame for a, b in zip(client.rows, client.rows[1:]) if not joined(a) and joined(b)]


def rule_sound_engine(clients):
    results = []
    good = True
    for client in clients:
        frames = joins(client)
        heard = [f for f in frames if count_sfx_from(client, SFX_ENGINE, "p1", range(f - 1, f + 3))]
        # Stopping is a frame later than parking: the player parks at the start of the frame's player loop.
        running = [row.frame for prev, row in zip(client.rows, client.rows[1:])
                   if not joined(prev) and not joined(row) and row.noise[1] != FORM_NONE]
        results.append(f"client {client.index}: player 1 joined {len(frames)} times, engine started on {len(heard)},"
                       f" running on {len(running)} parked frames")
        good = good and frames and len(heard) == len(frames) and not running
    return bool(good), "; ".join(results)


def count_sfx_from(client, sfx_id, source, window):
    return sum(1 for f in window if f in client.by_frame for s, src in client.by_frame[f].sfx
               if s == sfx_id and src == source)


def rule_sound_source(clients):
    client = clients[1]
    flying = [row for row in client.rows if joined(row) and row.mode == player_trace.ALL_RANGE and row.view == 1]
    if not flying:
        return False, "player 1 never flew all-range"
    # sfxSource is view space: x to the side, z ahead (negative). Seen from its own chase camera a player is ahead.
    ahead = [row for row in flying if abs(row.players[1].sfx[0]) <= abs(row.players[1].sfx[2]) * 0.5 + 60.0]
    return len(ahead) >= len(flying) * 0.95, f"own player ahead of the camera on {len(ahead)} of {len(flying)} frames"


def rule_player_down(clients):
    client = clients[1]
    after = [row for row in client.rows if row.frame > ALL_RANGE_DOWN + 5]
    wrong = [row.frame for row in after if joined(row) or row.view != 0 or row.noise[1] != FORM_NONE]
    good = len(after) >= 20 and not wrong
    return good, f"{len(after) - len(wrong)} of {len(after)} frames after the poke: out, looks through player 0, engine off"


def rule_pause(clients):
    counts = [sum(1 for row in client.rows if row.paused) for client in clients]
    early = [sum(1 for row in client.rows if row.paused and row.frame < PRESS_P0_START - 2) for client in clients]
    good = counts[0] >= 20 and counts[0] == counts[1] and not any(early)
    return good, f"paused rows on client 0 and 1: {counts[0]}, {counts[1]}; before player 0's press: {early[0]}, {early[1]}"


RULES = {
    "rails": {
        "camera-parked": rule_camera_parked,
        "camera-own": rule_camera_own,
        "local-index": rule_local_index,
        "hud-shield": rule_hud_shield,
        "hud-bombs": rule_hud_bombs,
        "cockpit": rule_cockpit,
        "flash": rule_flash,
        "shield-bar": rule_shield_bar,
        "sound-own": rule_sound_own,
        "sound-engine": rule_sound_engine,
        "pause": rule_pause,
    },
    "all-range": {
        "camera-parked": rule_camera_parked,
        "camera-own": rule_camera_own,
        "local-index": rule_local_index,
        "hud-shield": rule_hud_shield,
        "shield-bar": rule_shield_bar,
        "sound-engine": rule_sound_engine,
        "sound-source": rule_sound_source,
        "player-down": rule_player_down,
    },
}


def extra_buttons(flight, pad):
    """The presses of the rails section, on top of the input by rule."""
    level = START.frame(flight) - START.level
    if pad == 1 and level in PRESS_P1_VIEW or pad == 0 and level in PRESS_P0_VIEW:
        return BUTTON_C_UP
    if pad == 1 and level == PRESS_P1_START or pad == 0 and level == PRESS_P0_START:
        return BUTTON_START
    if pad == 0 and level == PRESS_P0_RESUME:
        return BUTTON_B
    return 0


def pokes(section):
    numbers = [f"{POKE_NUMBERS}:0:shields:{SHIELDS_P0}", f"{POKE_NUMBERS}:1:shields:{SHIELDS_P1}",
               f"{POKE_NUMBERS}:0:bombs:{BOMBS_P0}", f"{POKE_NUMBERS}:1:bombs:{BOMBS_P1}"]
    if section == "all-range":
        return ",".join([f"{ALL_RANGE_NUMBERS}:{p.split(':', 1)[1]}" for p in numbers]
                         + [f"{ALL_RANGE_DOWN}:1:down:4"])
    return ",".join(numbers + [f"{HIT_P1 - 1}:1:hit:20", f"{HIT_P0 - 1}:0:hit:20", f"{LOW_SHIELDS}:1:shields:80",
                               f"{HIT_LOW - 1}:1:hit:20"])


def play_section(section, exe, args):
    """Play one section with each local index. Returns a list of failures."""
    frames, l_at = SECTIONS[section]
    replay = replay_tool.ROOT / "local" / f"test-view-{section}.sfcr"
    player_trace.write_rail_replay(replay, frames, l_at, extra=extra_buttons if section == "rails" else None,
                                   start=START)
    cvars = {"gCoopDebugPlayerCount": 2, "gInvincible": 1}
    if l_at is not None:
        cvars["gDebugJumpToAllRange"] = 1

    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(hash_check.play, replay, exe, args.timeout, f"view-{section}-{index}",
                            config={"CVars": cvars}, start=START,
                            env_extra={"SF64_COOP_TEST_LOCAL_INDEX": str(index),
                                       "SF64_COOP_VIEW_TRACE": "view-trace.txt",
                                       "SF64_COOP_VIEW_POKE": pokes(section)}) for index in (0, 1)]
        results = [job.result() for job in jobs]

    if any(values is None or rows is None for values, _, rows in results):
        return [f"{section}: a run wrote no report or hash log"]
    failures = [f"{section}: client {i} did not play through: {values.get('result')}"
                for i, (values, _, _) in enumerate(results) if not hash_check.played_through(values)]
    clients = [read_trace(i, Path(values["log"]).parent / "view-trace.txt") for i, (values, _, _) in enumerate(results)]

    for name, rule in RULES[section].items():
        good, detail = rule(clients)
        print(f"view: {section}: {name}: {'ok' if good else 'FAIL'}: {detail}")
        if not good:
            failures.append(f"{section}: {name}")

    (_, names, a_rows), (_, _, b_rows) = results
    diff = hash_check.first_difference(a_rows, b_rows, frames)
    if diff is not None:
        print(f"view: {section}: hash: FAIL: {hash_check.describe(names, diff)}")
        failures.append(f"{section}: hash")
    else:
        print(f"view: {section}: hash: ok: equal on all {min(frames, len(a_rows), len(b_rows))} frames")
    return failures


def main():
    p = argparse.ArgumentParser(description="what each client shows and hears")
    p.add_argument("--section", choices=[*SECTIONS, "both"], default="both")
    p.add_argument("--exe", help="Starship executable (default: newest build found)")
    p.add_argument("--timeout", type=int, default=TIMEOUT, help="per run: give up after this many seconds")
    args = p.parse_args()

    exe = replay_tool.find_exe(args.exe)
    failures = []
    for section in SECTIONS if args.section == "both" else (args.section,):
        failures += play_section(section, exe, args)
    for f in failures:
        print(f"FAIL: {f}")
    print("view: FAIL" if failures else "view: PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
