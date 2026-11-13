#!/usr/bin/env python3
"""Tests of tools/coop/session_test.py without a build.

- `check_level`'s counting rule (#314): a client's further level starts count only when each follows a recover for a
  player the host saw leave with `closed`; the first is the host's. The shape of #309's gate failure (players 4 and 2
  closed their sessions while player 3 was still there, so player 3 saw two recoveries and three level starts) passes;
  a restart nobody left for, a recover for a player who timed out or never left, one leave counted twice, and a first
  level start that is not the host's fail.
- The report of a failed relay scenario (#309): when `level_start_bad_wifi` fails, its line goes on with what the
  relay dropped, per player and direction and when, what the host saw (who left, why, every recovery it started) and
  each client's events. The bots' result files and the
  relay's packet log are written by the test, as the real ones would be; no process starts.

No build, no game, no ROM; check.py runs it. Exit code 0 when every check holds.
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import session_test  # noqa: E402

ROSTER = ["Fox", "Falco", "Slippy", "Peppy"]
CLOSED, TIMEOUT = 2, 3  # COOP_SESSION_END_CLOSED, COOP_SESSION_END_TIMEOUT, src/port/coop/session/session.h
# client n: its UDP port, its player index (0 is the host), the picks the host refused it
CLIENTS = [(40001, 1, []), (40002, 2, ["Falco"]), (40003, 3, ["Falco", "Slippy"])]
STAYER = 2  # player index of the client still there when the others went: "player 3"
# the relay's drops: (client port, direction, time in ms)
DROPS = [(40001, "s2c", 310.5), (40002, "c2s", 880.25), (40002, "c2s", 905.0)]
PACKETS_EACH_WAY = 40
HOST_PORT = 39999


def event(kind, peer=0, player=0, text="", end=0, character="-"):
    return {"type": kind, "peer": peer, "diff": 0, "end": end, "player": player, "character": character, "text": text}


def left_text(player):
    return f"Player {player + 1} left. Starting the level again together."


def recover(player):
    return event("recover", 0, player, left_text(player))


# #309's gate: the host saw player 4 (peer 2), then player 2 (peer 0), then player 3 (peer 1) leave, each `closed`.
GATE_LEAVES = ((2, CLOSED), (0, CLOSED), (1, CLOSED))
# What player 3 saw after the host's level start in that failure.
GATE_STAYED = [recover(3), event("level_start"), recover(1), event("level_start")]


def level_start(roster=ROSTER):
    return {"level": session_test.LEVEL, "phase": session_test.PHASE, "roster": list(roster),
            "values": session_test.level_values(), "save": session_test.level_save()}


def host_result(leaves=GATE_LEAVES):
    """The host: three joins, three refused picks, the level start, then each leave; after each but the last, the
    recovery and the level start again for whoever is still there (Host_Left, session.c)."""
    events = [event("joined", n, n + 1) for n in range(3)]
    events += [event("pick_refused", 1, 0, "Falco is already taken.", character="Falco"),
               event("pick_refused", 2, 0, "Falco is already taken.", character="Falco"),
               event("pick_refused", 2, 0, "Slippy is already taken.", character="Slippy"),
               event("level_start")]
    for k, (peer, end) in enumerate(leaves):
        events.append(event("left", peer, end=end, text="The other side closed the connection."))
        if k < len(leaves) - 1:
            events += [recover(peer + 1), event("level_start")]
    return {"role": "host", "port": HOST_PORT, "finished": True, "received": 3, "ignored": 0, "rosters": 9,
            "roster": ROSTER, "start_refused": 0, "level_start": level_start(), "events": events}


def client_result(port, player, refused, stayed=GATE_STAYED):
    events = [event("joined", 0, player)]
    events += [event("pick_refused", 0, 0, f"{name} is already taken.", character=name) for name in refused]
    events.append(event("level_start"))
    if player == STAYER:
        events += stayed
    return {"role": "client", "port": port, "finished": True, "received": 1, "ignored": 0, "rosters": 5,
            "roster": ROSTER, "start_refused": 0, "level_start": level_start(), "events": events}


def level_clients(stayed=GATE_STAYED):
    return [client_result(port, player, refused, stayed) for port, player, refused in CLIENTS]


def level_failure(h, clients):
    """check_level on these results: the text of its Fail, or None if it passed."""
    try:
        session_test.check_level(h, clients)
        return None
    except session_test.Fail as e:
        return str(e)


def write_packets(folder, ports):
    lines = [{"format": "coopchaos-packetlog", "version": 1, "profile": "bad_wifi", "seed": 4242, "start_wall": 0}]
    for port in ports:
        for n in range(PACKETS_EACH_WAY):
            for way in ("c2s", "s2c"):
                lines.append({"t": 20.0 * n, "dir": way, "peer": f"127.0.0.1:{port}", "n": n, "len": 40,
                              "head": "00", "verdict": "fwd", "reason": None, "copies": [35.0], "corrupt": False})
    for port, way, t in DROPS:
        if port in ports:
            lines.append({"t": t, "dir": way, "peer": f"127.0.0.1:{port}", "n": 999, "len": 40, "head": "00",
                          "verdict": "drop", "reason": "burst", "copies": [], "corrupt": False})
    with open(folder / "packets.jsonl", "w", encoding="utf-8") as f:
        f.write("".join(json.dumps(line) + "\n" for line in lines))


def write_try(folder, host, clients, names):
    """What one try leaves in its folder: the bots' results and the relay's info and packet log."""
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "host.json").write_text(json.dumps(host), encoding="utf-8")
    for name, c in zip(names, clients):
        (folder / f"{name}.json").write_text(json.dumps(c), encoding="utf-8")
    (folder / "relay.json").write_text(json.dumps({"listen": 39998, "rpc": 39997}), encoding="utf-8")
    write_packets(folder, [c["port"] for c in clients])
    return str(folder / "relay.json")


def stats_of(ports):
    seen = {way: PACKETS_EACH_WAY * len(ports) for way in ("c2s", "s2c")}
    dropped = {way: sum(1 for d in DROPS if d[1] == way and d[0] in ports) for way in ("c2s", "s2c")}
    return {way: {"seen": seen[way] + dropped[way], "forwarded": seen[way], "dropped": dropped[way], "duplicated": 0}
            for way in ("c2s", "s2c")}


def scenario_failure(tmp, name, host, clients):
    """Runs the scenario `name` on the results given, through the files above. The text of its Fail, or None."""
    ports = [c["port"] for c in clients]

    def fake_party(sub, picks, via=None, seconds=60, seed=session_test.SEED, host_args=(), client_args=()):
        info = write_try(sub.folder, host, clients, [f"client{n}" for n in range(len(clients))])
        return host, clients, info

    def fake_level_party(sub, picks, via=None, seed=session_test.SEED, client_args=()):
        return fake_party(sub, picks, via, seed=seed)

    def fake_pair(sub, host_args=(), client_args=(), via=None, seconds=60, seed=session_test.SEED):
        info = write_try(sub.folder, host, clients, ["client"])
        return host, clients[0], info

    real = session_test.party, session_test.level_party, session_test.pair, session_test.chaos.stats
    session_test.party, session_test.level_party, session_test.pair = fake_party, fake_level_party, fake_pair
    session_test.chaos.stats = lambda info: stats_of(ports)
    try:
        session_test.SCENARIOS[name](session_test.Run(None, tmp / name), None)
        return None
    except session_test.Fail as e:
        return str(e)
    finally:
        session_test.party, session_test.level_party, session_test.pair, session_test.chaos.stats = real


def main():
    checks = 0
    failures = []

    def check(cond, what):
        nonlocal checks
        checks += 1
        if not cond:
            failures.append(what)

    # ---- check_level's counting rule (#314) ----
    text = level_failure(host_result(), level_clients())
    check(text is None, f"#309's gate shape (two recovers for `closed` leaves, three level starts) failed: {text}")
    two_at_once = [recover(3), recover(1), event("level_start")]  # both leaves before the host started again
    text = level_failure(host_result(), level_clients(two_at_once))
    check(text is None, f"two recovers before one restart failed: {text}")
    text = level_failure(host_result(), level_clients([recover(3)]))
    check(text is None, f"a recover the client left before the restart of failed: {text}")
    negatives = {
        "a restart nobody left for": (host_result(), level_clients([event("level_start")])),
        "a second restart after one recover": (host_result(), level_clients([recover(3), event("level_start"),
                                                                             event("level_start")])),
        "one leave counted for two restarts": (host_result(), level_clients([recover(3), event("level_start"),
                                                                             recover(3), event("level_start")])),
        "a recover for a player who timed out": (host_result(((2, TIMEOUT), (0, CLOSED), (1, CLOSED))),
                                                 level_clients()),
        "a recover for a player who never left": (host_result(((0, CLOSED), (1, CLOSED))),
                                                  level_clients([recover(3), event("level_start")])),
    }
    for what, (h, clients) in negatives.items():
        text = level_failure(h, clients)
        check(text is not None, f"check_level passed {what}")
        check(text is None or ("client 1" in text and "follows no recover" in text),
              f"the failure for {what} does not name client 1 and the restart: {text}")
    clients = level_clients()
    clients[STAYER - 1]["level_start"] = level_start(["Fox", "Falco", "Slippy", "-"])  # it saw only a restart
    text = level_failure(host_result(), clients)
    check(text is not None and "client 1 started from" in text, f"a first level start not the host's passed: {text}")
    clients = level_clients()
    clients[0]["events"] = [e for e in clients[0]["events"] if e["type"] != "level_start"]
    text = level_failure(host_result(), clients)
    check(text is not None and "client 0 saw no level start" in text, f"a client without a level start: {text}")

    # ---- the report of a failed level_start_bad_wifi (#309): a restart nobody left for ----
    with tempfile.TemporaryDirectory() as tmp:
        stayed = [recover(3), event("level_start"), event("level_start")]
        text = scenario_failure(Path(tmp), "level_start_bad_wifi", host_result(), level_clients(stayed))
    check(text is not None, "level_start_bad_wifi passed with a restart nobody left for")
    text = text or ""
    lines = text.splitlines()
    check(lines and "level_start" in lines[0], f"the first line is no longer the check that failed: {lines[:1]}")
    # The relay: the whole count, and per player, way and time.
    check("relay dropped 3 of 243" in text, f"the relay's whole count is missing: {text!r}")
    relay = [line for line in lines if line.lstrip().startswith("player 3") and "relay" in line]
    check(len(relay) == 1, f"no relay line for player 3: {text!r}")
    relay = relay[0] if relay else ""
    check("to the host 2 of 42" in relay and "880 ms" in relay and "905 ms" in relay,
          f"player 3's drops to the host, with their times, are missing: {relay!r}")
    check("from the host 0 of 40" in relay, f"player 3's drops from the host are missing: {relay!r}")
    check(any("player 2" in line and "from the host 1 of 41" in line and "310 ms" in line for line in lines),
          f"player 2's drop from the host is missing: {text!r}")
    check(any("player 4" in line and "relay dropped nothing" in line for line in lines),
          f"player 4's clean link is not said: {text!r}")
    # The host: who left and why, and each recovery, in order.
    host = [line for line in lines if line.lstrip().startswith("host saw")]
    check(len(host) == 1, f"no line of what the host saw: {text!r}")
    host = host[0] if host else ""
    order = ["player 4 left (closed)", left_text(3), "player 2 left (closed)", left_text(1), "player 3 left (closed)"]
    at = [host.find(part) for part in order]
    check(all(n >= 0 for n in at) and at == sorted(at), f"the host's leaves and recoveries, in order: {host!r}")
    # Which client failed: each client's events, by player.
    check(any(line.lstrip().startswith("player 3 saw") and line.count("level_start") == 3 for line in lines),
          f"player 3's own events are missing: {text!r}")

    if failures:
        print("session_test_test: FAILED\n  " + "\n  ".join(failures))
        return 1
    print(f"session_test_test: ok, {checks} checks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
