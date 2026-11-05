# Notes

Shared lab notebook. Add findings about the game and the port as you make them, newest at the top of each section,
in the same PR as the work. Each entry: date, issue, then **Observed** (with file:line or a link), **Derived**,
**Guess** and **Unknown** as separate lists. Remove nothing; mark an entry `[wrong, see <date>]` if it turns out
false. Settled knowledge moves into DESIGN.md.

Private notes (your paths, your PC, your logs) go in `local/`, not here.

## Build and run

### 2026-11-04, #1 (WP-01), Windows build

Identity: ROM US 1.0 (SHA-1 `d8b1088520f7c5f81433292a9258c1184afa1457`, `config.yml:34`), commit `ba0781d7`,
Windows 11 Home 10.0.26200, VS 2022 Build Tools, MSVC 19.44.35229.0 (v143), Windows SDK 10.0.26100.0, CMake 4.4.4,
Python 3.13, Git 2.55, generator "Visual Studio 17 2022", Debug, graphics backend DirectX 11 (default).

Observed:

- `git submodule update --init --recursive`, then `python tools/coop/build.py --assets --quiet` builds everything in
  one go: `build: OK (Windows, Debug, 10m22s)` on a fresh build directory. Outputs: `build/x64/Debug/Starship.exe`,
  `build/x64/sf64.o2r`, `build/x64/starship.o2r`, `build/x64/gamecontrollerdb.txt`.
- The ROM from a common dump was in v64 byte order (16-bit swapped, header `37 80 40 12`) despite a `.n64` file name.
  Its SHA-1 matches no entry in `config.yml`. After swapping every byte pair (header `80 37 12 40`) it matches US 1.0.
  `build.py` expects `baserom.z64` in the repo root and does not check the byte order itself (`tools/coop/build.py:120`).
- On Windows the app directory is the working directory: `GetAppDirectoryPath` returns `"."` unless `NON_PORTABLE` is
  set (`libultraship/src/Context.cpp:458-500`). Started with `local/test-a/` as working directory and the two `.o2r`
  files copied there, the game wrote `starship.cfg.json`, `default.sav`, `imgui.ini`, `logs/` and `mods/` into that
  folder and nothing next to the exe. This is how test instances stay apart from the owner's own setup.
- The game booted to the title sequence (window "Starship (DirectX 11)"), checked by a screenshot after 25 s.
- The game log reports `Failed add SDL game controller mappings from "./gamecontrollerdb.txt"` when that file is not in
  the working directory; it is built into `build/x64/`. Copy it into the test folder to get the mappings.
- Torch logs `[error] > Failed to process ast_audio/audio_setup` while building `sf64.o2r`; the build still ends OK.

Unknown:

- Whether the `audio_setup` error affects sound in game (not listened to), and whether upstream builds show it too.
- Release config, Ninja (`--ninja`), Linux and macOS: not tried in this session.

### 2026-11-04, #1 (WP-01), Linux build

Identity: ROM US 1.0 (SHA-1 `d8b1088520f7c5f81433292a9258c1184afa1457`, `config.yml:34`), commit `ba0781d7`, Linux,
GCC, Debug, graphics backend OpenGL (default).

Observed:

- On a fresh worktree of `origin/coop`, `python tools/coop/build.py --assets --quiet` fetched the submodules and built
  everything: `build: OK (Linux, Debug, 2m23s)`. Outputs: `build-cmake/Starship`, `build-cmake/gamecontrollerdb.txt`,
  and `sf64.o2r` and `starship.o2r` in both the repo root and `build-cmake/`.
- The ROM was in v64 byte order (header `37 80 40 12`, `.n64` name) and its SHA-1 matched no entry in `config.yml`.
  `dd conv=swab` gives header `80 37 12 40` and the US 1.0 hash.
- On Linux the `SHIP_HOME` environment variable sets the app directory (`libultraship/src/Context.cpp:481-486`).
  Without it, and with `NON_PORTABLE` off, the directory is `"."` (`libultraship/src/Context.cpp:500`). With
  `SHIP_HOME=<repo>/local/test-boot` and the two `.o2r` files and `gamecontrollerdb.txt` copied there, the game wrote
  `starship.cfg.json`, `default.sav`, `imgui.ini` and `logs/` into that folder and added nothing to
  `$HOME/.local/share` or `$HOME/.config`.
- The game reached the title screen with no input, checked by reading state through gdb and by a screenshot of the
  game window. Sampled about every 7 s: `gGameState` 2 (`GSTATE_TITLE`), `gTitleState` 2, `sCutsceneState` 1, 2, 4, 5,
  then 0 (`TITLE_SCREEN`) at system frame 3090 (`src/overlays/ovl_menu/fox_title.h:20-26`). No crash.
- The attract sequence plays before the title screen unless `gMainController` is -1 or `gGoToTitle` is set
  (`src/overlays/ovl_menu/fox_title.c:204-211`, `src/overlays/ovl_menu/fox_title.c:2156`).
- That run forced `SDL_VIDEODRIVER=x11` so the window could be captured. A plain launch in a Wayland session picked
  the `wayland` driver and also ran (`gGameState` 2, frames advancing).
- Torch logs `[error] > Failed to process ast_audio/audio_setup` on Linux too. `AudioContextFactory::parse` stores the
  audio tables and then returns `std::nullopt` on its normal path
  (`tools/Torch/src/factories/naudio/v1/AudioContext.cpp:44`), and `Companion` logs every empty result as failed
  (`tools/Torch/src/Companion.cpp:319-320`). All three table lines print before the error. The running game has the
  tables: 66 sequences, 33 sound fonts, 4 sample banks.

Derived:

- The `audio_setup` error is a false alarm in Torch's log, not a sign of missing audio data.

Guess:

- Upstream builds log the same error, because the code is in the Torch submodule. Not checked.

Unknown:

- How the sound actually plays: both runs used `SDL_AUDIODRIVER=dummy`, and nobody has listened to it on Linux.
- Release config and macOS: not tried in this session.

## Engine and players

### 2026-11-05, #6 (WP-06), on-rails: player position, path progress, camera

Read from the source only; nothing was run. Line numbers are for `origin/coop` at 1ec6c677.

**Observed**

- *Two z values per player.* `Player.pos.z` is the player's z in level space; `trueZpos` is `pos.z + camDist`, the
  value collision and shots use (`include/sf64player.h:190`, `:227`; set in `Player_UpdatePath`,
  `src/engine/fox_play.c:3649`; shot hit test `src/engine/fox_beam.c:857`). `camDist` is 0 in normal flight and only
  changes in cutscenes such as the warp exit (`src/engine/fox_demo.c:469-490`).
- *Path progress is per player, then copied into one global.* `Player_UpdatePath` (`src/engine/fox_play.c:3627`)
  adds `-vel.z` to `player->zPath` and writes `gPathVelZ`, `gPathProgress` and `gPathTexScroll` from that player
  (`:3632-3637`). Every caller of it overwrites the globals: `Player_UpdateOnRails` for the Arwing (`:5716`) and the
  Blue Marine (`:5746`), the Landmaster (`src/engine/fox_tank.c:1159`), cutscenes (`src/engine/fox_demo.c:1886`,
  `src/overlays/ovl_i5/fox_ti_cs.c:358`) and Macbeth (`src/overlays/ovl_i5/fox_ma.c:5544`, `:7043`, `:7679`,
  `:7709`, `:7751`).
- *Forward speed is the player's own.* `Player_MoveArwingOnRails` (`src/engine/fox_play.c:3981`) builds `vel` from
  `baseSpeed`, `boostSpeed` and the stick, then adds `vel.z` to `pos.z` (`:4173`). Boost and brake therefore change
  how fast that player's `zPath`, and with it `gPathProgress`, grows.
- *The flight box follows the path centre.* `pos.x` is clamped to `xPath ± pathWidth`, `pos.y` to
  `yPath + pathFloor … yPath + pathHeight` (`src/engine/fox_play.c:4148-4170`). The box sizes come from
  `Player_Setup` (`:4566`, sizes at `:4621-4659`; Arwing 700 wide, Landmaster 500).
- *Path turns.* `xPath`/`yPath` ease towards `xPathTarget`/`yPathTarget` in `Player_UpdatePath`
  (`src/engine/fox_play.c:3671-3674`); the step taken this frame goes into the globals `gPathVelX`/`gPathVelY`,
  which effects add to their own velocity (`src/engine/fox_effect.c:2459-2460`, `:2513-2514`, `:3038-3039`).
  `pathChangeTimer` turns the view with `yRot_114`/`xRot_120` (`fox_play.c:3676-3683`). The path-turn and
  path-split items set all of this on **`gPlayer[0]` only** (`src/engine/fox_enmy.c:2550-2591`); Macbeth sets it in
  its own code (`src/overlays/ovl_i5/fox_ma.c:6613-6775`).
- *Level objects load by the global progress.* `Object_LoadLevelObjects` (`src/engine/fox_enmy.c:548`) loads the
  next entries of the level's object list whose `zPos1` lies in `[gPathProgress, gPathProgress + 200]` (`:630`),
  and checks `gPlayer[0]`'s path turn and `xPath` (`:592-595`, `:631-633`). It runs only while `gPlayer[0]` is not
  in the level intro, and culling is switched on by `gPlayer[0].state` (`:3146-3155`).
- *Culling uses player 0's camera.* Objects are removed or skipped when they are behind
  `gPlayer[0].cam.eye.z` in path space (`src/engine/fox_enmy.c:2694-2695`, `:2798`).
- *Drawing.* Objects are drawn at `obj.pos.z + gPathProgress` (`src/engine/fox_edisplay.c:983` and many more).
  An Arwing is drawn at `trueZpos + zPath` with **its own** `zPath` (`src/engine/fox_display.c:1417`, `:1466`,
  `:1500`). Ground height also adds `gPathProgress` (`src/engine/fox_play.c:1194-1237`).
- *Camera.* `Camera_Update` (`src/engine/fox_play.c:6664`) picks `Camera_UpdateArwingOnRails` (`:6211`) for the
  Arwing. Eye and look-at x/y follow the player's offset from `xPath`/`yPath`, scaled by `unk_148`/`unk_14C`
  (0.777, set in `Player_Setup`); eye z is a constant 400 (`:6258`), look-at z is `trueZpos + gPathProgress - 1`
  (`:6257`). The camera also reads the stick through `gInputPress` (`:6222-6223`). The frame's view is built from
  `gPlayer[gPlayerNum]`: its camera offset, rotated by its `yRot_114`/`xRot_120`, plus its `trueZpos + zPath`
  (`src/engine/fox_display.c:1833`, `:1866-1888`).
- *Update order.* `Play_Update` runs `Player_Update` for every player, then `Object_Update`, then `Camera_Update` for
  every player, all in loops over `gCamCount` (`src/engine/fox_play.c:7033-7045`). Outside versus mode,
  `Player_Update` points `gInputPress`/`gInputHold` at `gMainController` for every player (`:5881-5889`).
- *Wrap.* When `zPath` passes 500000 without a boss, `zPath`, `pos.z` and the object list index are reset
  (`src/engine/fox_play.c:3643-3648`).

**Derived (reasoning, not tested)**

- On rails the world is drawn in "path space": level z plus progress. In flight `pos.z` falls by `vel.z` and `zPath`
  rises by the same amount, so `pos.z + zPath` stays the same and the Arwing stays at a fixed depth in front of a
  camera whose eye sits at z = 400. Wherever else `pos.z` is written on rails (knockback, cutscenes) this can drift.
- With two players the global progress is whatever the last player in the loop wrote, so player 1 would drive object
  loading, ground scroll and every object's draw position, while path turns and culling still follow player 0.
- If the two players fly at different speeds (one boosts, one brakes), their `zPath` values drift apart. Each Arwing
  is still drawn at its own fixed depth, but objects are drawn with the shared progress, so the other player would
  look closer than it is and its shots would not line up with where it is drawn. Collision uses level z and would
  stay correct.
- Player 2 has its own `xPath`/`yPath`. Since path items only turn `gPlayer[0]`, player 2's flight box and camera
  would stay on the old path centre after a turn.
- `Camera_Update` does not set `gInputPress` again, so each player's on-rails camera sways with whatever controller
  the last `Player_Update` used.
- Simplest model for WP-12: one shared path. Player 0 (the host or a fixed slot) owns `zPath`, `xPath`, `yPath`,
  the turn state and the speed along the path; the other players copy them each frame and only move freely in x/y
  inside the box. Boost and brake would then either be shared, or become a per-player z offset from the shared
  progress (a field like `camDist`). That is a design choice for WP-07/WP-12, not decided here.

**Guess**

- The 0.777 factor in the camera is why the camera lags the Arwing towards the box edges; with two players, a camera
  that frames both would need a different factor or a different centre.
- Landmaster and Blue Marine use the same path fields (they call `Player_UpdatePath`), so the shared-path model would
  carry over to Titania, Macbeth and Aquas. Their cameras are separate functions and were not read.

**Unknown**

- Whether boost/brake speed must stay per player for the game to feel right, or can be shared.
- Which other code writes `pos.z` on rails (knockback, damage, somersault `Player_PerformLoop`); not traced.
- How the level intro and the switch from rails to all-range (`PLAYERSTATE_START_360`) set `zPath` and `pos.z` for
  more than one player.
- Cockpit view (`Camera_UpdateCockpitOnRails`, `src/engine/fox_play.c:6287`), tank and Blue Marine cameras: not read.
- Whether the 500000 wrap is ever reached in a normal campaign.

### 2026-11-01, project setup

Observed: see DESIGN.md section 2 (player array, `gCamCount`, object arrays, RNG, event hooks, `USE_NETWORKING`).

Unknown: everything listed in DESIGN.md section 2, "Unknown".

## Determinism

_Nothing yet._

## Networking

### 2026-11-05, #18 (WP-20), UDP transport

Identity: no game and no ROM involved, two or three processes on one machine, UDP over loopback. Commit `12329623` on
Linux, GCC, Debug. Commit `fdd01ee1` in CI on Linux (GCC) and Windows (MSVC), both Release.

**Observed**

- All 17 scenarios of `tools/coop/net_test.py` pass, including the `hell` profile: the relay dropped 710 of 3042
  datagrams and duplicated 120, and all 300 reliable messages per direction still arrived in order, in about 10 s.
- A 2 s outage, in either or both directions, loses no reliable message and does not end the connection.
- Timeouts fire before the limit you would expect from the outage: with a 2000 ms timeout, both sides reported it
  1.8 s after the link died, because the clock runs from the last datagram heard, up to one ping (250 ms) earlier.
- Two-way traffic hides a missing ack-only datagram. A build that never sent `ACK` passed the `lan` scenario, because
  acks ride on data and pings. Only one-way traffic shows it, so the virtual network now has that case.
- SDL2_net's shared CMake target does not pass SDL2 on to whatever links it: linking `coop_net_test` against
  `SDL2_net::SDL2_net` alone failed with `undefined reference to symbol 'SDL_GetTicks'`
  (`tools/coop/nettest/CMakeLists.txt`).
- `SDLNet_UDP_GetPeerAddress(socket, -1)` returns the port picked for `SDLNet_UDP_Open(0)`
  (`src/port/coop/net/transport_sdlnet.c`).
- All 17 scenarios pass on Windows too. The virtual network gives the same counts on GCC Debug, GCC Release and MSVC
  Release: dropped 113877, duplicated 21597, corrupted 10131.
- On Windows `stray_datagrams` counted 1954 of 2257 junk datagrams as rejected; on Linux all 3000. In the Windows run
  301 of the 2257 were larger than the 1201-byte receive buffer, which leaves 1956.
- CMake's Visual Studio generator leaves the targets of an `EXCLUDE_FROM_ALL` directory out of the solution, so
  `cmake --build --target` fails with `MSB1009: Project file does not exist`. Ninja does not mind. Put
  `EXCLUDE_FROM_ALL` on the target instead (`tools/coop/nettest/CMakeLists.txt`).

**Derived**

- A goodbye that gets through ends a connection at once. Without one, a dead peer costs the full timeout.
- Under a long one-way outage the resend count grows quickly (about 1200 resends in the 2 s outage tests, with 120
  messages sent per second in all). The backoff bounds it, but a bigger reliable lane would need real congestion control.

**Guess**

- Windows drops a datagram larger than the receive buffer inside `recvfrom`, so it never reaches the core and is not
  counted. Linux cuts it to the buffer size, and the core rejects it. Harmless either way: traffic was unaffected.

**Unknown**

- The other 2 of the 1956 junk datagrams that Windows did not count.
- Anything between two real PCs: NAT, firewalls, real loss. Every run so far is loopback through the relay.
- The longest stall of the game loop, which `timeoutMs` (5000 by default) has to outlast. Level loads may matter.
- macOS: `Brewfile` and `.github/macports.yml` list SDL2_net, but nothing was built or run there.

### 2026-11-05, #18 (WP-20), boot with networking on

Identity: ROM US 1.0 (SHA-1 `d8b1088520f7c5f81433292a9258c1184afa1457`, `config.yml:34`), commit `326b4572`, Linux,
GCC 16.2.1, Debug, graphics backend OpenGL, `USE_NETWORKING` on, `SDL_VIDEODRIVER=x11`, `SDL_AUDIODRIVER=dummy`.

**Observed**

- The game reached the title screen with no input. Sampled every 7 s: `gGameState` 2, `gTitleState` 2,
  `sCutsceneState` 1, 2, 3, 4, 5, then 0 (`TITLE_SCREEN`) in the sample at system frame 3131. It was still running at
  frame 3762, when the probe ended it. No signal stopped it except the probe's own.
- A screenshot of the game window (title "Starship (OpenGL)") showed the title screen.
- The binary has 13 `CoopNet` symbols and links SDL2_net (`ldd`). The game log has no error or critical lines.
- With `SHIP_HOME` set, no file named `starship*` or `default.sav` appeared in `$HOME/.local/share` or `$HOME/.config`.
- `gSysFrameCount` rose by about 210 every 7 s, so 30 system frames per second on the title sequence.
- SDL is the only audio backend on Linux (`libultraship/src/audio/Audio.cpp:35-39`), so `SDL_AUDIODRIVER=dummy` keeps
  a test run silent.
- With `kernel.yama.ptrace_scope` at 1, gdb cannot attach to a game started by something else. Start the game from
  gdb instead. To stop it for a sample, use `handle SIGINT stop print nopass`: `noprint` implies `nostop`, and the
  first run of this check sat in `continue` until its timeout because of that.
- EU 1.0 does not work as a base ROM. `config.yml` sends its output to `./mods/sf64eu.o2r`, so no `sf64.o2r` comes
  out and the asset step fails.

**Derived**

- Building the transport in and linking SDL2_net does not stop the game from booting on Linux.

**Unknown**

- The transport inside the running game. Nothing calls it, so this run opened no socket and never called
  `SDLNet_Init`. WP-21 is the first code to do that.
- A boot with networking on under Windows or macOS. CI builds the game on Windows but does not start it.
- Anything past the title screen.

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
