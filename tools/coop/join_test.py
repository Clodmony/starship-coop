#!/usr/bin/env python3
"""Real game clients through the join flow (WP-24): lobby to level start over loopback, with nobody at the keyboard.

    python tools/coop/join_test.py                    # every scenario
    python tools/coop/join_test.py --only level,taken
    python tools/coop/join_test.py --list
    python tools/coop/join_test.py --frames 2600 --exe <Starship>

Each game runs in its own folder under local/ (see tools/coop/replay.py) with its own replay, so config, save and log
folders are never the owner's and no controller reaches it. The replays start from different seeds, so two games are
apart until the level start: a pass has to come from the session, and each scenario checks that they were apart. The
scripted join flow is src/port/coop/test/join_driver.h; the games report through it and write a state hash log.

What counts is the hash log from the first frame after Play_Init on, frame by frame, every region. No lockstep runs
(WP-22) and a frame has no owner, so this works because no game gets input after its level start. Prints one line per
scenario and a RESULT line, exits 0 only when everything passed. Needs a build with assets.
"""
import argparse
import struct
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hash_check  # noqa: E402
import replay as replay_tool  # noqa: E402

GSTATE_PLAY = 7  # include/sf64thread.h
LEVEL_CORNERIA = 0
START_FRAME = 600  # the host starts here at the earliest; the title screen is playing
MENU_START_FRAME = 1500  # the same for a host that has pressed A through the menus
FOUND_IN_LEVEL = 1200  # common frames in a level below this prove too little
SEEDS = {  # replay headers: what each game starts with instead of the clock
    "host": (1111, 2222, 3333, 4444, 5555, 6666),
    "client0": (7001, 7002, 7003, 7004, 7005, 7006),
    "client1": (8101, 8102, 8103, 8104, 8105, 8106),
}


class Fail(Exception):
    pass


def expect(condition, text):
    if not condition:
        raise Fail(text)


def write_replay(path, frames, seeds, press_until=0):
    """Input for one game: A every 30 frames up to press_until, then none. The layout is in hash_check.py."""
    data = bytearray(b"SFCR" + struct.pack("<III6i", 2, 20, 204, *seeds))
    idle = bytes(20)
    for frame in range(frames):
        button = hash_check.BUTTON_A if 60 <= frame < press_until and frame % 30 == 0 else 0
        pads = struct.pack("<H18x", button) + idle * 3
        data += struct.pack("<I", frame) + pads + pads + bytes(40)
    path.write_bytes(data)


def read_report(path):
    """The driver's report: a list of (word, {key: value}, the whole line)."""
    lines = []
    if path.exists():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            word, _, rest = line.partition(" ")
            values = dict(item.split("=", 1) for item in rest.split(" ") if "=" in item)
            lines.append((word, values, line))
    return lines


def said(report, word):
    return [values for w, values, _ in report if w == word]


class LevelLog:
    """What a game leaves behind: the driver's report at report_path and state-hash.txt in the folder run. menu_test.py
    reads its games the same way."""

    def rows(self):
        """Hash rows before the level, then from the frame after Play_Init on, without their frame numbers. In between
        the game is in GSTATE_PLAY but gGameFrameCount still counts the screen it came from."""
        names, rows = hash_check.read_log(self.run / "state-hash.txt")
        running = said(self.report(), "level_running")
        expect(running, f"{self.name}'s game never got to the level")
        first = int(running[0]["frame"])
        return names, [r for r in rows if r[1] != GSTATE_PLAY], [r[1:] for r in rows if r[0] >= first]

    def report(self):
        return read_report(self.report_path)


class Game(LevelLog):
    """One game of a scenario, on a thread of its own: run_game blocks until the replay is over."""

    def __init__(self, folder, name, exe, frames, env, press_until=0):
        self.name = name
        self.report_path = folder / f"{name}.report"
        replay = folder / f"{name}.sfcr"
        write_replay(replay, frames, SEEDS[name], press_until)
        env = {**env, "SF64_COOP_JOIN_REPORT": str(self.report_path)}
        self.run = None
        self.thread = threading.Thread(target=self.play, args=(replay, exe, frames, env))

    def play(self, replay, exe, frames, env):
        _, self.run, _ = replay_tool.run_game("play", replay, exe, max(600, frames), hash_log=True, tag=self.name,
                                              env_extra=env)


def play(folder, exe, frames, picks, host_env=None, client_env=None, press_until=0, client_frames=None):
    """A host and one client for each list of picks. Clients start once the host has its port."""
    folder.mkdir(parents=True, exist_ok=True)
    port_file = folder / "port"
    env = {"SF64_COOP_JOIN_PORT_FILE": str(port_file), "SF64_COOP_JOIN_CLIENTS": str(len(picks)),
           "SF64_COOP_JOIN_LEVEL": str(LEVEL_CORNERIA), "SF64_COOP_JOIN_START_FRAME": str(START_FRAME)}
    host = Game(folder, "host", exe, frames, {**env, **(host_env or {})}, press_until)
    host.thread.start()
    deadline = time.monotonic() + 120
    while not (port_file.exists() and port_file.read_text(encoding="utf-8").strip()):
        expect(host.thread.is_alive() and time.monotonic() < deadline, "the host did not report its port")
        time.sleep(0.2)
    port = port_file.read_text(encoding="utf-8").strip()
    clients = []
    for n, wishes in enumerate(picks):
        env = {"SF64_COOP_JOIN_CONNECT": f"127.0.0.1:{port}", "SF64_COOP_JOIN_PICKS": ",".join(wishes)}
        clients.append(Game(folder, f"client{n}", exe, client_frames or frames, {**env, **(client_env or {})}))
        clients[-1].thread.start()
    for game in [host, *clients]:
        game.thread.join()
        expect(game.run is not None, f"{game.name}: no run")
    return host, clients


def first_difference(names, a, b):
    """The first row of two level logs that differs, by position, as text; None if they agree."""
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            regions = [names[k] for k in range(len(a[i][3])) if a[i][3][k] != b[i][3][k]]
            return f"level frame {i}: {', '.join(regions) or 'state or level'}"
    return None


def check_running(game, running):
    """This game reached the level, with what the host's game holds in running."""
    there = said(game.report(), "level_running")
    expect(len(there) == 1, f"{game.name}'s game never got to the level")
    for key in ("level", "phase", "players", "characters", "seeds", "team_damage", "team_event"):
        expect(there[0][key] == running[key], f"{game.name}: {key} {there[0][key]}, host {running[key]}")


def check_started(host, clients, roster):
    """Everyone heard the same level start, and every game went there with it."""
    hr = host.report()
    starts = said(hr, "level_start")
    expect(len(starts) == 1, f"host: {len(starts)} level starts")
    expect(starts[0]["roster"] == roster, f"host started from {starts[0]['roster']}, want {roster}")
    expect(starts[0]["level"] == str(LEVEL_CORNERIA), f"host started level {starts[0]['level']}")
    running = said(hr, "level_running")
    expect(len(running) == 1, "the host's game never got to the level")
    for game in clients:
        report = game.report()
        theirs = said(report, "level_start")
        expect(len(theirs) == 1, f"{game.name}: {len(theirs)} level starts")
        for key in ("level", "phase", "roster", "values"):
            expect(theirs[0][key] == starts[0][key], f"{game.name}: {key} {theirs[0][key]}, host {starts[0][key]}")
        check_running(game, running[0])
    return starts[0], running[0]


def check_same_level(host, clients):
    """The games were apart before the level, and are the same on every frame in it. Returns the frames compared."""
    names, before, inside = host.rows()
    expect(len(inside) >= FOUND_IN_LEVEL, f"host: only {len(inside)} frames in the level")
    rng = names.index("rng")
    least = len(inside)
    for game in clients:
        _, their_before, their_inside = game.rows()
        n = min(len(before), len(their_before))
        expect(any(before[i][4][rng] != their_before[i][4][rng] for i in range(n)),
               f"{game.name}: same seeds as the host before the level, so this pass proves nothing")
        difference = first_difference(names, inside, their_inside)
        expect(difference is None, f"{game.name} differs from the host at {difference}")
        least = min(least, len(their_inside))
        expect(least >= FOUND_IN_LEVEL, f"{game.name}: only {least} frames in the level")
    return least


# ---- Scenarios. Each returns the text for its summary line. ----

def sc_level(args, folder):
    host, clients = play(folder, args.exe, args.frames, [["falco"]], host_env={"SF64_COOP_JOIN_POKE_TEAM": "1"})
    _, running = check_started(host, clients, "Fox,Falco,-,-")
    expect(running["players"] == "2", f"{running['players']} players in the level")
    expect(running["characters"] == "Fox,Falco,-,-", f"the game holds {running['characters']}")
    expect(running["team_damage"] != "0,0,0,0,0,0", "nothing to carry in gTeamDamage, so this pass proves nothing")
    frames = check_same_level(host, clients)
    return (f"same hash on {frames} frames from the level start, Fox and Falco, seeds {running['seeds']}, "
            f"team_damage {running['team_damage']}")


def sc_from_the_menus(args, folder):
    # The map draws slowly, so a client on the title screen gets far ahead of the host in frames.
    frames = args.frames + MENU_START_FRAME - START_FRAME
    host, clients = play(folder, args.exe, frames, [["peppy"]],
                         host_env={"SF64_COOP_JOIN_START_FRAME": str(MENU_START_FRAME)},
                         press_until=MENU_START_FRAME - 60, client_frames=frames + 3000)
    check_started(host, clients, "Fox,Peppy,-,-")
    _, before, _ = host.rows()
    states = {row[1] for row in before[-30:]}
    expect(states != {2}, f"the host was still on the title screen: game state {states}")
    frames = check_same_level(host, clients)
    return f"host came from game state {sorted(states)}, client from the title screen: same hash on {frames} frames"


def sc_taken(args, folder):
    host, clients = play(folder, args.exe, args.frames, [["falco", "slippy"], ["falco", "slippy"]])
    wants = [said(game.report(), "pick_refused") for game in clients]
    expect(sum(len(w) for w in wants) == 1, f"want one refused pick, got {wants}")
    refused = [w[0]["character"] for w in wants if w]
    expect(refused == ["Falco"], f"refused {refused}")
    _, running = check_started(host, clients, said(host.report(), "level_start")[0]["roster"])
    expect(running["players"] == "3", f"{running['players']} players in the level")
    held = running["characters"].split(",")
    expect(held[0] == "Fox" and sorted(held[1:3]) == ["Falco", "Slippy"], f"the game holds {held}")
    frames = check_same_level(host, clients)
    return f"Falco refused once, then {running['characters']}: same hash on {frames} frames"


def sc_unpicked(args, folder):
    host, clients = play(folder, args.exe, START_FRAME + 1000, [[]])
    report = host.report()
    expect(said(report, "start_refused"), "the host never tried to start")
    expect(not said(report, "level_start"), "the host started with a client that holds nothing")
    expect(not said(clients[0].report(), "level_start"), "the client got a level start")
    return "no level start while the client holds nothing"


def sc_control(args, folder):
    """Can this test fail? The client goes to the level with its own values."""
    host, clients = play(folder, args.exe, START_FRAME + 1000, [["falco"]],
                         host_env={"SF64_COOP_JOIN_POKE_TEAM": "1"}, client_env={"SF64_COOP_JOIN_KEEP_OWN": "1"})
    names, _, inside = host.rows()
    _, _, theirs = clients[0].rows()
    expect(inside and theirs, "the control never reached the level")
    difference = first_difference(names, inside, theirs)
    expect(difference is not None, "the hashes agree although the client kept its own values")
    ours, its = (said(game.report(), "level_running")[0] for game in (host, clients[0]))
    for key in ("seeds", "team_damage", "team_event"):
        expect(ours[key] != its[key], f"{key} agrees although the client kept its own values: {ours[key]}")
    return f"with its own values the client differs at {difference}; seeds and team values differ too"


SCENARIOS = {
    "control": sc_control,
    "unpicked": sc_unpicked,
    "level": sc_level,
    "from_the_menus": sc_from_the_menus,
    "taken": sc_taken,
}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", help="Starship executable (default: newest build found)")
    ap.add_argument("--only", help="scenarios, comma-separated")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--frames", type=int, default=START_FRAME + FOUND_IN_LEVEL + 800,
                    help="length of each game's replay")
    args = ap.parse_args()
    if args.list:
        print("\n".join(SCENARIOS))
        return 0
    args.exe = replay_tool.find_exe(args.exe)
    names = args.only.split(",") if args.only else list(SCENARIOS)
    for name in names:
        if name not in SCENARIOS:
            sys.exit(f"join: no scenario {name}")
    root = replay_tool.ROOT / "local" / f"test-join-{time.strftime('%Y%m%d-%H%M%S')}"
    passed = 0
    for name in names:
        started = time.monotonic()
        try:
            detail = SCENARIOS[name](args, root / name)
            passed += 1
            print(f"ok      {name}: {detail} ({time.monotonic() - started:.0f} s)", flush=True)
        except Fail as failure:
            print(f"FAILED  {name}: {failure} ({time.monotonic() - started:.0f} s)", flush=True)
    print(f"RESULT: {passed}/{len(names)} scenarios passed")
    print(f"logs: {root}")
    return 0 if passed == len(names) else 1


if __name__ == "__main__":
    sys.exit(main())
