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

### 2026-11-05, #5 (WP-05), single-player assumptions audit

Read at `d653ee21` (branch `coop`). Full list with a verdict per site: `docs/coop/audit/single-player.md`, generated
by `python tools/coop/sp_audit.py` from `docs/coop/audit/sp-verdicts.tsv` (`--check` fails when it is stale or a site
has no verdict). Rerun it after an upstream sync: line numbers move, and new sites show up as missing verdicts.

Counts: 1799 sites in 409 functions in 43 files. `gPlayer[0]` 1577, `gMainController` 146, `gCamCount` 73,
`gPlayer->` 3. By verdict: sim 1074, flow 326, view 223, ok 93, setup 64, input 19.

Observed:

- **`gCamCount` doubles as the "versus mode" flag.** Code tests `gCamCount != 1` or `gCamCount == 4` to switch on
  versus rules: player-vs-player hits and mercy rules (`src/engine/fox_play.c`, `Player_CollisionCheck`), floor
  handling (`Player_FloorCheck`), arena wrap at 12000 (`Player_CheckBounds360`), no radio (`src/engine/fox_radio.c`,
  `Radio_Draw`), versus HUD and radar (`src/engine/fox_hud.c`, `HUD_Draw`, `HUD_RadarWindow_Draw`), no positional
  sound (`src/engine/fox_edisplay.c`, `Object_SetSfxSourceToPos`), and the down state going to `PLAYERSTATE_VS_STANDBY`
  instead of a level restart (`fox_play.c`, `Player_Update`). There is a separate `gVersusMode` flag, but these sites
  do not use it.
- **Objects are deleted by player 0's camera.** `Actor_Move` calls `Object_Kill` when an actor is behind player 0's
  `cam.eye.z` plus its cull distance or outside player 0's path bounds (`src/engine/fox_enmy.c`, `Actor_Move`);
  `Effect_Move`, `Scenery_Move`, `Sprite_Move`, `Boss_Move` and `Item_Move` do the same. Level objects are loaded
  relative to player 0's path position (`Object_Load`, `Object_LoadLevelObjects`).
- **Level phases are switched by writing `gPlayer[0].state`.** Bosses and level triggers set player 0 to
  `PLAYERSTATE_LEVEL_COMPLETE`, `PLAYERSTATE_START_360` or `PLAYERSTATE_STANDBY` and move player 0's camera, for
  example `src/overlays/ovl_i1/fox_co.c:109` (Granga), `src/overlays/ovl_i2/fox_me.c:1049` (Crusher),
  `src/overlays/ovl_i4/fox_ka.c:919` (Saucerer), `src/engine/fox_enmy.c`, `ItemPathChange_Update` (path split and level
  end gate) and `src/engine/fox_play.c`, `Play_UpdateLevel`. The audit list marks 18 such boss and enemy functions.
- **Enemy fire targets and hits player 0 only.** Projectile effects aim at, collide with and knock back
  `gPlayer[0]` (`src/engine/fox_effect.c`, `Effect_Effect353_354_Update`, `Effect_Effect398_Update`); homing missiles
  seek player 0 (`fox_enmy.c`, `ActorMissileSeek_Update`); event actors read player 0 position, shields and laser
  level as script conditions (`src/engine/fox_enmy2.c`, `ActorEvent_ProcessTriggers`).
- **Wingmen fly with player 0.** `src/engine/fox_360.c`, `ActorAllRange_Update` and `src/engine/fox_hud.c`,
  `ActorTeamBoss_FlyAlongPlayer`.
- **One controller drives the game.** `gMainController` is set at the title screen to the controller that pressed
  Start (`src/overlays/ovl_menu/fox_title.c:171`, `Title_CheckControllers`). `Player_Update` then reads
  `gInputHold`/`gInputPress`/`gControllerRumble` from it for every player (`src/engine/fox_play.c:5886-5888`). Pause,
  intro skip and the R-button radio (`Play_Main`), bomb detonation and charge-shot release (`src/engine/fox_beam.c`)
  read it directly too.
- **Controller index used as player index.** Level code sets rumble with `D_ctx_801779A8[gMainController]` and
  `gControllerRumbleTimers[gMainController]` (for example `src/overlays/ovl_i3/fox_so.c:1652`), while the engine
  reads those arrays by `player->num` (`src/engine/fox_play.c:5891-5897`).
- **One wing animation for all Arwings.** `Display_ArwingWings` draws with `gPlayer[0].jointTable`
  (`src/engine/fox_display.c:633`).
- **Solar heat damages player 0 only** (`fox_play.c`, `Play_UpdateLevel`, the `gPlayer[0].shields--` block).

Derived:

- Setting `gCamCount = 2` for co-op (WP-10) would switch on half of versus mode. WP-10 needs a separate player count
  and must replace the `gCamCount` versus tests with `gVersusMode` or the level id, each one checked.
- Culling and loading are simulation, not drawing: with two players they must use a rule that every client computes
  the same way (for example the leading player, or path progress alone on rails), or clients will delete different
  objects and desync.
- Level phase changes need one place that applies them to all players, which suggests a hook where the game writes
  player 0's state (WP-16, WP-19).
- In co-op the sites marked `view` must use the local player of this client (WP-28), and `input` must use the input
  source of the player concerned (WP-14).

Guess:

- 642 sites have basis `name`: the verdict comes from the function name and file, not from reading the code (mostly
  level enemies and bosses, marked `sim`). A check of those sites for writes to player 0 state or camera found 18
  boss and enemy functions that also switch the level phase; they are now recorded with basis `site`. Other mixed
  functions may remain.
- Most `sim` sites only need "which player" to be chosen (nearest, or a fixed owner); that choice is a game rule
  (WP-07) and the same helper could serve most of them.

Unknown:

- Uses of the player that do not go through these four patterns: `gPlayerNum` outside loops, `Player* player =
  gPlayer` (without `[0]`), `gPlayer[i]` with a fixed `i`, and arrays indexed `[0]` that mean player 0 (`gLaserStrength[0]`,
  `gLifeCount[0]`, `D_ctx_801779A8[0]`). Not counted here.
- `src/port/` (enhancements, cheats, hooks) was out of scope.

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

### 2026-11-05, #19 (WP-21), session handshake: build, ROM, CVars

Read at `1ec6c677` (origin/coop). The handshake code is in `src/port/coop/session/`, the wire form in PROTOCOL.md.

**Observed**

- The ROM version the port knows is the game version of each archive. `Archive::Load` reads a file named `version`
  from the archive: one byte of endianness, then a u32 (`libultraship/src/resource/archive/Archive.cpp:33-41`).
  Torch writes that u32 as the ROM header CRC at offset 0x10 (`tools/Torch/src/Companion.cpp:1243-1251`,
  `tools/Torch/src/n64/Cartridge.cpp:9-10`). Only archives that have a `version` file add to the list
  (`ArchiveManager.cpp:266-268`); `starship.o2r` has none.
- An `sf64.o2r` made from US 1.0 (SHA-1 `d8b10885...`) holds `01 76 92 4C A9`, so its version is `0x76924CA9`.
- `src/port/Engine.h:7` names `0x94F1D5A7` as `SF64_VER_US`. That is not the US 1.0 value above, so
  `GameEngine_HasVersion(SF64_VER_US)` is false with a US 1.0 archive.
- The game does not know its git commit. The only version in the build is `PROJECT_VERSION` 2.0.0
  (`CMakeLists.txt:4`). WP-21 adds `tools/coop/cmake/coop_build_id.cmake`, which writes the commit into a header on
  every build.
- CVars read by game code that change game state or game flow, so they must match (the table in
  `src/port/coop/session/sim_cvars.c`):
  - Damage and shots: `gInvincible` (`src/engine/fox_play.c:977, 5629, 6951`), `gUnbreakableWings`
    (`fox_play.c:938, 955`), `gRapidFire` (`fox_play.c:3331, 3459`, `fox_beam.c:1789`, `fox_enmy.c:2849`,
    `ovl_i3/fox_aq.c:1331`), `gLtoCharge` (`fox_play.c:3334, 3373`), `gRestoreBetaBombExplosion`
    (`fox_beam.c:1320, 1350`).
  - Input: `gInvertYAxis` multiplies `gInputPress->stick_y` inside the player update (`fox_play.c:3760, 4006`,
    `fox_tank.c:385, 674`, `ovl_i3/fox_aq.c:809, 884`).
  - Rules and flow: `gHit64FalcoDead`, `gHit64SlippyDead`, `gHit64PeppyDead` (`fox_play.c:2938-2944`),
    `gHit64SelfDestruct` (`src/mods/hit64.c:7`), `gForceExpertMode` (`ovl_menu/fox_option.c:371`), `gLevelSelector`
    (`fox_message.c:53`, `ovl_menu/fox_map.c:1893, 4116, 5528, 6107, 6751`), `gSfxJukebox` (`fox_option.c:400,
    1665`), `gDebugEnding` (`ovl_menu/fox_title.c:3420`).
  - Bug fixes in level code: `gSzMissileBug` (`ovl_i4/fox_sz.c:186`), `gSzActorFix` (`fox_sz.c:1901`, changes a loop
    bound), `gMaCameraFix` (`ovl_i5/fox_ma.c:7012`, writes `gCsCamAtY`).
  - Writes actor state although it looks visual: `gTeamFaces` sets `iwork[14]` (`fox_demo.c:106`,
    `ovl_i2/fox_me.c:1920` and 13 more sites), `gDisableLOD` sets `iwork[KA_ACTOR_LOW_POLY]`
    (`ovl_i4/fox_ka.c:2642`), `gDisableGorgonFlash` decides whether `gFillScreenAlpha` is set
    (`fox_effect.c:3818`, `ovl_i3/fox_a6.c:837`).
  - Radio and voice: `gVoiceLanguage` sets an audio channel's IO (`src/audio/audio_general.c:546, 2789`); the radio
    reads audio state (`fox_radio.c:615-645`, WP-02). `gEnemyRedRadio` sets `radioRedBox` in a hook
    (`src/port/mods/PortEnhancements.c:319`).
  - Port enhancements in `src/port/mods/PortEnhancements.c`: `gInfiniteLives` (122), `gInfiniteBombs` (129),
    `gHyperLaser` (136), `gScoreEditor` and `gScoreEditValue` (143-147, write `gHitCount`), `gSpawnerMod` (167),
    `gInfiniteBoost` (196-212), `gLaserRangeMult` (218, scales a shot timer), `gRestoreBetaCoin` (227, 243),
    `gRestoreBetaBoostGauge` (287, 309, 392), and the developer tools `gDebugSpeedControl` (18), `gDebugJumpToMap`
    (39), `gDebugWarpZone` (62), `gDebugNoCollision` (88), `gDebugLevelComplete` (95), `gDebugJumpToAllRange` (112),
    `gDebugPause`, `gLToDebugPause`, `gLToFrameAdvance` (173-184).
  - Checkpoints per level: `gCheckpoint.<level>.Set`, `.gSavedGroundSurface`, `.gSavedPathProgress` (float),
    `.gSavedObjectLoadIndex`, read at level start (`fox_play.c:4709-4716`).
- Left out because they only change drawing or sound output: `gRadioCommBox.expand` (`fox_hud.c:213, 2279, 2656`,
  `fox_radio.c:133, 746-811`, all positions of draw calls), `gDisableStarsInterpolation` (`fox_bg.c:255, 346`),
  `gCockpitOpacity` (`fox_display.c:726`), the speaker positions and volumes in `src/audio/`, and the resolution,
  HUD and window CVars in `src/port/`. WP-02 found no hash difference with another interpolation rate or window size.
- The menu's "Set Checkpoint" writes `gCheckpoint.<level>.gSavedPathProgress` twice, first as an integer with
  `gGroundSurface`, then as a float (`src/port/ui/ImguiUI.cpp:896-897`). `gSavedGroundSurface` is never written, so
  the game reads its default (the current global) there. An upstream bug; harmless for the handshake, which reads
  the same on both sides.
- Test: `python tools/coop/session_test.py` gives 9/9 (Windows 11, MSVC 19.44, Debug). Three bugs put in on purpose
  (CVars not compared, refusal not held until it arrives, platform refusing) each failed `coop_session_test sim`.

**Derived**

- US 1.0 and US 1.1 should give different versions, because Torch takes the header CRC of each ROM. Only US 1.0 was
  seen.
- A CVar that is not in the table and changes the game will not be caught by the handshake, only later by state
  hashes (WP-23).

**Guess**

- `0x94F1D5A7` is US 1.1. Not checked; the text shows it as "US" together with the hex value.

**Unknown**

- Whether Windows and Linux builds compute the same game. Until it is checked, a different platform only warns.
- CVars changed after the handshake, from the menu. Nothing stops that yet; WP-22 or WP-25 should lock the table
  while a session runs.
- Mods: an `.o2r` in `mods/` with a `version` file shows up in the ROM list, one without does not, and nothing
  compares their content. `gEnhancements.Mods.AlternateAssets` (`src/port/Engine.cpp:263-469`) swaps assets; whether
  any of them change the game is not checked.
- The game glue `CoopIdentity_FromGame` (`src/port/coop/session/identity_game.c`) compiles and links into the game,
  but nothing calls it yet and it has not run. WP-24 or WP-25 will call it when a session starts.
- Builds without git get the build id `unknown`, and two of them count as the same build. Two `-dirty` builds of the
  same commit also match, whatever their changes.

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
