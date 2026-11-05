# QA

Every PR states the QA level it reached and how. Levels follow Co-op Anything:

| Level | Meaning |
|---|---|
| `none` | Compiles, or nothing was run. Say which. |
| `community-reports` | People played it and reported back, without a test plan. |
| `automated` | A scripted harness drove real game clients and checked shared state. Paste the summary. |
| `verified-playthrough` | A full campaign was finished in co-op, written up with known issues. |

Current project level: `none` (no co-op play yet; the transport has its own tests).

## Test identity

Record with every result: commit hash, ROM version (US 1.0 or 1.1), OS, compiler and version, graphics backend,
number of clients, transport.

## Test plan (to be built in M0 and M2)

1. **Replays (WP-03).** Controller input recorded per game frame and played back. Single-player replays guard the rule
   that co-op off changes nothing: a replay recorded on upstream must give the same per-frame hashes on this fork.
2. **State hashes (WP-04).** One hash per game frame over players, object arrays, RNG seeds and key globals. Two
   clients compare hashes; the first different frame is the desync.
3. **Two clients on one machine.** Two Starship processes with their own config, save and log folders (never the
   owner's), connected over loopback, both fed replays.
4. **Network conditions (WP-27).** Lag, loss and reordering between the clients with a chaos relay, as in the hub's
   [network chaos testing](https://github.com/cranebeaker/coop-anything/blob/main/playbook/network-chaos-testing.md).
   The transport on its own already runs through that relay: `python tools/coop/net_test.py` (WP-20), which needs a
   configured build directory but no ROM. WP-27 does the same with real game clients.
   Whether those scenarios can fail at all is checked by `python tools/coop/net_mutants.py`: it breaks one behaviour
   of `net_core.c` at a time and requires the named scenario to fail (#44). Run it after changing `net_core.c`.
5. **Regression per bug.** Every desync fix comes with the replay that triggered it; every transport fix adds a
   break to `tools/coop/net_mutants.py`.

Running tests must never use or modify the owner's own `starship.cfg.json`, saves, `mods/` folder or running game.

## Results

Newest first. One block per run that matters (a milestone, a release, a QA level change).

```
date:       2026-11-05
commit:     326b4572
identity:   ROM US 1.0 (SHA-1 d8b1088520f7c5f81433292a9258c1184afa1457), Linux, GCC 16.2.1, Debug, OpenGL,
            USE_NETWORKING on
what ran:   boot with a private SHIP_HOME and no input, game state read through gdb every 7 s, one screenshot
result:     title screen at frame 3131, still running at frame 3762, no crash. Boot only: nothing calls the
            transport yet, so no socket was opened inside the game.
```

```
date:       2026-11-05
commit:     fdd01ee1
identity:   no game, no ROM; CI on Linux (GCC) and Windows (MSVC), Release; 2 or 3 processes, UDP over loopback, chaos relay
what ran:   python tools/coop/net_test.py --config Release
result:     17/17 scenarios on both. Transport only, not co-op play.
```

```
date:       2026-11-05
commit:     12329623
identity:   no game, no ROM; Linux, GCC, Debug; 2 or 3 processes on one machine, UDP over loopback, chaos relay
what ran:   python tools/coop/net_test.py
result:     17/17 scenarios: virtual network with 200 seeds, every relay profile, corruption, outages, timeouts,
            killed client, full host, two clients, junk datagrams. Transport only, not co-op play.
```

```
date:       YYYY-MM-DD
commit:     <hash>
identity:   <ROM version, OS, compiler, backend>
what ran:   <command>
result:     <pass/fail counts, first failing frame>
```
