# QA

Every PR states the QA level it reached and how. Levels follow Co-op Anything:

| Level | Meaning |
|---|---|
| `none` | Compiles, or nothing was run. Say which. |
| `community-reports` | People played it and reported back, without a test plan. |
| `automated` | A scripted harness drove real game clients and checked shared state. Paste the summary. |
| `verified-playthrough` | A full campaign was finished in co-op, written up with known issues. |

`automated` needs at least two game clients sharing state. A run of one game is `none`, even when a script drove it and
checked its state: the PR says what ran.

Current project level: `automated`. Two real game clients in one session, on one PC, stay equal on every frame through all of Corneria (`python tools/coop/lockstep_test.py`). Nobody has played it, and it has not run on two PCs.

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
   `python tools/coop/hash_check.py run --players 2 --poke-player 1` does the same with two co-op players and the bit
   flipped in player 2. Without a replay file, `run` and `probe` play input by rule (A every 30 frames, one L into
   all-range with `gDebugJumpToAllRange`), so they need no recording. Such a replay has no checkpoints and reports
   `FAIL_MISMATCH` at frame 0, as does any co-op run, so those runs only have to play through.
   `python tools/coop/hash_check.py probe --players 2` (one game, unattended, exit code 0 on `PASS`): every 200 frames
   of a level the game changes single values, hashes, changes them back and logs what the hash did
   (`SF64_COOP_HASH_PROBE`, `# probe` lines in the log). Each player, parked or flying, must change `players` and
   nothing else; two negative `iwork` numbers must differ; two foreign pointers must not; an active `gScenery360` slot
   must show and a free one must not. Every per-player array the hash covers, all four slots, must show for a player
   in the level and not for anyone else, and so must each co-op slot. Both hashes come from one frame of one process,
   so this does not need two runs to agree. With `--players 1` (co-op off) it checks that single player kept the old
   rules: the arrays that are new in co-op must not show, `gLifeCount`, `gBombCount` and `gPlayerScores` must, in every
   slot.
   `python tools/coop/hash_check.py views` (unattended, exit code 0 on `PASS`) plays one replay with
   `SF64_COOP_TEST_LOCAL_INDEX` 0 and 1, in Corneria's rails and its all-range section: two clients of one session, who
   draw different cameras and must still hash alike on every frame. It names the first differing frame and regions.
   `python tools/coop/hash_check.py builds --debug <exe> --release <exe>` (unattended, exit code 0 on `PASS`) plays the
   same two sections on two Release runs and a Debug run of one commit, and all three must hash alike on every frame.
   `python tools/coop/hash_check.py rates --exe <exe>` (unattended, exit code 0 on `PASS`) does the same across
   `gInterpolationFPS`, which no handshake compares: 30 (no interpolated draw), 60, 120, 144 (not a multiple of the
   game's 30) and 360 (the top of the menu's slider). Vsync is off, because it clamps the rate without a word, and a run
   that did not log the draws its rate asks for fails (`# draws` lines in the log).
   `python tools/coop/hash_check.py draw` (unattended, exit code 0 on `PASS`) plays the same two sections with and
   without `SF64_COOP_TEST_NO_DRAW`, from each of the three starts below, and both must hash alike on every frame, with no
   draws in one and draws in the other (#147). `python tools/coop/hash_check.py base --base <exe>` (unattended, exit code 0
   on `PASS`) plays input by rule through the menus with co-op off on this build and on the build before a change, and they
   must hash alike on every frame.
   `python tools/coop/view_check.py` (unattended, exit code 0 on `PASS`) plays the same two clients with buttons and
   pokes that give the two players different numbers, and checks what each one shows and hears from a view trace
   (`src/port/coop/test/view_trace.h`): camera, shield gauge, bomb counter, cockpit, hit flash, shield bar, sound, pause.
   The trace holds what a client means to show. What reached the screen needs a picture: a game with no window can be
   read back from OpenGL, and such pictures are game art, so they stay local and are deleted after reading.
   `python tools/coop/hash_check.py diff <a> <b>` compares every column of the two logs, names the first differing
   frame and the regions that differ there. It compares the common frames: read its first line for the two lengths.
   `python tools/coop/desync_report.py diff <a> <b>` (WP-23) does the same for two desync reports, the files a game
   writes into its own folder when two clients went out of step (`docs/coop/PROTOCOL.md`, Desync check): it checks each
   dump against the region hashes in its header, then names the regions that differ and the first value that does, by
   chunk and byte offset. `show <report>` prints one report's header and what its dump holds.
   `python tools/coop/replay.py play <replay> --hash-log` writes a log next to an ordinary playback.
   `python tools/coop/determinism.py <replay>` (WP-02, unattended, exit code 0 on `PASS`) plays the replay once alone
   and then six more times at once, each under a condition that can differ between machines: graphics backend,
   window and frame pacing, audio timing (`SF64_COOP_TEST_AUDIO_SAMPLES`) and C runtime math without FMA3
   (`SF64_COOP_TEST_NO_FMA3`, `src/port/coop/test/switches.h`). Every run's hash must equal the first run's on every
   recorded frame. `--only opengl,no-fma3` picks variants.
   `python tools/coop/full_level.py` (#142, unattended, exit code 0 on `PASS`) is a whole level with 2 players: a pilot
   that reads game state (`src/port/coop/test/pilot.h`) flies Corneria from level start to the map once and writes a
   replay, which two games then play with no pilot, one of them drawing and one not (#147). `gCoopDebugPlayerCount` is the only CVar set, as there is no other
   way yet to have a second player without a session. Both must `PASS` every checkpoint, and their hash logs must equal
   each other's and the pilot's on every frame. A third game flips a bit of player 2 in all-range, and that must show at
   exactly its frame. Each run is one game: this says nothing about two clients.
   `python tools/coop/input_test.py` (WP-14, unattended, exit code 0 on `PASS`) checks who steers whom. `SF64_COOP_TEST_PAD=<file>`
   makes the game attach an SDL virtual gamepad that presses what the file says (`src/port/coop/test/test_pad.h`), so
   the live local controller is tested on the road a real pad takes, with nobody at the controls.
   `SF64_COOP_REPLAY_LIVE_LOCAL=1` makes a replay fly every player but the local one (`src/port/coop/test/replay.h`).
   The `unit` scenario is the only test of a local index other than 0, until the game can have one.
   `determinism.py` runs in real time, as pacing and audio timing are what it varies. Its variant `speed` plays as the
   other tools do, with no frame limit, and has to give the same hash (#137); without a replay file the tool plays input
   by rule, through Corneria's rails into all-range. Tests count frames, so the tools run a game with
   `SF64_COOP_TEST_SPEED=max`, silent and on Linux without a window (`game_env` in `tools/coop/replay.py`). A tool that
   needs wall-clock time keeps real time and says why. At test speed each game frame is drawn once, so frame
   interpolation does not run and those tools cannot see it. `determinism.py` and `hash_check.py builds` and `rates`
   are the tests that play with it on, and `speed` against the base of `determinism.py` is interpolation off against on.
   Most tools do not test the menus, so a game can skip them (#148): `SF64_COOP_TEST_START_LEVEL=<level>` goes straight to
   a level and `SF64_COOP_TEST_START_SKIP_INTRO=1` past its intro too (`src/port/coop/test/start_level.h`). `Start` in
   `tools/coop/replay.py` holds these three, with Corneria's frames, and input by rule counts from when player 0 flies, so
   one rule fits all three:

   | Start | Level frame 0 | Player 0 flies | Who plays it |
   |---|---|---|---|
   | `menus` | 3101 | 4775 | `determinism.py`, `full_level.py`, `join_test.py`, `menu_test.py`, `exit_test.py`, `replay_test.py`, `hash_check.py builds` and `rates`, `wingman_check.py sitout` and `chain` |
   | `level` | 10 | 1684 | `hash_check.py probe`, `player_trace.py rails`, `view_check.py` |
   | `flying` | 10 | 14 | `hash_check.py run` and `views`, `player_trace.py shots`, `lowhealth` and `effects`, `input_test.py`, `wingman_check.py alive` and `down` (in Sector X) |

   A level's intro is real simulation, and only `menus` and `level` run it. `level` from `START_SEEDS` is the game `menus`
   plays, frame for frame: `python tools/coop/hash_check.py start` (#148, unattended, exit code 0 on `PASS`) plays through
   the menus, starts twice in the level from the seeds that game entered it with, and needs the two starts equal on every
   frame and each equal to the game from the menus in every region from level frame 1 on (with `--players 1` but for
   `scenery360`, whose free slots hold what earlier screens left). `flying` is another flight: what it leaves out is in
   NOTES.md, 2026-11-07, #148. `determinism.py`, `full_level.py`, `join_test.py` and `menu_test.py`
   keep the whole path from boot, and fail on a game that started in a level (`hash_check.whole_path_problem`), as
   `replay_test.py` shows for the first two. `builds` and `rates` keep it as well: frame interpolation first went wrong in
   the option menu (#141), and only they play it in Release and at many rates. A replay with checkpoints holds the frames it
   was made on, so it plays from where it was made, wherever the tool's own input by rule would start.
   A test game does not draw either (`SF64_COOP_TEST_NO_DRAW`, #147): its draw code runs, as that is part of the
   simulation, and the renderer gets an empty display list, while ImGui, the window, the swap and the frame limit go on.
   A tool that needs pixels or interpolation asks for them (`DRAWN`, `REAL_TIME`), says why, and fails if it did not get
   them: `determinism.py`, `hash_check.py builds` and `rates`, `exit_test.py`, `version_test.py`. `replay_test.py` shows
   each of those fails without.
   `python tools/coop/test_all.py` (#147, unattended) runs every test here in lanes, on a Debug build or with
   `--config Release` on a Release build in a folder of its own. Exit code 0 only when every step ran and passed.
3. **Two clients on one machine.** Two Starship processes with their own config, save and log folders (never the
   owner's), connected over loopback, both fed replays. `python tools/coop/join_test.py` (WP-24, unattended, exit code
   0 only when everything passed) takes two or three games from the lobby into a level through the scripted flow in
   `src/port/coop/test/join_driver.h`. Each game starts from other seeds, and its hash log must then equal the host's
   on every frame from the first frame in the level. `control` lets a client keep its own values and must differ.
   `python tools/coop/lockstep_test.py` (WP-22, unattended, exit code 0 only when everything passed) is two clients
   playing one game in lockstep. Each game takes its input from `src/port/coop/test/lockstep_driver.h`, which writes
   port 0, so from there on input goes the road a pad's goes. The hash logs from the first frame in the level and a log of
   every player's input per frame must be equal on every frame; `control` runs the same with no lockstep and must
   differ. `sim` is `lockstep.c` alone in a virtual network, and `python tools/coop/lockstep_mutants.py` shows that can
   fail. The rest are in the tool: steering, three games, pause, what a save or a zero seed could do, a slow client, lag
   through the chaos relay, another input delay, and every way of losing a player, through the join driver and through
   the real menu. `--only full` flies all of Corneria with a pilot on each client, repeats the flight with plain games,
   and flips a bit on one client, which must show at exactly its frame. Its log feeds `--only menu,release,rates,audio
   --feed <log>`: the same flight through the real menu, on a Release build, and with the clients at different
   `gInterpolationFPS` or audio sample counts.
   The desync check (WP-23) is tested there too: `desync_sim` is `desync.c` alone in a virtual network
   (`tools/coop/desynctest`), and `python tools/coop/desync_mutants.py` shows that can fail. `desync` flips a bit of
   player 2 on one client, on rails and in all-range: both games must write a report naming that frame, the pair and
   the region `players`, end the session within a few frames with the text for the player, and `desync_report.py diff`
   on the two reports must name `gPlayer[1]`. `desync_three` does it with a third game that stays in step, and
   `desync_control` with `SF64_COOP_TEST_NO_DESYNC_CHECK`: the hashes differ and nobody says so. The poke in `full`
   is checked the same way, and every scenario that compares two games also checks that neither wrote a report.
4. **Network conditions (WP-27).** Lag, loss and reordering between the clients with a chaos relay, as in the hub's
   [network chaos testing](https://github.com/cranebeaker/coop-anything/blob/main/playbook/network-chaos-testing.md).
   The transport on its own already runs through that relay: `python tools/coop/net_test.py` (WP-20), which needs a
   configured build directory but no ROM. WP-27 does the same with real game clients.
   Whether those scenarios can fail at all is checked by `python tools/coop/net_mutants.py`: it breaks one behaviour
   of `net_core.c` at a time and requires the named scenario to fail (#44). Run it after changing `net_core.c`.
   `python tools/coop/session_mutants.py` does the same for `session.c` and `session_test.py` (#55; `test_all.py` runs
   it), and `python tools/coop/session_test.py --asan` runs the session tests under AddressSanitizer and UBSan (`test_all.py`
   does, on Linux); `session_mutants.py --asan` shows that run can fail. `python tools/coop/net_test.py --asan` does
   the same for the transport tests (#75; `test_all.py` does), and `net_mutants.py --asan` shows that run can fail
   (#92).
   The Steam relay transport (WP-26) runs without Steam: `python tools/coop/steam_test.py` puts it on a fake of
   `ISteamNetworkingMessages` in a virtual network (no SDK, no ROM, no sockets), with the chaos relay's profiles, and
   then runs its real backend on a mock Steam library in two processes. `--asan` runs it under AddressSanitizer and
   UBSan, and `python tools/coop/steam_mutants.py` shows the scenarios can fail. This proves the transport keeps its
   promises under the rules Steam documents, not that Steam behaves that way. A real run takes two PCs and two Steam
   accounts, so it is the owner's to start: `python tools/coop/steam_test.py --live host --library <steam library>`,
   then `--live client --library <steam library> --connect <the SteamID the host printed>`. Its result files hold
   SteamIDs, so they stay in `local/`.
5. **The co-op menu (WP-25).** `python tools/coop/menu_test.py` checks the menu layer with no game: `sim` on the real
   session in a virtual network, `udp` over real sockets. `python tools/coop/menu_test.py --game` starts three real
   games with `SF64_COOP_TEST_MENU=<folder>` (`src/port/coop/test/menu_driver.h`), clicks the real ImGui widgets through
   ImGui's input queue and reads back what each window holds, so it needs nobody at the screen. Its `game_level` and
   `game_level_map` scenarios (#126) go on to the Start button and compare the state hash logs from the first frame after
   `Play_Init`, as `join_test.py` does. Its `game_platform` scenario reads what the menu shows for a platform refusal and
   runs `SF64_COOP_TEST_ANY_PLATFORM`, against `coop_session_test` as the other side, because two games on one PC never
   differ in platform. Neither says how the window looks.
6. **Regression per bug.** Every desync fix comes with the replay that triggered it; every transport fix adds a
   break to `tools/coop/net_mutants.py`, every session fix one to `tools/coop/session_mutants.py`.

Running tests must never use or modify the owner's own `starship.cfg.json`, saves, `mods/` folder or running game.

## Results

Newest first. One block per run that matters (a milestone, a release, a QA level change).

```
date:       2026-11-08
commit:     issue/145-windows-release, game code at 4283e076
identity:   ROM US 1.0, Windows 11, MSVC 19.44 (14.44.35207), Release and Debug from build.py (Visual Studio 2022),
            DirectX 11, drawn in real time, 2 players by gCoopDebugPlayerCount, input by rule through the menus, no
            transport
what ran:   python tools/coop/hash_check.py builds --debug build/x64/Debug/Starship.exe
            --release build/x64/Release/Starship.exe; python tools/coop/hash_check.py rates
            --exe build/x64/Release/Starship.exe (30, 60, 120, 144, 360 fps)
result:     PASS on both. builds: two Release runs equal, and Debug equal to Release, on 10500/10500 rail frames and
            8000/8000 all-range frames. rates: 60, 120, 144 and 360 fps each equal to 30 fps on all 10500 rail and
            all 8000 all-range frames, every run with the draws its rate asks for. Each run's replay result is
            FAIL_MISMATCH at frame 0, as for every co-op run (see Test plan). About 35 min in all
```

```
date:       2026-11-06
commit:     issue/10-rails-player2, game code at e3aaaa4a, on 95962d04
identity:   ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window, 1 client per run, no transport,
            input by rule (Corneria's rail section into all-range, 10500 frames), players 2 to 4 from the replay file
what ran:   python tools/coop/player_trace.py rails --players 2, and --players 4; hash_check.py run <rail replay>
            --players 2 --poke-frame 5400 --poke-player 1; one plain 1-player run of 95962d04 and of this commit,
            compared with hash_check.py diff; player_trace.py run with input by rule into all-range; hash_check.py run
            --players 2 --poke-player 1 and probe --players 2
result:     PASS on all. Rail section 4525 frames, 2 and 4 players: gPathProgress, gPathVelZ and gPathTexScroll equal
            single player on every frame, largest difference 0.000, path speed 20 to 70, section ends on the same frame.
            Hash, 2 players on rails: A = B on 10500/10500 frames, poke in player 2 at 5400 first seen at 5400
            (players). Co-op off: 10512/10512 common log rows equal to 95962d04. Before: player 1 parked for all 4598
            rail frames, FAIL. Without CoopPlayers_FollowPath: gPathProgress matches on 26 of 4525 frames, FAIL.
```

```
date:       2026-11-06
commit:     issue/53-hash-players at 47a90af5, on a69c811a
identity:   ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window, 1 client per run, no transport,
            input by rule (Corneria, all-range, 5800 frames)
what ran:   python tools/coop/hash_check.py probe --players 2, probe --players 1, run --players 2 --poke-player 1, run;
            one plain 1-player run of a69c811a and of this commit, compared with hash_check.py diff
result:     PASS on all four. Probes 2 players: player 1 14/14 (10 parked, 4 flying), player 0 14/14, iwork 14/14 twice,
            scenery360 5/5 twice. A = B on 5800/5800 frames with 1 and with 2 players; poke in player 2 at 5400 first
            seen at 5400 (players); poke in an actor at 5400 first seen at 5400 (actors). Co-op off: 5801/5801 log rows
            equal to a69c811a, every region. Same checks on the test commit 54837e0e: FAIL, quoted in the PR.
```

```
date:       2026-11-06
commit:     issue/67-hash-player-arrays at 14a353d2, on 95962d04
identity:   ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window, 1 client per run, no transport,
            input by rule (Corneria, all-range, 5800 frames)
what ran:   python tools/coop/hash_check.py probe --players 2, probe --players 4, probe --players 1,
            run --players 2 --poke-player 1, run; two plain 1-player runs of 95962d04 and of this commit, compared in
            pairs with hash_check.py diff
result:     PASS on all five. Per-player arrays, co-op slots, team arrays: all seen for players in the level, 14/14 and
            4/4; parked players, players who do not exist and unread slots leave the hash alone. A = B on 5800/5800
            frames with 1 and with 2 players; poke in player 2 at 5400 first seen at 5400 (players). Co-op off: no
            difference on 5821 and on 5809 common rows, every column, each past the 5800 recorded frames. Same probe on
            the test commit edca28d4: FAIL, 64 checks, quoted in the PR.
```

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
date:       2026-11-06
commit:     issue/58-session-characters (WP-34), on 95962d04
identity:   no game, no ROM; Linux, GCC, Debug; 2 to 4 processes, UDP over loopback, chaos relay
what ran:   python tools/coop/session_test.py, then with --asan; python tools/coop/session_mutants.py, then with --asan;
            python tools/coop/net_test.py
result:     12/12 scenarios, plain and under AddressSanitizer and UBSan, with three clients in characters, taken and
            taken_bad_wifi. All 225 session breaks caught (238 break-and-test pairs), all 4 sanitizer breaks caught,
            17/17 transport scenarios. Session only: nothing in the game calls it yet, and until WP-17 a character
            changes nothing in a level.
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
