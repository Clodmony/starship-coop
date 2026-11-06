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
   `python tools/coop/replay.py record <file>` (someone plays, closes the window to stop; with `--frames <n>` it stops
   by itself and nobody has to play) and `python tools/coop/replay.py play <file>` (unattended, exit code 0 on `PASS`).
   Each run gets a fresh folder under `local/`, and `SHIP_HOME` is set to that folder whatever your shell says.
   Recording starts at `GSTATE_START`, after the boot logo; each frame stores a checkpoint (game state, level,
   player 0 state and position, RNG seeds) and playback reports the first frame that differs. Replay files hold raw
   structs: same build and platform only, and they stay in `local/` (made from game play, not shared).
   Only a whole file can pass: `FAIL_NO_FRAMES` is a file with a header and nothing else, `FAIL_TRUNCATED` one that
   ends inside a frame (a recording that was killed can), `ERROR_BAD_FILE` one without a valid header. These win over
   `FAIL_MISMATCH`, and `src/port/coop/test/replay.h` lists every result.
   `python tools/coop/replay_test.py` (unattended, exit code 0 on `PASS`, needs a build with assets) tests the tools
   themselves: it records a short replay nobody plays and plays it whole and damaged through `replay.py`,
   `hash_check.py` and `determinism.py`, with a decoy `SHIP_HOME` that must stay untouched.
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
   `python tools/coop/determinism.py <replay>` (WP-02, unattended, exit code 0 on `PASS`) plays the replay once alone
   and then six more times at once, each under a condition that can differ between machines: graphics backend,
   window and frame pacing, audio timing (`SF64_COOP_TEST_AUDIO_SAMPLES`) and C runtime math without FMA3
   (`SF64_COOP_TEST_NO_FMA3`, `src/port/coop/test/switches.h`). Every run's hash must equal the first run's on every
   recorded frame. `--only opengl,no-fma3` picks variants.
3. **Two clients on one machine.** Two Starship processes with their own config, save and log folders (never the
   owner's), connected over loopback, both fed replays.
4. **Network conditions (WP-27).** Lag, loss and reordering between the clients with a chaos relay, as in the hub's
   [network chaos testing](https://github.com/cranebeaker/coop-anything/blob/main/playbook/network-chaos-testing.md).
   The transport on its own already runs through that relay: `python tools/coop/net_test.py` (WP-20), which needs a
   configured build directory but no ROM. WP-27 does the same with real game clients.
   Whether those scenarios can fail at all is checked by `python tools/coop/net_mutants.py`: it breaks one behaviour
   of `net_core.c` at a time and requires the named scenario to fail (#44). Run it after changing `net_core.c`.
   `python tools/coop/session_mutants.py` does the same for `session.c` and `session_test.py` (#55), and
   `python tools/coop/session_test.py --asan` runs the session tests under AddressSanitizer and UBSan (CI does, on
   Linux); `session_mutants.py --asan` shows that run can fail.
5. **Regression per bug.** Every desync fix comes with the replay that triggered it; every transport fix adds a
   break to `tools/coop/net_mutants.py`, every session fix one to `tools/coop/session_mutants.py`.

Running tests must never use or modify the owner's own `starship.cfg.json`, saves, `mods/` folder or running game.

## Results

Newest first. One block per run that matters (a milestone, a release, a QA level change).

```
date:       2026-11-06
commit:     issue/55-session-tests, on a69c811a
identity:   no game, no ROM; Linux, GCC, Debug; 2 or 3 processes, UDP over loopback, chaos relay
what ran:   python tools/coop/session_test.py, then with --asan; python tools/coop/session_mutants.py, then with --asan;
            python tools/coop/net_mutants.py
result:     9/9 scenarios, plain and under AddressSanitizer and UBSan. All 129 session breaks caught (139 break-and-test
            pairs), all 3 sanitizer breaks caught, all 16 transport breaks caught (20 pairs). Handshake and transport
            only, not co-op play.
```

```
date:       2026-11-05
commit:     issue/19-session (WP-21), on 1ec6c677
identity:   no game, no ROM; Windows 11, MSVC 19.44, Debug; 2 or 3 processes, UDP over loopback, chaos relay
what ran:   python tools/coop/session_test.py
result:     9/9 scenarios: virtual network (identity wire form and fuzz, transport endings, 10 mismatch cases, two
            clients, broken and silent peers, 80 lossy runs), then real processes: match, build, ROM, CVar,
            platform warning, two clients, a match through bad_wifi and a refusal through hell. Handshake only;
            the game glue (CoopIdentity_FromGame) compiles but has not run.
```

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
date:       2026-11-05
commit:     issue/2-determinism (WP-02), on top of e8c4e6a2
identity:   ROM US 1.0, Windows 11, MSVC 19.44, Debug, DirectX 11 (OpenGL in one run), 1 client per run, no transport
what ran:   python tools/coop/determinism.py local/replays/corneria.sfcr
result:     PASS. 6/6 variants (base-2, opengl, window, audio-low, audio-high, no-fma3) equal to base on 13545/13545
            frames; 7 replays PASS; 1040 s
```

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
