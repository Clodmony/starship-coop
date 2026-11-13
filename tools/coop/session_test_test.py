#!/usr/bin/env python3
"""Tests of tools/coop/session_test.py without a build (#309): when level_start_bad_wifi fails, its line says what the
relay dropped, per player and direction and when, and what the host saw: who left, why, and every recovery it started.

The failure is the one of the gate run in #309: three clients through a lossy relay, players 4 and 2 leave while player
3 is still there, so player 3 sees two recoveries and three level starts. The bots' result files and the relay's packet
log are written by the test, as the real ones would be; no process starts.

No build, no game, no ROM; check.py runs it. Exit code 0 when every check holds.
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import session_test  # noqa: E402

ROSTER = ["Fox", "Falco", "Slippy", "Peppy"]
CLOSED = 2  # COOP_SESSION_END_CLOSED, src/port/coop/session/session.h
LEFT_4 = "Player 4 left. Starting the level again together."
LEFT_2 = "Player 2 left. Starting the level again together."
# client n: its UDP port, its player index (0 is the host), the picks the host refused it
CLIENTS = [(40001, 1, []), (40002, 2, ["Falco"]), (40003, 3, ["Falco", "Slippy"])]
# the relay's drops: (client port, direction, time in ms)
DROPS = [(40001, "s2c", 310.5), (40002, "c2s", 880.25), (40002, "c2s", 905.0)]
PACKETS_EACH_WAY = 40


def event(kind, peer=0, player=0, text="", end=0, character="-"):
    return {"type": kind, "peer": peer, "diff": 0, "end": end, "player": player, "character": character, "text": text}


def level_start():
    return {"level": session_test.LEVEL, "phase": session_test.PHASE, "roster": ROSTER,
            "values": session_test.level_values(), "save": session_test.level_save()}


def host_result():
    events = [event("joined", n, n + 1) for n in range(3)]
    events += [event("pick_refused", 1, 0, "Falco is already taken.", character="Falco"),
               event("pick_refused", 2, 0, "Falco is already taken.", character="Falco"),
               event("pick_refused", 2, 0, "Slippy is already taken.", character="Slippy"),
               event("level_start"),
               event("left", 2, end=CLOSED, text="The other side closed the connection."),
               event("recover", 0, 3, LEFT_4), event("level_start"),
               event("left", 0, end=CLOSED, text="The other side closed the connection."),
               event("recover", 0, 1, LEFT_2), event("level_start"),
               event("left", 1, end=CLOSED, text="The other side closed the connection.")]
    return {"role": "host", "port": 39999, "finished": True, "received": 3, "ignored": 0, "rosters": 9,
            "roster": ROSTER, "start_refused": 0, "level_start": level_start(), "events": events}


def client_result(port, player, refused):
    events = [event("joined", 0, player)]
    events += [event("pick_refused", 0, 0, f"{name} is already taken.", character=name) for name in refused]
    events.append(event("level_start"))
    if player == 2:  # the one still there when the others went
        events += [event("recover", 0, 3, LEFT_4), event("level_start"), event("recover", 0, 1, LEFT_2),
                   event("level_start")]
    return {"role": "client", "port": port, "finished": True, "received": 1, "ignored": 0, "rosters": 5,
            "roster": ROSTER, "start_refused": 0, "level_start": level_start(), "events": events}


def write_try(folder):
    """What one try leaves in its folder: the bots' results and the relay's info and packet log."""
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "host.json").write_text(json.dumps(host_result()), encoding="utf-8")
    for n, (port, player, refused) in enumerate(CLIENTS):
        (folder / f"client{n}.json").write_text(json.dumps(client_result(port, player, refused)), encoding="utf-8")
    (folder / "relay.json").write_text(json.dumps({"listen": 39998, "rpc": 39997}), encoding="utf-8")
    lines = [{"format": "coopchaos-packetlog", "version": 1, "profile": "bad_wifi", "seed": 4242, "start_wall": 0}]
    for port, _, _ in CLIENTS:
        for n in range(PACKETS_EACH_WAY):
            for way in ("c2s", "s2c"):
                lines.append({"t": 20.0 * n, "dir": way, "peer": f"127.0.0.1:{port}", "n": n, "len": 40,
                              "head": "00", "verdict": "fwd", "reason": None, "copies": [35.0], "corrupt": False})
    for port, way, t in DROPS:
        lines.append({"t": t, "dir": way, "peer": f"127.0.0.1:{port}", "n": 999, "len": 40, "head": "00",
                      "verdict": "drop", "reason": "burst", "copies": [], "corrupt": False})
    with open(folder / "packets.jsonl", "w", encoding="utf-8") as f:
        f.write("".join(json.dumps(line) + "\n" for line in lines))


def failure_text(tmp):
    """Runs the scenario on the files above. Returns the text of its Fail, or None if it passed."""
    seen = {"c2s": PACKETS_EACH_WAY * 3, "s2c": PACKETS_EACH_WAY * 3}
    stats = {way: {"seen": seen[way] + sum(1 for d in DROPS if d[1] == way), "forwarded": seen[way],
                   "dropped": sum(1 for d in DROPS if d[1] == way), "duplicated": 0} for way in ("c2s", "s2c")}

    def fake_party(sub, picks, via=None, seed=session_test.SEED, client_args=()):
        write_try(sub.folder)
        clients = [json.loads((sub.folder / f"client{n}.json").read_text(encoding="utf-8")) for n in range(len(picks))]
        return host_result(), clients, str(sub.folder / "relay.json")

    real_party, real_stats = session_test.level_party, session_test.chaos.stats
    session_test.level_party = fake_party
    session_test.chaos.stats = lambda info: stats
    try:
        session_test.sc_level_start_bad_wifi(session_test.Run(None, tmp / "level_start_bad_wifi"), None)
        return None
    except session_test.Fail as e:
        return str(e)
    finally:
        session_test.level_party, session_test.chaos.stats = real_party, real_stats


def main():
    checks = 0
    failures = []

    def check(cond, what):
        nonlocal checks
        checks += 1
        if not cond:
            failures.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        text = failure_text(Path(tmp))
    check(text is not None, "the scenario passed with a client that saw three level starts")
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
    order = ["player 4 left (closed)", LEFT_4, "player 2 left (closed)", LEFT_2, "player 3 left (closed)"]
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
