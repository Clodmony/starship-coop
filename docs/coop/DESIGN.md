# Design

Status: draft. Upstream base: HarbourMasters/Starship `3322643f` (2026-09-15). Everything under "Observed" was read
in this repo at that commit; line numbers drift with upstream syncs, so search for the symbol if a line is off.

## 1. What we are building

Two players (later up to four) play the Star Fox 64 campaign together **online**: each player runs Starship on their
own PC and sees the level from their own Arwing. Every player sees the same level: the same enemies, bosses, items,
wingmen and route. The host's save file holds the progress.

Local split screen (two players on one PC, the way versus mode renders) is deferred until after the first release.
Nothing in the online design may rule it out.

## 2. What the decomp tells us

### Observed (with sources)

- **The engine already simulates several players, in versus mode.** `gPlayer` is a pointer to an array of `Player`
  (`src/engine/fox_context.c:373`). `Player_InitializeAll` clears `gCamCount * sizeof(Player)` bytes
  (`src/engine/fox_play.c`, `Player_InitializeAll`). `Player_InitVersus` sets up four players in a loop
  (`src/engine/fox_play.c`, `Player_InitVersus`).
- **Player count is tied to camera count.** Level init sets `gCamCount = 1`, and `gCamCount = 4` only when
  `gCurrentLevel == LEVEL_VERSUS` (`src/engine/fox_play.c`, around line 2931). The draw loop sets `gPlayerNum` to the
  player being drawn before `Display_Update()` (`src/engine/fox_game.c`, around line 289).
- **Many per-player arrays already have four slots:** `gLifeCount[4]` (`include/sf64context.h:228`), the button maps
  `gBoostButton[i]`, `gBrakeButton[i]`, `gShootButton[i]` for `i < 4` (`src/engine/fox_game.c`, `GSTATE_INIT`),
  `gControllerPress[]` indexed by `gPlayerNum` or `gMainController`.
- **Two level modes matter:** `LEVELMODE_ON_RAILS` and `LEVELMODE_ALL_RANGE` (`include/sf64player.h:62`).
- **On rails, path progress is one global fed by each player.** `Player_UpdatePath` copies the player's own `zPath`
  into `gPathProgress` (`src/engine/fox_play.c:3627`); object loading, dynamic-floor collision and object drawing use
  the global, ground scroll sums every player's speed (`gPathTexScroll`, `:3637`; Titania follows the last writer),
  and path turns and the x/y part of culling read `gPlayer[0]` only (`src/engine/fox_enmy.c:630`, `:2550-2591`,
  `:2694-2696`). Details in NOTES.md, 2026-11-05, WP-06.
- **Wingmen are separate from players.** `gTeamShields[6]` (`include/sf64context.h:23`) with ids starting at
  `TEAM_ID_FALCO` (`include/sf64player.h:18`); level init copies them from `gSavedTeamShields`
  (`src/engine/fox_play.c`, after the `gCamCount` lines).
- **World objects live in fixed arrays:** `gScenery[50]`, `gSprites[40]`, `gActors[60]`, `gBosses[4]`,
  `gEffects[100]`, `gItems[20]`, `gPlayerShots[16]` (`include/sf64context.h:232-238`).
- **Level memory is a bump allocator:** `Memory_Allocate` advances a pointer into `sMemoryBuffer`, `Memory_FreeAll`
  resets it (`src/sys/sys_memory.c`).
- **Randomness:** `Rand_ZeroOne` uses three seeds set by `Rand_Init()` from `osGetTime()` (`src/sys/sys_math.c`),
  called at boot (`src/engine/fox_game.c:58`). On Linux and Switch, `Rand_ZeroOne` re-runs `Rand_Init()` when a seed
  is 0 (a workaround marked `LTODO` in the code). That condition checks `sRandSeed2` twice and never `sRandSeed3`.
  What zeroed a seed on GCC builds was frame interpolation (NOTES.md, 2026-11-07, #141).
  In co-op that re-seed is off, as the clock would part the clients (#20). Each game frame also has one audio frame, and
  a frame held for input has none: audio frames on held frames parted the clients (NOTES.md, 2026-11-07, #20).
  A separate seeded generator exists: `Rand_SetSeed` / `Rand_ZeroOneSeeded`.
- **The port has an event system** for hooking game code without editing it: `DEFINE_EVENT` / `CALL_EVENT` in
  `src/port/hooks/impl/EventSystem.h`, events in `src/port/hooks/list/` (`GamePreUpdateEvent`,
  `PlayerPreUpdateEvent`, `PlayerPostUpdateEvent`, `ObjectInitEvent`, `ObjectDestroyEvent`, `ItemDropEvent`,
  `PlayerAction*Event`, HUD draw events).
- **Networking hook in the build:** `USE_NETWORKING` in `CMakeLists.txt` links SDL2_net and defines `USE_NETWORKING`.
  Upstream hard-sets it off; this fork turns it on for desktop builds (WP-20).
- **Render rate is separate from game rate:** the interpolation frame count is derived from `gInterpolationFPS` and
  `gVIsPerFrame` (`src/port/Engine.cpp`, `GetInterpolationFPS` and the function after it). It is local and may differ
  between clients: 30, 60, 120, 144 and 360 fps computed the same game (`hash_check.py rates`, Corneria, Linux Release).
- **Draw code is part of the simulation.** It frees objects, runs game logic and feeds boss AI, and two clients of one
  replay that drew different cameras split one frame after Corneria starts (NOTES.md, 2026-11-06, #96).

### Derived (reasoning, not yet tested)

- Running a second `Player` in the campaign starts with separating "number of players" from "number of cameras",
  because versus mode already updates and draws several players. On rails it also needs one shared path, owned by
  player 0 (NOTES.md, 2026-11-05, WP-06, and 2026-11-06, WP-12).
- Lockstep still holds with one camera per client, because in co-op every client runs all draw code and the camera
  drawn feeds nothing (`src/port/coop/game/view.h`). Shown on Corneria only; other levels are #129. That does not keep
  the simulation apart from rendering, as a stray write on the draw side did reach it: frame interpolation wrote below
  its own matrix stack, onto the RNG seeds of a GCC Release build (NOTES.md, 2026-11-07, #141). A hash on one PC only
  sees such a write when it lands on hashed memory.
- Because world state is in fixed arrays plus one bump-allocated buffer plus globals, a full state snapshot (for
  rollback or for a late join) is plausible in size. The arrays contain pointers, so a snapshot is only valid for the
  same build on the same platform.

### Unknown (each is a research task in the roadmap)

- Whether the simulation is deterministic across machines with the same build, and across OS and compilers. On one PC
  it was, in Corneria, on Windows with MSVC in Debug and on Linux with GCC in Debug and Release: same seeds and same
  inputs gave the same state ([decision 0002](decisions/0002-sync-model.md), table Measured).
- What else reads time or host state inside the simulation (audio timing, `osGetTime` callers, frame pacing).
- What breaks with two players on rails outside Corneria: only Corneria's rail section has been run with a second
  player (NOTES.md, 2026-11-06, WP-12).
- Which code assumes `gPlayer[0]` or a single player: cameras, HUD, radio, cutscenes, level-complete logic, warps.
- How the Landmaster (Titania, Macbeth) and Blue Marine (Aquas) levels handle extra players.
- Which port enhancements (CVars) change the simulation and must match between players. A first list from reading
  the code is in NOTES.md (WP-21) and in the handshake; nothing has tested it against state hashes yet.

## 3. Architecture (proposed)

```
src/port/coop/
  session/   lobby, peers, handshake: build id, ROM version, sim-affecting CVars must match
  net/       transport behind one interface: UDP on SDL2_net first (USE_NETWORKING, decision 0004), Steam relay later
  sync/      the sync model from decision 0002 (input exchange + state hashes, or host snapshots)
  game/      game glue: player count apart from camera count, player 2+ spawn, input routing, HUD, rules
  ui/        ImGui menu entries: host, join, status (the menubar is in src/port/ui/ImguiUI.cpp)
  log/       the co-op log of a session, one file per player, streamed between the players (#172)
  test/      replay files, state hashing, harness hooks used by tools/coop/
```

- **Gate:** the co-op session being active (a CVar plus session state). With co-op off, the simulation must be
  unchanged from upstream.
- **Layers talk downward only:** `ui` and `session` use `sync`; `sync` uses `net` and `game`; `game` uses the
  decomp through hooks. Nothing in `src/engine/` includes headers from `net/` or `session/`.
- **Online first, one camera per client.** Each client simulates the level with several `Player` structs but renders
  one camera, following its own player. So the number of players must be separate from `gCamCount` (WP-10), and the
  camera, HUD and sound must follow the client's own player index, for example 0 on the host and 1 on the first
  client (WP-28). Code that hard-codes `gPlayer[0]` as "the player looking at the screen" is the main thing to find (WP-05).
- **Engine work does not wait for the network.** In one process, a second player driven by a replay file (WP-03,
  WP-14) exercises all of `game/` before the transport exists. The network then replaces the replay file as that
  player's input source (lockstep) or state source (host authority).
- Local split screen later reuses the same player code and adds viewports (WP-13, WP-15).

## 4. Game rules to decide

Each needs a decision record before code depends on it. Proposals in brackets.

1. Who player 2 is: decided in [0005](decisions/0005-players-and-characters.md). Player 1 is always Fox; players 2-4
   each pick Falco, Peppy or Slippy; unpicked wingmen stay computer wingmen. What a human-flown wingman is, with its
   shields, radio, rail scenes and story events: [0006](decisions/0006-human-flown-wingman.md).
2. Lives: shared pool or per player [shared pool, game over when it is empty].
3. A player goes down: respawn rule [respawn after a delay while any player is alive].
4. Route choices and warps: who decides [whoever triggers it; the whole team follows].
5. Hit count and medals: [team total].
6. Landmaster and Blue Marine levels: [same rules, vehicles per player].
7. Cutscenes, radio and level intros: [host's timing; clients follow].
8. Save: [only the host saves; clients run with a throwaway save].

## 5. Sync model

Deterministic lockstep, [decision 0002](decisions/0002-sync-model.md), accepted on one-PC evidence. Every client runs
the whole simulation. Only inputs travel, plus a state hash per frame to detect desyncs, so all clients need the same
build, ROM and simulation CVars. A platform difference only warns
([decision 0010](decisions/0010-cross-platform-sessions.md)): nobody has compared two platforms, and the desync check is
what shows whether two compute the same game. Before a first public release: the two-machine run, and
`hash_check.py builds` and
`rates` on Windows Release (#145). A difference that cannot be found and removed reopens the choice through a new record.
