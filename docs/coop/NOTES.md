# Notes

Shared lab notebook. Add findings about the game and the port as you make them, newest at the top of each section,
in the same PR as the work. Each entry: date, issue, then **Observed** (with file:line or a link), **Derived**,
**Guess** and **Unknown** as separate lists. Remove nothing; mark an entry `[wrong, see <date>]` if it turns out
false. Settled knowledge moves into DESIGN.md.

Private notes (your paths, your PC, your logs) go in `local/`, not here.

## Build and run

_Nothing yet._

## Engine and players

### 2026-11-01, project setup

Observed: see DESIGN.md section 2 (player array, `gCamCount`, object arrays, RNG, event hooks, `USE_NETWORKING`).

Unknown: everything listed in DESIGN.md section 2, "Unknown".

## Determinism

### 2026-11-04, #3 (WP-03), input replay

Identity: ROM US 1.0, branch `issue/3-input-replay` on top of `ba0781d7`, Windows 11, MSVC 19.44 (VS 2022 Build
Tools), Debug, DirectX 11.

Observed:

- Controller input enters the game once per game frame: `push_frame` (`src/port/Game.cpp:23`) calls
  `Graphics_ThreadUpdate` (`src/sys/sys_main.c:232`), which runs `Controller_UpdateInput` then `Controller_ReadData`
  (`src/sys/sys_main.c:241-242`) and then `Game_Update`. `Controller_UpdateInput` copies the pads read in the previous
  frame into `gControllerHold[4]` and computes `gControllerPress[4]` (edges and dead zone) (`src/sys/sys_joybus.c:65-82`).
  `Game_Update` fires `GamePreUpdateEvent` before any game logic (`src/engine/fox_game.c:352`). Between the two only
  `Main_SetVIMode` reads input (gamma toggle, `src/sys/sys_main.c:160`).
- The port's `OSContPad` is 0x24 bytes and carries gyro and right stick fields next to the N64 ones
  (`libultraship/include/libultraship/libultra/controller.h:98-107`).
- RNG seeds are plain globals: `sRandSeed1-3` and `sSeededRandSeed1-3` (`src/sys/sys_math.c:4-9`). `Rand_Init` fills
  them from `osGetTime()` (`src/sys/sys_math.c:15-18`), called from `Game_Initialize` (`src/engine/fox_game.c:58`).
  The seeds observed at runtime are often negative (for example `-28555`), because the 64-bit time is cast to `s32`
  before `% 30000`.
- The game runs about 30 logic frames per second: 1251 to 1277 frames in a 45 s idle run.
- **Wall-clock timer:** the port's `Timer_CreateTask` / `Timer_Update` fire on `SDL_GetTicks()`
  (`src/port/Engine.cpp:687-734`). The only game caller is the Nintendo logo at boot, which advances `gGameState` after
  1000 ms (`src/engine/fox_game.c:402`). A first replay that started at the first game frame diverged at frame 28
  (`GSTATE_LOGO_WAIT` against `GSTATE_START`), because the logo lasted a different number of frames.
- With recording and playback starting at the first frame of `GSTATE_START` (after the logo) and the six RNG seeds
  restored there, a 1251-frame idle run (title, intro, attract demo) replays with every per-frame checkpoint equal.
- **Corneria:** a human run recorded from `GSTATE_START` through the menus, Corneria and on to the map (13545 frames,
  465 s, level complete at frame 11747 on level 0) played back unattended in a fresh folder with every per-frame
  checkpoint equal and level complete at the same frame 11747: `replay: PASS (play, 467s)`.

Derived:

- Inputs plus the six seeds at `GSTATE_START` are enough to reproduce the menus and a full Corneria run frame by frame
  on one machine and one build, as far as the checkpoint sees (game state, level, player 0 state and position, RNG
  seeds). This is a first positive sign for WP-02, not its result: WP-04 hashes far more state.
- For lockstep, a session has to start after boot (at a level start or a menu), not at process start, or the logo
  timer has to become frame-based.

Unknown:

- Whether state outside the checkpoint (enemies, bosses, wingmen, items) also stays identical. WP-04 will tell.
- Whether a replay recorded on one PC plays back on another with the same build (WP-02).
- Whether audio or other wall-clock sources affect other levels.

### 2026-11-04, #4 (WP-04), state hash

Identity: ROM US 1.0, branch `issue/4-state-hash` on top of `070256bd` (#37), Windows 11, MSVC 19.44 (VS 2022 Build
Tools), Debug, DirectX 11.

Observed:

- The simulation state is: `gPlayer` (`gCamCount` entries, allocated per level, `src/engine/fox_play.c:2948`), the
  fixed arrays `gScenery[50]`, `gSprites[40]`, `gActors[60]`, `gBosses[4]`, `gEffects[100]`, `gItems[20]`,
  `gPlayerShots[16]`, `gTexturedLines[100]`, `gRadarMarks[65]`, `gBonusText[10]` (`include/sf64context.h:232-241`),
  `gScenery360` (200 entries, all-range levels only, `src/engine/fox_play.c:2957-2958`), the six RNG seeds
  (`src/sys/sys_math.c:4-9`) and globals in `src/engine/fox_context.c`.
- Pointer fields: `ObjectInfo.draw/dList`, `.action`, `.hitbox` in every object (`include/sf64object.h:145-160`),
  `Effect.unk_74` (`include/sf64object.h:229`), `Player.sfx.srcPos/srcVel` (`include/sf64player.h:159-160`).
  `Actor.iwork[25]` is `uintptr_t` and some actors keep pointers to other actors in it
  (`src/overlays/ovl_i5/fox_ti.c:694-697`, `:1733-1734`).
- Every object and the players are cleared byte by byte before use (`src/engine/fox_enmy.c:156-210`,
  `src/engine/fox_play.c:2605-2612`), so struct padding holds zeros unless the game writes it.
- `GamePostUpdateEvent` does not fire every frame: `Game_Update` returns early on standby (`src/engine/fox_game.c:355-358`)
  and the event sits inside a branch (`:614`). `GamePreUpdateEvent` fires every frame (`:352`).
- **Listener order:** listeners of one event are sorted by ascending priority (`src/port/hooks/impl/EventSystem.cpp:27-29`),
  so `EVENT_PRIORITY_LOW` runs first and `HIGH` last; equal priorities have no defined order (`std::sort` is not
  stable). The replay listener is now `LOW` so the hash (`NORMAL`) sees the restored seeds on frame 0.
- `python tools/coop/hash_check.py run local/replays/corneria.sfcr` (the 13545-frame Corneria run from WP-03), three
  games at once: runs A and B gave equal hashes on all 13545 frames; run C, with one bit of
  `gActors[59].fwork[29]` flipped at frame 6772, first differed at frame 6772 (region `actors`), then on 2847 frames up
  to 9618, and only in region `actors`. All three replays: `PASS`. 467 s.
- The total hash took 13536 distinct values over 13545 frames.

Derived:

- On one machine and one build, the whole hashed state (not just the WP-03 checkpoint) of a full Corneria run is
  reproduced exactly by inputs plus seeds. This is the "one machine" half of WP-02.
- The poke stopped showing at frame 9619, most likely because the game reused and cleared actor slot 59 (guess, not
  checked). The simulation itself did not change, since only `actors` differed.
- The pointer handling is enough to hide address randomisation between two processes of the same build.

Guess:

- Values in `Actor.iwork` below 2^32 are numbers, values above are pointers. True for 64-bit Windows and Linux with
  ASLR (images and heaps above 4 GiB), not checked on macOS or 32-bit builds.

Unknown:

- Whether the hash holds across two PCs, other compilers or Release builds (WP-02).
- State outside the hashed set that still affects the game (for example overlay statics, `gLevelObjects` progress
  beyond `gObjectLoadIndex`, audio). A desync that the hash misses would show up as equal hashes with different play.
- `gScenery360` is only hashed in `LEVELMODE_ALL_RANGE`; Andross allocates it mid-level
  (`src/overlays/ovl_i6/fox_andross.c:4055`), where it may not be hashed.

## Networking

_Nothing yet._

## Prior work

### 2026-11-01, triage

Observed:

- No Star Fox 64 entry in the Co-op Anything directory (`data/directory.yaml`, checked 2026-11-01).
- No co-op or online fork found among the Starship forks on GitHub (forks list sorted by stars, checked 2026-11-01).
  Notable forks: `izzy2lost/Starship` (Android), `SgtBilko76/Starship-VR`, `keraion/Starship` (Archipelago).
- Star Fox 64 has a built-in 4-player versus mode; `src/engine/fox_versus.c` holds its logic.

Guess:

- The Archipelago fork (`keraion/Starship`) has client networking code that may be worth reading for how it talks to a
  server from inside the port. Not opened yet.
