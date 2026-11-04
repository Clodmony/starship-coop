# QA

Every PR states the QA level it reached and how. Levels follow Co-op Anything:

| Level | Meaning |
|---|---|
| `none` | Compiles, or nothing was run. Say which. |
| `community-reports` | People played it and reported back, without a test plan. |
| `automated` | A scripted harness drove real game clients and checked shared state. Paste the summary. |
| `verified-playthrough` | A full campaign was finished in co-op, written up with known issues. |

Current project level: `none` (no co-op code yet).

## Test identity

Record with every result: commit hash, ROM version (US 1.0 or 1.1), OS, compiler and version, graphics backend,
number of clients, transport.

## Test plan (to be built in M0 and M2)

1. **Replays (WP-03).** Controller input recorded per game frame and played back. Single-player replays guard the rule
   that co-op off changes nothing: a replay recorded on upstream must give the same per-frame hashes on this fork.
   `python tools/coop/replay.py record <file>` (someone plays, closes the window to stop) and
   `python tools/coop/replay.py play <file>` (unattended, exit code 0 on `PASS`). Each run gets a fresh folder under
   `local/`. Recording starts at `GSTATE_START`, after the boot logo; each frame stores a checkpoint (game state, level,
   player 0 state and position, RNG seeds) and playback reports the first frame that differs. Replay files hold raw
   structs: same build and platform only, and they stay in `local/` (made from game play, not shared).
2. **State hashes (WP-04).** One hash per game frame over players, object arrays, RNG seeds and key globals. Two
   clients compare hashes; the first different frame is the desync. `SF64_COOP_HASH_LOG=<file>` makes the game write
   one line per frame (`src/port/coop/sync/state_hash.h`): frame, game state, level, total hash and one hash per
   region (players, actors, bosses, effects, items, scenery, sprites, scenery360, shots, misc, rng, globals). Frame 0
   is the same frame as in the replay, so logs from two runs of one replay line up. Pointers are normalised or left
   out, so two runs of the same build give equal logs despite address randomisation.
   `python tools/coop/hash_check.py run <replay>` (unattended, exit code 0 on `PASS`) plays the replay three times:
   two plain runs must give equal hashes on every recorded frame, and a third run with `SF64_COOP_HASH_POKE=<frame>`
   (one bit of game state flipped) must first differ at exactly that frame.
   `python tools/coop/hash_check.py diff <a> <b>` names the first differing frame and the regions that differ there.
   `python tools/coop/replay.py play <replay> --hash-log` writes a log next to an ordinary playback.
3. **Two clients on one machine.** Two Starship processes with their own config, save and log folders (never the
   owner's), connected over loopback, both fed replays.
4. **Network conditions (WP-27).** Lag, loss and reordering between the clients with a chaos relay, as in the hub's
   [network chaos testing](https://github.com/cranebeaker/coop-anything/blob/main/playbook/network-chaos-testing.md).
5. **Regression per bug.** Every desync fix comes with the replay that triggered it.

Running tests must never use or modify the owner's own `starship.cfg.json`, saves, `mods/` folder or running game.

## Results

Newest first. One block per run that matters (a milestone, a release, a QA level change).

```
date:       2026-11-04
commit:     issue/4-state-hash (WP-04), on top of 070256bd
identity:   ROM US 1.0, Windows 11, MSVC 19.44, Debug, DirectX 11, 1 client per run, no transport
what ran:   python tools/coop/hash_check.py run local/replays/corneria.sfcr
result:     PASS. A = B on 13545/13545 frames; poke at 6772 first seen at 6772 (actors); 3 replays PASS; 467 s
```

```
date:       YYYY-MM-DD
commit:     <hash>
identity:   <ROM version, OS, compiler, backend>
what ran:   <command>
result:     <pass/fail counts, first failing frame>
```
