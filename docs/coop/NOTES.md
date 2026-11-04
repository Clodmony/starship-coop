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
