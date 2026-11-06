# Star Fox 64 (1997)

| | |
|---|---|
| Engine | Starship (native PC port from the decompilation, libultraship) |
| Versions tested | N64 US 1.0, SHA-1 d8b1088520f7c5f81433292a9258c1184afa1457. Linux (GCC) and Windows (MSVC), Debug builds. macOS not built. No store: the game data is built from your own ROM |
| Projects | [Starship Co-op](https://github.com/Clodmony/starship-coop), a fork of [Starship](https://github.com/HarbourMasters/Starship) |
| QA level | none |
| Last updated | 2026-11-06 |

## Where things stand

No: nobody can play Star Fox 64 together with this project today. Two game clients have never shared a session,
because the network code is built and tested on its own but nothing in the game calls it, there is no host or join
menu, and the sync model is still open. What does run is a second player inside one game process, on Corneria only,
behind a debug setting. QA level `none`.

## Existing work

- **[Starship Co-op](https://github.com/Clodmony/starship-coop)** (this project): active, last commit 2026-11. Syncs
  nothing between clients yet. Fork of Starship with the co-op code in `src/port/coop/`, and every change to upstream
  game code marked `// COOP:`
  ([decision 0001](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0001-fork-and-code-layout.md)).
- **[Starship](https://github.com/HarbourMasters/Starship)**: the port this builds on. It has the game's four-player
  versus mode, and upstream builds with networking off. No campaign co-op.
- A search on 2026-11-01 found no Star Fox 64 entry in this directory, and no co-op or online fork in the Starship
  forks list on GitHub, sorted by stars. Forks seen there, none of them co-op:
  [izzy2lost/Starship](https://github.com/izzy2lost/Starship) (Android),
  [SgtBilko76/Starship-VR](https://github.com/SgtBilko76/Starship-VR) and
  [keraion/Starship](https://github.com/keraion/Starship) (Archipelago).

## How the game works (for co-op)

File references are to `Clodmony/starship-coop` at commit `797c678b`. The project's lab notebook has the detail:
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

Unknown:

- Whether two PCs with the same build stay equal. Only one PC has run the determinism test.
- Other levels: only Corneria has been run. The Landmaster and Blue Marine levels are not run with a second player.
- Release builds, other compilers, and Windows against Linux.
- Anything between two real PCs: NAT, firewalls, real loss. Every network run so far is loopback.
- What is client-side and what is host-side: that depends on the sync model
  ([decision 0002](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0002-sync-model.md), still
  Proposed). It proposes lockstep if two PCs agree, and host authority otherwise.

## What we changed

Every test below ran one game process, or the network test programs with no game. None is a co-op run.

- **The player count was the camera count.** `gCamCount` is both, and it also means "this is versus". Fix:
  `CoopPlayers_Count()` (`src/port/coop/game/players.c`) counts players and `gCamCount` stays 1, which keeps a single
  camera. Test: with co-op off, a Corneria recording made before this change still played to PASS on all 13545 frames.
- **A second player in all-range.** Starting one mid-level through `Player_Setup` would reset level-wide state for
  player 0. Fix: extra players start parked and join as a copy of player 0 once it is flying. Test:
  `player_trace.py` on Corneria, player 2 active on the same frame as player 0.
- **A second player on rails.** Each player wrote the shared path, so two players moved the level at the wrong speed.
  Fix: player 0 owns the path and the others follow it (`CoopPlayers_FollowPath`). Test: `player_trace.py rails`, with
  2 and with 4 players, path equal to single player on all 4525 frames of Corneria's rail section. Without the fix,
  equal on 26 of them.
- **A second player going down restarted the level for everyone.** `Player_Down` stops the music and fades out for any
  player outside versus. Fix: a co-op player other than player 0 leaves quietly (`CoopPlayers_OnPlayerDown`). Test:
  player 2 forced down through gdb, `player_trace.py check` PASS.
- **Broken graphics over water with two players.** `Display_Update` pushed the matrix once but popped once per player,
  so the stack underflowed every frame. Fix: one pop after the loop (`src/engine/fox_display.c:2024`). Test: one
  two-player run watched by hand, graphics normal, and single-player replay still PASS.
- **No netcode.** Fix: UDP on SDL2_net with our own reliability layer
  ([decision 0004](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0004-transport.md)). Test:
  `net_test.py`, see QA.
- **Nothing kept mismatched clients apart.** Fix: a session handshake that refuses a different build, ROM or
  simulation setting, and carries who flies which character
  ([decision 0005](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/decisions/0005-players-and-characters.md):
  player 1 is Fox, players 2 to 4 pick a wingman). A character changes nothing in a level yet. Test: `session_test.py`,
  see QA.
- **No way to test sync.** Fix: replays, a per-frame state hash and a determinism check, the tools in the table below.

## QA

Level: `none`. `automated` needs at least two game clients sharing state, and no such run exists. The results below
are the last ones recorded in [QA.md](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/QA.md); they were
not run again for this write-up.

| Command (in `tools/coop/`) | What it asserts | Last result |
|---|---|---|
| `net_test.py` | Transport: order and delivery under loss, lag and outages, in a virtual network and through the chaos relay. No game, no ROM | 17/17 scenarios, 2026-11-06, Linux; 17/17 on Linux and Windows, 2026-11-05 |
| `session_test.py` | Handshake refusals and character picks, up to three clients. No game, no ROM | 12/12 scenarios, also under AddressSanitizer and UBSan, 2026-11-06, Linux |
| `net_mutants.py`, `session_mutants.py` | Those tests can fail: each break in the code must be caught | All 16 transport breaks and all 225 session breaks caught, 2026-11-06, Linux |
| `determinism.py <replay>` | One replay under six changed conditions gives the same hash on every frame | 6/6 on 13545/13545 frames (all of Corneria), 2026-11-05, Windows, one PC |
| `hash_check.py run` | Two runs match, and a one-bit change shows at exactly its frame | Equal on 10500/10500 frames, change seen at frame 5400, two players, 2026-11-06, Linux |
| `player_trace.py rails --players 2` | A second player flies the rail section without changing path progress | PASS, largest difference 0.000 on 4525 frames, also with 4 players, 2026-11-06, Linux |

## How to play

You can't yet. There is no release and no host or join menu. Developers can build it with their own US 1.0 or US 1.1
ROM ([BUILDING.md](https://github.com/Clodmony/starship-coop/blob/coop/docs/BUILDING.md)); only US 1.0 has been tested.

## Known issues

There is no co-op to play yet. With the debug setting that adds a second player in one game:

- The camera, HUD and sound stay on player 0 (`CoopPlayers_LocalIndex` always returns 0).
- Many enemy effects only hit player 0 (for example `src/engine/fox_effect.c:1396`).
- Only player 0's boost and brake change the speed of a rail level.
- A second player who goes down stays out until the next level start. There is no respawn rule yet.
- In the Landmaster and Blue Marine sections on rails the second player stays parked.

## Next steps

The [roadmap](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/ROADMAP.md) lists every work package. The
main steps:

1. Run the determinism test across two PCs with the same build
   ([#2](https://github.com/Clodmony/starship-coop/issues/2)), then settle decision 0002.
2. Give each player its own input source ([#12](https://github.com/Clodmony/starship-coop/issues/12)) and view
   ([#30](https://github.com/Clodmony/starship-coop/issues/30)).
3. Build the sync model ([#20](https://github.com/Clodmony/starship-coop/issues/20)) and the join flow
   ([#22](https://github.com/Clodmony/starship-coop/issues/22)).
4. Run two game clients sharing state under a script, on one machine. That is the first run that can raise the QA
   level.
