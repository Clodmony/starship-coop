# Star Fox 64 (1997)

| | |
|---|---|
| Engine | Starship (native PC port from the decompilation, libultraship) |
| Versions tested | N64 US 1.0, SHA-1 d8b1088520f7c5f81433292a9258c1184afa1457. Linux (GCC) for the scripted tests, Linux and Windows (MSVC) for the played sessions, Debug builds. macOS not built. No store: the game data is built from your own ROM |
| Projects | [Starship Co-op](https://github.com/Clodmony/starship-coop), a fork of [Starship](https://github.com/HarbourMasters/Starship) |
| QA level | automated (the project's own level, [QA.md](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/QA.md)); a full campaign was also played by hand, see below |
| Last updated | 2026-11-11 |

## Where things stand

Yes, with limits. Two people played the whole campaign together online, each on their own PC, a Windows host and a Linux
joining player over a direct connection with a VPN: Corneria through the ending in 66 minutes, on 2026-11-09 and 2026-11-10,
with no desync on that build, a median ping of 33 ms and 31 short waits for the other player. Sessions on the evening of
2026-11-10 found two faults that only two PCs show, and both are fixed; the longest went through Corneria's waterfall exit,
two maps and Sector Y into Aquas, about 33,000 frames. There is no release and no download: you build it from source with your own ROM.
Not done: the host's ship going down restarts the level for both players, and a desync or a lost player ends the session
(both have fixes decided or in progress), and several levels have not been played by hand with two real players.

## Existing work

- **[Starship Co-op](https://github.com/Clodmony/starship-coop)** (this project): active, last commit 2026-11-11. Lockstep
  campaign co-op for two players, up to four in the code, with the co-op code in `src/port/coop/` and every change to
  upstream game code marked `// COOP:`
  ([decision 0001](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0001-fork-and-code-layout.md)).
- **[Starship](https://github.com/HarbourMasters/Starship)**: the port this builds on. It has the game's four-player
  versus mode, and upstream builds with networking off. No campaign co-op.
- A search of this hub on 2026-11-11 (`data/directory.yaml`, `games/`, issues and pull requests) found no Star Fox 64
  entry and no mention of Starship. A search on 2026-11-01 found no co-op or online fork in the Starship forks list on
  GitHub, sorted by stars. Forks seen there, none of them co-op:
  [izzy2lost/Starship](https://github.com/izzy2lost/Starship) (Android),
  [SgtBilko76/Starship-VR](https://github.com/SgtBilko76/Starship-VR) and
  [keraion/Starship](https://github.com/keraion/Starship) (Archipelago).

## How the game works (for co-op)

File references in this section are to `Clodmony/starship-coop` at commit `797c678b` (2026-11-06), the version it was written
against: line numbers have drifted since, function and file names still find the code. The project's lab notebook has the detail:
[NOTES.md](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/NOTES.md).

Observed, from the code:

- **Engine:** decompiled C. Shared code is in `src/engine/`, level code in the overlays in `src/overlays/`, and the
  game has 406 object ids (`include/sf64object.h:725`). There is no general scripting language, but actors can run
  event scripts held as data (`include/sf64event.h`, `ActorEvent_Update` in `src/engine/fox_enmy2.c:3026`).
- **Netcode:** none upstream: `USE_NETWORKING` is set off, and `src/` and `include/` on upstream `main` have no socket
  code. Versus mode is local: `gCamCount` is 1, and 4 only for `LEVEL_VERSUS`
  (`src/engine/fox_play.c:2929-2932`), and `Player_InitVersus` sets up the players (`src/engine/fox_play.c:2810`).
- **Players:** `gPlayer` is a pointer to an array of `Player` (`src/engine/fox_context.c:373`), and many per-player
  arrays have four slots, such as `gLifeCount[4]` (`include/sf64context.h:228`). `gCamCount` is the player count and
  also, in about 25 places, the versus flag, so co-op cannot just set it to 2.
- **Single-player assumptions:** the audit lists 1788 uses of `gPlayer[0]`, `gPlayer->`, `gMainController` and
  `gCamCount` in 407 functions
  ([single-player.md](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/audit/single-player.md)). Its
  verdicts are a starting list: a sample of 45 found 5 wrong.
- **World state:** fixed arrays such as `gActors[60]`, `gBosses[4]` and `gEffects[100]`
  (`include/sf64context.h:232-238`), one level memory buffer with a bump allocator (`src/sys/sys_memory.c:5-12`), and
  globals. The structs hold pointers, so a snapshot only fits the same build on the same platform.
- **Levels:** two modes, `LEVELMODE_ON_RAILS` and `LEVELMODE_ALL_RANGE` (`include/sf64player.h:62-63`). `Play_Init`
  frees level memory (`src/engine/fox_play.c:2847`). On rails, `Player_UpdatePath` copies the player's own `zPath` into
  the global `gPathProgress` (`src/engine/fox_play.c:3643`), and `Object_LoadLevelObjects` loads objects by that global
  (`src/engine/fox_enmy.c:630`).
- **Randomness:** `Rand_Init` seeds three values from `osGetTime()` (`src/sys/sys_math.c:15-18`) at boot
  (`src/engine/fox_game.c:58`). On Linux and Switch, `Rand_ZeroOne` seeds again from the clock when a seed is 0, and
  that check tests `sRandSeed2` twice and never `sRandSeed3` (`src/sys/sys_math.c:25`).
- **Audio and logic:** one game frame runs the whole game update first, and only then the audio thread
  (`push_frame`, `src/port/Game.cpp:24-31`). Game logic still reads audio state: the radio waits for the voice sequence
  (`src/engine/fox_radio.c:615-645`).
- **Saves:** one `gSaveFile` (`src/engine/fox_save.c:3`), written by `Save_Write` (`src/engine/fox_save.c:142`). No
  co-op code writes it yet.
- **Hooks:** the port has an event system for reaching game code without editing it (`DEFINE_EVENT` and `CALL_EVENT`,
  `src/port/hooks/impl/EventSystem.h:35`, `:43`).

Derived, from test runs:

- On one PC and one build, the hashed state of a full Corneria run depended only on inputs and seeds. Graphics
  backend, window and frame pacing, audio timing and turning off the C runtime's FMA3 path changed nothing, on all 13545
  frames. That is what lockstep needs on one machine.
- Two players need one path. Player 0 owns it, and the others follow it: with both players writing, `gPathProgress`
  matched single player on only 26 of 4525 frames.

Observed in real sessions on two PCs (2026-11-09 to 2026-11-11), each a difference between a Windows and a Linux build of one commit:

- The C library's `sinf` and `cosf` round differently on the two platforms, and the game calls them through `SIN_DEG` and
  `COS_DEG`: the two parted within 30 frames of Corneria. In a session the game now uses the N64's own algorithm in double
  arithmetic ([decision 0011](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0011-deterministic-sine-in-sessions.md)).
- Two random draws in one expression, and the order the compilers evaluate them in. Titania, Macbeth and the Landmaster dust
  had them; each now draws in its written order in a session.
- `Actor.iwork` is `uintptr_t` (unsigned, 64 bits) in the port and an `s32` on the N64, so a negative index compared as a
  huge number and one Aquas enemy read memory outside an array, which the two compilers lay out differently. Two Linux
  games read the same memory and agree, so no Linux-only test could part on it.
- The map reads the save of the PC it runs on, so two PCs with two saves parted 41 frames into the map. The host's save is
  now the session's save ([decision 0009](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0009-vehicles-cutscenes-save.md), item 8).

Unknown (2026-11-11):

- Levels not played by hand with two real players: Sector X, Sector Z, Katina, Bolse, Zoness, Titania, Venom 1. Scripted
  two-client runs cover most of them, with a pilot that cannot beat every boss yet.
- More than two players in a real session. Three clients run in scripted tests; four are in the code.
- A Release build in a campaign between two PCs, other compilers, and a network beyond one direct connection over a VPN.
- The Steam relay on real Steam: it is built and tested on a fake Steam only.

## What we changed

Each item is a bug or a missing piece, its cause, the fix, and the test that proves it. The tests are in `tools/coop/`; the
commands and results are in the QA table below.

- **The player count was the camera count.** `gCamCount` is both, and it also means "this is versus". Fix:
  `CoopPlayers_Count()` (`src/port/coop/game/players.c`) counts players and `gCamCount` stays 1. Test: with co-op off, a
  Corneria recording made before the change plays to PASS on every frame, and `hash_check.py base` shows a build with
  co-op off equal to one without the co-op code.
- **A second player on rails and in all-range.** Each player wrote the shared path. Fix: player 0 owns the path and the
  others follow it; extra players start parked and join as a copy of player 0. Test: `player_trace.py rails`, path equal to
  single player on every frame of Corneria.
- **No netcode, and nothing kept mismatched clients apart.** Fix: UDP on SDL2_net with our own reliability layer
  ([decision 0004](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0004-transport.md)), and a session
  handshake that refuses a different build, ROM or simulation setting, warns about a different platform, and carries who
  flies which character ([decision 0005](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0005-players-and-characters.md)).
  Test: `net_test.py`, `session_test.py` and their mutation suites (a break in the code must be caught).
- **Keeping two PCs equal.** Fix: lockstep (inputs go over the network, every frame waits for every player), a hash of
  the game state compared on every frame, and on a difference a report from each game plus a co-op log streamed to the
  others ([decision 0002](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0002-sync-model.md)).
  Test: `lockstep_test.py` with deliberate one-bit differences that must show at exactly their frame.
- **Differences between a Windows and a Linux build,** found by real sessions: the sine and cosine, two random draws in one
  expression, the unsigned `iwork`, and the map reading each PC's own save (see "How the game works"). Fix: the N64 sine in a
  session, draws in their written order, a signed comparison, and the host's save as the session's. Test: a scripted pair
  with two different saves (`map_saves`: equal on 11689 frames through the map, and failing on the build before), and
  `eels` (the Aquas boss, 9000 frames, failing on the build before).
- **Game rules for more than one human:** one shared pool of lives with players 2 to 4 coming back after 150 frames
  ([decision 0007](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0007-lives-and-respawn.md)), the
  team earning and scoring ([0008](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0008-routes-and-score.md)),
  a vehicle per player in the Landmaster and Blue Marine levels, a human-flown wingman and the scenes it takes
  ([0006](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0006-human-flown-wingman.md),
  [0012](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0012-taken-wingman-scenes.md)), enemies
  and bosses aiming at the nearest human, and items and level triggers that count every player. Test: a scripted
  two-client scenario per rule.

## QA

Level: `automated`: real game clients share state under a script and a test harness checks it. A full campaign was also
played by hand on two PCs; whether that earns `verified-playthrough` is the owner's to enter in
[QA.md](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/QA.md), which also lists what no run covers.

| Command (in `tools/coop/`) | What it asserts | Last result |
|---|---|---|
| `test_all.py` | Everything below and more: 52 steps, a build of the previous branch as the base for "co-op off" | PASS 52/52, 36 minutes, 2026-11-11, Linux |
| `session_test.py`, `session_mutants.py` | Handshake, character picks, level start and save message, and that each of 309 deliberate breaks of the session code is caught | 17/17 scenarios, all 309 breaks caught, 2026-11-11, Linux |
| `lockstep_test.py --only full` | Two real clients fly Corneria to the map: equal on every frame, a one-bit change shows at its frame | 10601 frames equal, 2026-11-11, Linux |
| `lockstep_test.py --only map_saves` | Two PCs with two different saves stay one game through the map into the next level, and no real save file is written | 11689 frames equal, 2026-11-11, Linux |
| `lockstep_test.py --only eels` | Aquas' boss fight with both marines firing: equal, and every eel's smoke lies in the fight | 9000 frames equal, 2026-11-11, Linux |
| `hash_check.py base` | With co-op off a replay of Corneria equals the same replay on a build without the co-op change | PASS, a step of the 52, 2026-11-11, Linux |
| `campaign.py` | The campaign with two real clients per level and a pilot on each | 12 of the 17 levels from start to end, as recorded in QA.md; the rest need a pilot that beats their boss |
| Played by hand | Two people, two PCs (Windows host, Linux client) | The whole campaign, 66 minutes, 2026-11-09 and 2026-11-10; sessions on 2026-11-10 up to Aquas' boss |

## How to play

There is no release and no download. Each player needs their own Star Fox 64 ROM (US 1.0 or 1.1, the same version for
both) and the same build of this project, built from source: `python tools/coop/build.py --config Release --assets`
([BUILDING.md](https://github.com/Clodmony/starship-coop/blob/coop/docs/BUILDING.md); only US 1.0 has been tested). In the
game press F1, open **Co-op**: the host clicks **Host a game** (UDP port 46464 by default, which must be reachable: the same
network, a forwarded port, or both on a VPN), the other player types the host's address and clicks **Join a game**, each
picks a character, and the host clicks **Start the level**. Both players must see the same `Co-op <build id>` text in the
menu bar. The README has the full steps and what each rule does:
[How to play](https://github.com/Clodmony/starship-coop#how-to-play).

## Known issues

What a player will notice, as of 2026-11-11:

- When the host's ship goes down, the level restarts from its checkpoint for both players. The accepted rule is to refill
  Fox on the spot for a life; it is not built yet.
- A desync or a lost player ends the session, and each game carries on alone in the level. Restarting the level together is
  in progress.
- While your own ship is out, you watch player 0's view. The Solar heat alarm, the pause menu's reticle toggle and billboards
  still follow player 0 on every PC.
- A wingman's scenes that a human flies are taken by another wingman, Katt or Bill, with that character's own radio lines
  (decisions 0012 and 0013); nobody has listened to those lines yet.
- Several levels have not been played by hand with two players (Sector X, Sector Z, Katina, Bolse, Zoness, Titania, Venom 1),
  nor three or four players in a real session, nor a Release build in a campaign, nor the Steam relay.
- Between a Windows and a Linux PC every real session so far found a fault, and each was fixed; more are likely.
- No split screen, no macOS build, no download.

## Next steps

The [roadmap](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/ROADMAP.md) lists every work package. The
main steps now:

1. Refill Fox in place when his ship goes down ([decision 0007](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0007-lives-and-respawn.md)),
   and a session that survives a lost player or a desync by restarting the level together.
2. Show each human's name over their ship, and make the heat alarm, the reticle and the billboards follow each PC's own player.
3. A pilot that beats the remaining bosses, so `campaign.py` runs the whole route unattended.
4. The Windows measurements that need a Windows PC: the MSVC order-of-draws check and a Windows Release run of `hash_check.py`.
5. A downloadable build, the Steam relay on real Steam, and more than two players in a real session.
