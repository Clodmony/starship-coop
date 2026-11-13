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

Current project level: `automated`. Two real game clients in one session, on one PC, stay equal on every frame through all of Corneria (`python tools/coop/lockstep_test.py`), and, a level per session, from the start to the end of 12 of the 17 campaign levels (`python tools/coop/campaign.py`, #28): Corneria, Meteo, Fortuna, Sector X, Titania, Venom 1, Aquas, Sector Z, Venom 2, Katina, Solar, Macbeth. Not covered with real clients: the ends of Bolse, Venom (Andross), Sector Y, Zoness, Area 6 (the pilot does not beat their bosses; every frame it flew was equal), a level that follows from the one before (each starts as a new game, no route state), a player who goes down (all but Corneria with `gInvincible`), two PCs, Windows, a real network, a Release build and more than two players. A full campaign was played by hand on two PCs on 2026-11-09 (issue #2); that record is not this one, and its level stays the owner's to enter.

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
   The client with index 1 has also ticked 6 more frames since boot (`SF64_COOP_TEST_SYS_FRAME_OFFSET`, `--skew`):
   `gSysFrameCount` is not simulation state, and two clients of a session do not share it (#129). `--level katina`
   (or a list, or `all`) flies another Arwing level for 2400 frames instead, and `--cvar NAME=VALUE` sets a CVar on both.
   The two clients also render different sample counts per audio update (`SF64_COOP_TEST_AUDIO_SAMPLES` high and low,
   `--audio`), as a radio line follows the voice sequence of the sound thread; the log counts the radio state changes, so
   a run shows that a line was played. In co-op the hash also holds that sequence's clock in a level (#230,
   `src/port/coop/sync/voice_clock.h`), so two games whose radio lines would end on different frames differ from the
   frames after the voice player starts, not only at a radio step. `python tools/coop/hash_check.py slots --level katina,fortuna,corneria` plays a level
   with 1 player and with 2 and fails when co-op holds more than 15 more effect slots (median): the draw code frees nothing
   for leaving the view in co-op (#96), so an effect that only that freed would stay for the whole level (#129).
   `python tools/coop/hash_check.py views --pilot` (#181, unattended, exit code 0 on `PASS`) takes the same two clients
   through the rest of each level: the boss and the end. Per level, one game with 2 players and `gInvincible` starts past
   the intro, and a pilot that reads game state (`src/port/coop/test/pilot.h`, `pilot_aim.h`) flies every player to level
   complete and on to the map or whatever comes next, and writes a replay; the two clients then play it and must `PASS`
   every checkpoint, hash alike on every frame and equal the pilot's game. Every campaign level, the vehicle levels and
   level 4 by default, or `--level`; `--mortal` leaves `gInvincible` off. The summary gives per level the result, the
   frames, the frame of level complete and the downs per player; a level the pilot does not finish says so, with the
   frames that were compared. Like `views` this is one game per run, so it says nothing about two clients over a
   network: that is `lockstep_test.py`.
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
   `python tools/coop/solar_check.py` (unattended, exit code 0 on `PASS`) plays Solar with the same two clients and
   gInvincible off: both players' shields must fall to the heat, and the hashes stay equal (#200).
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
   Two games that meet over sockets play at 8 times real time (`replay.MEETING_SPEED`), and a client opens its session on
   its own frame 100 (`replay.CLIENT_JOIN_FRAME`, #287): a client whose level start comes in its first 21 frames parts from
   the host at level frame 4 to 9 (22 of 23 runs; from frame 22 on 0 of 50), which a Release build at no frame limit did
   until then, as it plays 1100 frames a second and more. `COOP_TOOLS_MEETING_SPEED=max` runs the `lockstep` step on
   Release in 5m49s against 13m57s (46 of 48 scenarios; `save_own` is intermittent there and `log` is on the clock).
   `join` and `menu` stay at 8: their replays are counted in frames and their ping is on the clock. NOTES.md, 2026-11-13.
   A full `test_all.py` run takes 23m26s since #290 (41m37s before): `lockstep` 4 scenarios at a time, the mutation suites in
   a second checkout beside the rest, the `rates` steps with their 5 games at once. NOTES.md, 2026-11-13, #290.
   `python tools/coop/test_all.py --mixed` takes 18m04s (two runs, 56 of 56 steps ok, `coop-off` not run, against 23m47s
   for the same tree on Debug): it builds Debug and Release and plays every step that starts the game on Release, except
   `wingman`, `exit-test`, `builds` and `coop-off`, and the unit tests and loopback tools run in a lane of their own beside
   the others. On Debug alone the lanes do not help (24m46s): the machine is the limit there. A full `menu` failed in
   2 of 2 runs of the old lanes (`game_leave`, a 40 s timer on a loaded machine) and passed in 4 of 4 of these.
   NOTES.md, 2026-11-13, #290 (second part). It was no timer: the games parted at level frame 0 of the restart and the
   client's wait was measured from the wrong time (NOTES.md, 2026-11-13, #293).
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
   the real menu. `--only vehicles` (#16) plays Titania, Aquas and Macbeth side by side with two clients each: same
   hash, both players driving in Titania and Aquas, the client's player parked in Macbeth (`--levels` picks some). `--only full` flies all of Corneria with a pilot on each client, repeats the flight with plain games,
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
   `python tools/coop/campaign.py` (WP-32, #28, unattended, exit code 0 when every level passed) is the campaign with
   two real clients: per level a host and a client through the join driver, the level started as the lobby's
   "Start at" does (#211), a pilot on each client flying its own player (`SF64_COOP_LOCKSTEP_PILOT`) and `gInvincible`
   on both unless `--mortal`. The levels run in the order of a route (`--route blue|red|owner|all`, the map's paths in
   `fox_map.c`, or `--levels`), each as a session of its own, so no route state goes from one to the next: lives,
   score, lasers, team shields and medals start as for a new game. A level passes when the two games are equal on
   every frame, hash log and input log, from its first frame to the map or whatever follows its level complete, with no
   desync report, and player 0 reached level complete. One summary at the end: per level the result, the frames
   compared, the frame of level complete and the downs per player. `venom-2-andross` (#275) is Venom 2 and the level
   its clear jumps into, Andross' second part: the brain, then the escape through a maze to the trigger at its exit
   and the ending; the escape's start, a level complete of its own, does not count. A game over ends a level's run.
   `--lock <file>` holds an flock for each level's
   session, so other tests on the PC take turns. This is not the hand-played campaign of 2026-11-09 (issue #2), the
   only `verified-playthrough`: no level here follows from the one before.
4. **Network conditions (WP-27).** Lag, loss and reordering between the clients with a chaos relay, as in the hub's
   [network chaos testing](https://github.com/cranebeaker/coop-anything/blob/main/playbook/network-chaos-testing.md).
   The transport on its own already runs through that relay: `python tools/coop/net_test.py` (WP-20), which needs a
   configured build directory but no ROM. `python tools/coop/chaos_run.py` (WP-27, #25, unattended, exit code 0 when
   every run passed) does the same with real game clients: per profile of `chaos/profiles.json` and per seed (default
   1, 2 and 3), one session over loopback with the client's datagrams through the relay, a pilot on each client flying
   Corneria from its intro (6000 lockstep frames by default, `--frames`). Lockstep stamps every input with its frame,
   so the flight is the same at any lag: a run passes when both games reach the last frame equal on every frame, hash
   and input logs, with no desync report, and equal to the first passing run. Per run it reports the frames and the
   time held, the longest single hold, resends, ping, what the relay dropped and duplicated, and where a session that
   broke ended, with the profile and seed that repeat it. `--profiles`, `--seeds`, `--lock <file>`.
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
   `Play_Init`, as `join_test.py` does. Its `game_platform` scenario reads what the menu shows both players for a
   platform warning (decision 0010), against `coop_session_test` as the other side, because two games on one PC never
   differ in platform. Its `game_leave` scenario (#132) has player 2 leave during a level, checks that the level starts
   again for the two who stay (the session survives), has player 3 leave and checks that the session then ends while
   every game keeps running (lockstep, #160), then holds a second session in the same games, starts
   a level again and checks that all three players stay past the time-out. `game_level` and `game_leave` print the
   longest stall of the session tick that any game saw. Neither says how the window looks.
6. **Regression per bug.** Every desync fix comes with the replay that triggered it; every transport fix adds a
   break to `tools/coop/net_mutants.py`, every session fix one to `tools/coop/session_mutants.py`.

Running tests must never use or modify the owner's own `starship.cfg.json`, saves, `mods/` folder or running game.

## Results

Newest first. One block per run that matters (a milestone, a release, a QA level change).

```
date:       2026-11-13
commit:     issue/combined-test-6 (PR #285) with #288, #291 and #292 merged in: the C sources are those of 7e4385cc (#288
            changed the test join driver; #291 and #292 changed test tools and docs only); the runs are of 0b9dc392, the
            merged tree has the docs commit 3ceacfac after it, on 07c03c1f (coop)
identity:   ROM US 1.0, Linux, GCC 16.2, Debug and Release, OpenGL on a software renderer, no window; 2 or 3 game clients
            on one PC, UDP over loopback, normal priority, 4 game lanes
what ran:   python tools/coop/test_all.py --mixed (builds both configs), twice; the default (Debug) once; --mixed with the
            first rule of #292 once, on 47723983 (NOTES.md, 2026-11-13, #290 second part)
result:     automated. --mixed: 56 of 56 steps ok in 18m04s, both runs (builds, determinism, both rates steps, lockstep with
            results_hole, menu, join, net, session, steam, all seven mutant suites, replay-test, rails-2, rails-4,
            full-level, views, exit-test and wingman on Debug, and the check that no tracked file changed). Default
            Debug: 55 of 55 in 24m46s. coop-off was not run (no base build). The gate recorded below (52 of 53, on e8f038c6)
            is not of this head; these runs are. On 90594a98 with the old lanes menu's game_leave failed in 2 of 2 full runs
            (#293). Not run: Windows and MSVC, two PCs, a real connection that drops.
```

```
date:       2026-11-12
commit:     issue/combined-test-6 (PR #285): C sources as built 04af2b4d, the gate on its head e8f038c6 (the commits after
            04af2b4d change docs only), on 07c03c1f (coop)
identity:   ROM US 1.0, Linux, GCC, Debug (the Release build is the one the `builds` step compares), OpenGL on a software
            renderer, no window; 2 or 3 game clients on one PC, UDP over loopback
what ran:   python tools/coop/test_all.py --no-build --base-exe <build of 07c03c1f> --lanes 2 (53 steps, 42 minutes);
            before it, one scenario per run on 04af2b4d: lockstep_test.py --only macbeth_bombs, sy_robot_spread,
            other_bosses --levels macbeth, survive_gaps_checkpoints --levels andross, aim, bosses --levels andross;
            campaign.py --levels venom-2-andross; hash_check.py base --base <build of 07c03c1f>; then results_hole alone
            (see result)
result:     automated. 52 of 53 steps passed, among them coop-off (equal to the build of 07c03c1f), builds (Release
            against Debug), determinism, both rates steps, rails-2, rails-4, full-level, views, replay-test, the session,
            net, lockstep, desync and steam mutant suites (every break caught), and the check that no tracked
            file changed. The one failure is the `lockstep` step's scenario results_hole (#259): "seats: the one who left
            is player 3, the other 2". It is intermittent and also fails on the build of #259 alone: alternating, 6 of 8
            runs passed on each build (7 of 12 on this tree and 10 of 13 on #259 alone over all runs); the other 50
            lockstep scenarios passed (issue #286). The Andross end is reached with equal hashes (36998 frames), and
            survive_gaps_checkpoints --levels andross flies on to the ending (5339 frames). Not run: Windows and MSVC,
            two PCs, a real connection that drops, three or four players beyond the scenarios of #270, a person playing.
```

```
date:       2026-11-12
commit:     issue/272-macbeth-bombs, C sources as built c121da89 (later commits docs only), on 07c03c1f (coop)
identity:   ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window; 2 game clients on one PC,
            UDP over loopback, through the join driver
what ran:   python tools/coop/lockstep_test.py --only macbeth_bombs (on this branch, on the test commit c670ff0c
            alone, and on 07c03c1f); --only vehicles --levels macbeth; --only vehicle_end --levels macbeth;
            --only aim; python tools/coop/test_all.py --no-build --only coop-off,effects,effects-allrange,rails-2,
            rails-4,shots,lowhealth-rails,lowhealth-allrange,full-level,wingman,hash-run-1,hash-run-2,draw,exit-test,
            replay-test; determinism.py --only no-draw; check.py; sp_audit.py --check
result:     automated. Macbeth's bomb fire and state-16 beams, placed on one tank at a time by a probe, hit player 1
            3 of 3 times each (0 of 3 on the test commit without the fix), player 0 3 of 3 (the control), never the
            other tank; hashes and probe logs equal on every frame. Macbeth to level complete with two pilots and
            the other runs unchanged; co-op off equal to 07c03c1f on Corneria and, by a scratch script, in Macbeth.
            Not run: the train's end's own bombs and beams hitting player 1 in a fight, two PCs, Windows, Release,
            three or four players, a person driving it.
```

```
date:       2026-11-12
commit:     issue/220-macbeth-end, C sources as built ebfdd215 (later commits docs only), on wave7-base 07c03c1f (coop)
identity:   ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window; 2 game clients on one PC,
            UDP over loopback, through the join driver, a pilot on each
what ran:   python tools/coop/lockstep_test.py --only other_bosses --levels macbeth; --only w7macbeth_ram;
            --only vehicle_end --levels macbeth; --only vehicles; --only fox_refill_levels --levels macbeth;
            --only fox_refill_long --levels macbeth; --only other_bosses --levels <each of the other eight>;
            --only vehicle_end --levels titania (one per run); python tools/coop/campaign.py --levels macbeth;
            python tools/coop/hash_check.py base --base <build of 33bd5fe6>; check.py; sp_audit.py --check
result:     automated. Macbeth's train end goes to and faces the tank that was the nearer when each of its states
            started (906 and 396 frames toward player 1 when it was the nearer, 0 toward player 0; a build of this
            branch with fox_ma.c at wave7-base fails the same run). Its ram at the end of the track takes player 0
            down once and is not refilled under it (34 refills before the fix). Macbeth with two pilots to level
            complete, equal on every frame; the other bosses, vehicles, the refill tests and co-op off unchanged.
            Not run: two PCs, Windows, Release, three or four players, a person driving it.
```

```
date:       2026-11-12
commit:     issue/273-sy-robot-spread, build 103b864c717c, on coop 07c03c1f; red build 70e49d5caebf (the base's game
            code with this branch's aim trace); reference build 07c03c1f822d
identity:   ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window; 2 game clients on one PC, UDP over
            loopback, through the join driver, gInvincible on both
what ran:   python tools/coop/lockstep_test.py --only sy_robot_spread (branch, red build, reference build), aim, steer,
            other_bosses --levels shogun (one scenario per run); test_all.py --only checks,draw,exit-test,coop-off
            --base-exe <07c03c1f>, then --only wingman, then --only shots,effects,effects-allrange,lowhealth-rails,
            lowhealth-allrange; Sector Y with one player from its level start against 07c03c1f (a scratch script with
            hash_check.py's play and compare, no tool does it)
result:     automated. Sector Y's robots spread their shots around the human they aim at (#273): equal on all 9500
            frames, 23 robot shots, the 10 plain ones at the nearest human (within 0.00), the 13 spread ones within 150
            in x and y of it, 11 around player 1 with a root mean square of 100. The red build fails: 11 of 11 spread
            shots with player 1 the nearer fly straight at it, and one with player 0 the nearer went to player 1. Co-op
            off equal to 07c03c1f on Corneria (5800 frames) and on Sector Y (9510 frames). The test_all.py steps:
            all ok. Not run: the steps that start three or more games at once (full-level, hash-run-1, hash-run-2,
            rails-2, rails-4, replay-test, determinism) and the rest of the default lockstep run, two PCs, Windows, a
            Release build, more than two players.
```

```
date:       2026-11-12
commit:     issue/267-survive-gaps, code 4f162f64 (the C of the branch), on wave7-base 07c03c1f (coop)
identity:   ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window; 2 to 4 game clients on one PC,
            UDP over loopback, through the join driver; pilots on both clients (gInvincible but in Corneria, as
            campaign.py) or a feed
what ran:   python tools/coop/lockstep_test.py --only survive_gaps_route, survive_gaps_map, survive_gaps_map_lost,
            survive_gaps_venom_jump, survive_gaps_four, and survive_gaps_checkpoints --levels <one level> for katina,
            bolse, andross and corneria, meteo, sector_x, area_6, sector_y, venom_1, solar, zoness, macbeth, titania,
            aquas (each @ring: the level's own ring put where player 2 flies), one scenario or level per run;
            regression: survive_desync, survive_three, survive_checkpoint, survive_checkpoint_allrange,
            survive_next_level, stall, frozen, save_own, vehicles --levels macbeth; join_test.py; check.py
result:     automated. Decision 0014 beyond Corneria and three players. A level that started again (Corneria; Meteo
            with the route it carried) flown to its end, the map and the next level: equal on every frame, the same
            route and save on both games at every level and map. A desync and a lost player on the map: the level
            the team last entered starts again from its own level start, the map's changes dropped, equal on 600
            frames. A restart at the game's own checkpoint in 13 levels (11 rails rings, Katina, Bolse), each flown
            on to the level's end equal on every frame; in Andross' second part the restart at the brain held for
            4872 frames, but the pilots ran out of lives there, so its end was not reached. A restart in Venom 2's
            second part from the point Venom 1's jump leaves, flown on into Andross. Four players: one lost, the
            level started again for three; a second lost, again for two. Fault found and fixed: a restart at
            Macbeth's checkpoint parted again 34 frames in (the tank's state and Macbeth's rail survived the level
            start); seen on ebe1afbb, gone on 4f162f64. Not run: two PCs, Windows, a real connection drop, a Release
            build, what a player sees and hears.
```

```
date:       2026-11-12
commit:     issue/275-andross-pilot at 3cd90834 (build 3cd90834ee4f; pilot_andross.c, campaign.py) on 07c03c1f
            (coop); the failure on the reference build of 07c03c1f
identity:   ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window, 2 clients per run over loopback
            through the join driver, lockstep, a pilot on each client, gInvincible and gInfiniteBombs on both
what ran:   python tools/coop/campaign.py --levels venom-2-andross --lock <file>, also with --seeds
            17,71,170,710,1700,7100; --levels corneria,venom-andross; the same session from Venom 2 with 10 more sets of
            start seeds; lockstep_test.py --only full and --only bosses --levels andross; test_all.py --no-build --only
            checks,replay-test,exit-test,hash-run-2,full-level; #270's survive_gaps_checkpoints --levels andross on a
            scratch merge of #270 and this branch
result:     automated. Andross' second part, after Venom 2 (the face, the brain, the escape) to the ending: equal on
            every frame, 36461 frames compared, level complete at the escape's exit at level frame 25439, downs 0, 0;
            with the other seeds 33146 and 23281; each of the 10 seed sets to the ending, equal; all three of the
            escape's mazes flown. #270's scenario with this branch: run 2 to the ending, equal on 4313 frames. On
            the reference build the same session ends in a game over: an escape explosion sets player 0's shields to
            0 whatever gInvincible says (src/overlays/ovl_i6/fox_andross.c:751), and the old pilot flew into one.
            Corneria and Venom/Andross' first part as on 2026-11-11 (10601 and 7581 frames, equal); full, bosses:
            ok; test_all: 6/6. Not run: two PCs, Windows, Release, --mortal, other_bosses, vehicle_end.
```

```
date:       2026-11-12
commit:     issue/262-stray-reads: the sweep on the game code of wave7-base (07c03c1f), the runs after the fixes on
            a7e3f2c2
identity:   ROM US 1.0, Linux, GCC, Debug with AddressSanitizer and UBSan (build-asan), OpenGL on a software
            renderer, no window; one game per run, 2 players by gCoopDebugPlayerCount, no transport; each level
            started past its intro, gInvincible but in Corneria, gInfiniteBombs in Zoness and at Andross
what ran:   python tools/coop/w7asan_sweep.py baseline; python tools/coop/w7asan_sweep.py sweep (17 levels, 229304
            frames, 71 minutes), and the same again after the fixes
result:     QA level none (one game per run). Before: 17 AddressSanitizer and 18 UBSan bounds reports in 71 distinct
            lines, 17 of them not known as harmless: six places that read or wrote outside an array (the map's
            mission bar, Aquas' jellyfish, Venom 1's Golemech, Katina's team at the end, Macbeth's texture scroll,
            which ended that game at frame 4494, and the test pilot at Goras). After: w7asan: PASS, 17 of 17 levels
            to level complete, 0 reports that fail the run (7, 10 and 28 known or listed). Two clients on the plain build of a7e3f2c2 (automated): eels,
            vehicle_end macbeth, aquas and titania, campaign.py venom-1 and katina, each equal on every frame; co-op
            off equal to the reference build on 5800 frames. Not run: Windows, MSVC, a Release build, a session
            under the sanitizers, test_all.py. docs/coop/NOTES.md, 2026-11-12, the sanitizer sweep.
```

```
date:       2026-11-11
commit:     issue/session-survives at 921d6411, on top of a4867d99 (coop)
identity:   ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window; 2 or 3 game clients on one PC,
            UDP over loopback, through the join driver and through the real menu
what ran:   python tools/coop/lockstep_test.py --only survive_desync,survive_three,survive_checkpoint,
            survive_checkpoint_allrange,survive_next_level,save_own,desync,handover,stall,frozen,left,host_left
            (one scenario per run); python tools/coop/menu_test.py --game; python tools/coop/join_test.py;
            python tools/coop/hash_check.py base --base <build of a4867d99>
result:     automated. A session survives a desync and a lost player: the level starts again from the host's state
            and is equal on its first 600 frames in every scenario (from the level's start; from a rails checkpoint
            on Corneria; from Fortuna's all-range checkpoint; in the level after the map, with the campaign's values;
            with a client that stopped sending input or was frozen and came back with the session id; with one of two
            clients gone). Both co-op logs name the first differing field as desync_report.py diff does (#189). With
            nobody left, or after two restarts that part at once, the session ends as before. menu_test.py --game
            8/8, join_test.py 5/5, co-op off equal to the base build on 5800 frames. Not run: two PCs, Windows, a
            real connection drop, a Release build, four players, the map after a level that started again.
```

```
date:       2026-11-10
commit:     issue/map-host-save-impl, code 00a34694 (build 00a3469451fe) and 4bbf2898 (build 4bbf289836e3, the same
            code and docs), on issue/combined-test-4 b98c3870; base b98c38708b44
identity:   ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window, 2 clients over loopback through
            the join driver, lockstep, a pilot on each client, each game with its own save file (tools/coop/make_save.py)
what ran:   python tools/coop/lockstep_test.py --only map_saves, map_saves_host_full, map_saves_equal, save, save_own,
            full, handover, menu, leave (one scenario per run); join_test.py; hash_check.py base --base <coop 0e5a5cf6>
result:     automated. Two different saves (decision 0009 item 8): equal on every frame from Corneria's level start
            through the map into the next level (Meteo) and 300 frames of it: host blank and client all cleared 11689
            frames, host all cleared and client blank 11449, both all cleared 11449. After the session every
            default.sav byte for byte as before, the host's too, and each game's save in memory its own again; in the
            session both held the host's progress, the client its own sound settings. On b98c38708b44 both orders part
            at frame 10582, 41 frames into the map. save, save_own, full, handover, menu, leave: ok; join_test 5/5;
            co-op off equal to coop on all 5800 frames. Not covered: the alternate exit, two PCs, Windows, Release.
```

```
date:       2026-11-10
commit:     issue/181-views-later-parts (pilot_aim.c, views --pilot), on 80ff9be6
identity:   ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window, one game per run (local index 0
            and 1, the second 6 frames ahead since boot, high and low audio sample counts), no transport, 2 players by
            gCoopDebugPlayerCount, gInvincible, each level started past its intro
what ran:   python tools/coop/hash_check.py views --pilot --level <name>, one level per run, all 17 campaign levels and
            level 4; hash_check.py slots --level meteo,katina,zoness,area-6 --frames 9000
result:     QA level none (one game per run). Equal on every frame to the map or what follows level complete:
            Corneria 10215, Meteo 14828, Sector X 12517, Solar 10212, Fortuna 8811, Katina 15823, Sector Z 5572,
            Venom 2 17911, Venom 1 10147, Aquas 7973, Titania 53552, Macbeth 11524 (there the pilot's own game
            differed from both clients on 6 single frames, gRadioState). Equal on every frame flown, level not
            finished by the pilot: Area 6 60014, Sector Y 36014, Zoness 24014, Bolse 24014, Andross 24014, level 4
            24014 (no end). Not covered: Training (no level complete), level 15 (crashes with co-op off too).
            slots: PASS, co-op minus single player median effects 0 to +12; the explosion mark piles up in Katina.
```

```
date:       2026-11-10
commit:     issue/28-campaign-run (campaign.py) on 80ff9be6, against the build of issue/181-views-later-parts a36dea0a
identity:   ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window, 2 clients per level over loopback
            through the join driver, lockstep, a pilot on each client, gInvincible on both but in Corneria
what ran:   python tools/coop/campaign.py --route all --lock <file>; then --levels corneria and --levels titania
result:     automated. Equal on every frame, hash and input logs, from the level start to the map or what follows level
            complete, no desync report: Corneria 10291, Meteo 21410, Fortuna 9426, Sector X 12155, Titania 45111, Venom
            1 11172, Aquas 8599, Sector Z 6505, Venom 2 17968, Katina 16751, Solar 12711, Macbeth 11315. Equal on every
            frame flown, the pilot did not reach the end: Bolse 26312, Venom (Andross) 26059, Sector Y 36131, Zoness
            26041, Area 6 40270. Downs: 0 on every level.
```

```
date:       2026-11-10
commit:     issue/25-chaos-numbers (chaos_run.py) on 80ff9be6, against the build of issue/181-views-later-parts a36dea0a
identity:   ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window, 2 clients over loopback through
            the join driver, the client's datagrams through chaos/relay.py, a pilot on each client, real time
what ran:   python tools/coop/chaos_run.py --seeds 1,2,3 --frames 6000 --lock <file> (every profile);
            python tools/coop/chaos_run.py --profiles lan --seeds 1 --outages 5,9,12 --lock <file>
result:     automated. 18/18 profile runs reached frame 6000 equal on every frame, each equal to the first: lan and
            good_wifi waited 0 to 44 ms in all, bad_wifi 0.4 to 0.5 s, mobile_tether 3.5 to 6.6 s, transatlantic
            2.7 to 15.9 s, hell 151 to 154 s (longest single wait 0.8 to 1.4 s, 26 to 28 % of datagrams dropped).
            Link down 5 s and 9 s: waited out, equal. Link down 12 s: the session ends after 10 s without input
            (frame 3059). One PC, loopback, no real WAN.
```

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
