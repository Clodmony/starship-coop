# Notes

Shared lab notebook. Add findings about the game and the port as you make them, newest at the top of each section,
in the same PR as the work. Each entry: date, issue, then **Observed** (with file:line or a link), **Derived**,
**Guess** and **Unknown** as separate lists. Remove nothing; mark an entry `[wrong, see <date>]` if it turns out
false. Settled knowledge moves into DESIGN.md.

Private notes (your paths, your PC, your logs) go in `local/`, not here.

## Build and run

### 2026-11-06, #54, what SHIP_HOME reaches, and how a replay file can end

Identity: ROM US 1.0, on commit `a69c811a`, Linux, GCC, Debug, OpenGL in software, no window.

Observed:

- `sf64.o2r` is looked up in the app directory only (`src/port/Engine.cpp:78`); `starship.o2r` also falls back to the
  working directory (`src/port/Engine.cpp:79`, `libultraship/src/Context.cpp:511-526`). With `SHIP_HOME` at a folder
  without `sf64.o2r`, and no window, the game exited with code 1 within 2 s and wrote nothing in either folder.
- With `SHIP_HOME` at a folder holding `sf64.o2r`, and the tools passing it through, `default.sav`, `logs/`, `mods/` and
  `starship.cfg.json` landed in that folder, plus `imgui.ini` on the runs of 100 frames or more (not on those of 0 or
  30). Besides the data files copied in, the run folder got only the report and the hash log. Seen through `replay.py`,
  `hash_check.py` and `determinism.py`; the three runs of `hash_check.py` shared that one folder.
- A header-only replay reported `PASS` with 0 frames, and `determinism.py` then printed `det: PASS`, "equal to base on
  all 0 frames". `hash_check.py` failed it only because its poke changed nothing, while all three replays said `PASS`.
- A file cut inside a frame also reported `PASS`, with the whole frames before the cut as `frames`.
- A recording stopped with SIGKILL ended inside a frame in 2 of 3 tries (16 and 32 bytes left over, the third on a frame
  edge). The file is flushed every 60 frames (`src/port/coop/test/replay.c:260`) and in blocks in between. No report is
  written then.
- After SIGTERM a recording game ran its `atexit` handler (report `RECORDED`, whole frames only), yet it had not exited
  40 s later and needed SIGKILL. One run measured this.
- A replay with no frames to play took 17 to 21 s in `replay.py`.

Derived:

- A folder without `sf64.o2r` fails loudly; the dangerous `SHIP_HOME` is one that looks like a real install. A test of
  this bug needs a decoy holding that archive, or it shows nothing.
- With `SHIP_HOME` passed through, the game reads its config from there (`libultraship/src/Context.cpp:181`) and its
  mods too (`src/port/Engine.cpp:110`), so a config a tool writes into the run folder is ignored. Not run for that
  case: `determinism.py`'s `opengl` and `window` variants write one.

Guess:

- Most of those 17 to 21 s is `run_game` waiting 15 s for the game to exit after SIGTERM, before it uses SIGKILL.

Unknown:

- Why the game does not exit after SIGTERM.
- Windows and macOS: none of this ran there. `SHIP_HOME` is read on Linux and macOS only
  (`libultraship/src/Context.cpp:471-486`).

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

### 2026-11-06, #56, audit regenerated after WP-10 and WP-11

Read at `a69c811a`. `python tools/coop/sp_audit.py --check` passes again. This moved line numbers and corrected the
notes below; it is not a recheck. The verdicts stay a starting list: a sample of 45 found 5 wrong, and the `flow` group
is not usable as a checklist yet.

Counts now: 1788 sites in 407 functions in 43 files. By verdict: sim 1074, flow 326, view 223, ok 93, setup 53,
input 19.

Observed:

- **Why two verdict rows matched no site.** WP-10 replaced the last `gCamCount` in `Player_InitializeAll`
  (`src/engine/fox_play.c:2610`) and `Play_Update` (`fox_play.c:7039`, `:7054`) with `CoopPlayers_Count()`, and neither
  function has any other pattern. The `// COOP: was gCamCount` comments are skipped. No site moved to another audited
  function: the code stayed, and the choice of count went to `src/port/coop/game/players.c:45-50`, outside the audit's
  scope. So both rows are gone; what `Player_InitializeAll`'s row said now stands in the `Play_Init` row. All game code
  changes since the audit (`d653ee21`) are in `fox_play.c` and `fox_display.c`: 11 `gCamCount` sites went away (5 in
  `Display_Update`, 2 in `Play_Init`, 2 in `Play_Update`, 1 in `Player_InitializeAll`, 1 in `Play_Main`), none came in.
  The notes of the `Play_Init`, `Play_Main` and `Display_Update` rows still described those loops and were corrected.
- **`gMainController` is not the controller that pressed Start.** It is written in two places only:
  `Title_Setup` (`src/overlays/ovl_menu/fox_title.c:171`) and `Title_UpdateEntry` (`:240`, every title frame). Both
  call `Title_CheckControllers` (`:388-400`), which returns the lowest index with `gControllerPlugged[i] == 1`, or -1.
  It reads no button. All controller reads in `fox_title.c` use that index (`:884`, `:3374`, `:3405-3406`,
  `:3446-3464`), so the title reads no other controller.
- **On this port that index is always 0.** `Controller_UpdateInput` sets `gControllerPlugged[i] = osContGetStatus(i)`
  for all four (`src/sys/sys_joybus.c:69`), and `osContGetStatus(x)` is defined as `true` (`:63`, `// LTODO: Fix this`).
  It runs before `Game_Update` each frame (`src/sys/sys_main.c:241`, `:248`), so `Title_CheckControllers` returns 0
  and its -1 is not reached. Read, not run.
- `Title_Setup`'s old note also said every later read uses `gMainController`. Versus does not: `Player_Update` takes
  `gInputHold`, `gInputPress` and `gControllerRumble` from `player->num` when `gVersusMode` is set
  (`fox_play.c:5887-5895`), and pause reads R per controller (`fox_play.c:7234`).
- **`Play_Main` has no radio input.** Its `gMainController` reads are Start (intro skip on a cleared level, `:7191`;
  pause, `:7214`; unpause, `:7244`) and R while paused (`:7224`), which toggles `gShowReticles[0]`
  (`fox_play.c:7226`; drawn at `src/engine/fox_display.c:2072`). The radio answer is C-right, in
  `ActorAllRange_SpawnSupplies` (`src/engine/fox_360.c:667`) and the `EVC_PRESS_CRIGHT` event condition
  (`src/engine/fox_enmy2.c:2695`); their rows were already right. The same mistake was in the `Play_Main` verdict row.
- **A new site only shows as missing when no row covers it.** Tried with temporary sites, reverted afterwards.
  Shown as missing (`--check` names it, `--todo` lists it, and it stays red after a rerun): a new function in
  `fox_play.c`, `gPlayer->` in `Player_Update` (its rows are per pattern), a new tracked file. Not shown as missing:
  a new function in `fox_co.c` (took `sim`, basis `name`, from the `ovl_i*` row) or `fox_demo.c` (took `flow`), and
  `gPlayer[0]` added to `Player_CheckBounds360` (took that row's `setup` and a note that does not describe it). For
  those, `--check` only calls the file stale and passes after a rerun. A file git does not track yet is not scanned:
  `--check` stays green. 678 of the 1788 sites sit under a `*` row, in 21 files.

Derived:

- WP-14 cannot use `gMainController` to find who pressed Start, and on this port neither it nor
  `gControllerPlugged` says which controllers exist.
- After a rerun, the diff of `single-player.md` is the only place new sites show up. The generated header says so now.

Unknown:

- Which 5 verdicts the sample found wrong, beyond the notes #56 names, and how the verdicts outside the sample stand.
  Nothing here rechecked them.
- Why the `flow` group is not usable as a checklist yet: #56 does not say. 113 of its 326 sites have basis `name`.
- Which physical pad lands on index 0: that is libultraship's mapping, not read here.

### 2026-11-06, #60, which radio lines a wingman says on its own

Read at `95962d04`; nothing was run. "Wingman" means Falco, Slippy and Peppy, the three a player can take (decision
0005); Katt and Bill are left out. Lines are named by message id or described, not quoted. Paths in the tables drop
`src/engine/` and `src/overlays/`.

Scripted lines are ROM data, not source, so they are cited as level, script index and command index (`aiIndex / 2`),
for example Corneria 17.8. The tables are the `SF64:SCRIPT` assets (`assets/yaml/us/rev0/ast_corneria.yaml:445` for
Corneria), picked per level in `ActorEvent_ProcessScript` (`src/engine/fox_enmy2.c:1075-1122`). They were decoded from
a US 1.0 `sf64.o2r` with a throwaway script, not kept. Its command format is `EVENT_CMD` (`include/sf64event.h:92`).
Script counts come from walking each script's branches, so they say what can run, not what a given run plays.

**Observed: how a line reaches the radio**

- *One door.* Everything in play ends in `Radio_PlayMessage(msg, character)` (`src/engine/fox_radio.c:58-130`). Four
  routes add their own conditions. `AllRange_PlayMessage` needs an idle radio and player 0 not in standby
  (`src/engine/fox_360.c:89-96`). `ActorAllRange_PlayMessage` needs `!gHideRadio`, `gActors[0].state == STATE360_2` and
  player 0 not in standby (`:479-483`). `ActorEvent_SetMessage` needs an idle radio or another speaker
  (`src/engine/fox_enmy2.c:29-34`). The script op `EVOP_PLAY_MSG` needs `gPlayer[0].state == PLAYERSTATE_ACTIVE`, reads
  the speaker from the low 9 bits, and moves on either way, so a skipped line is lost (`:1333-1339`).
- *No queue.* A message starts with a priority character (`src/engine/fox_radio.c:64-78`). While the radio is busy, a
  lower priority is dropped and an equal or higher one replaces the current line (`:43-56`, `:97-99`). Of the 363
  message ids wingmen use, 304 are priority 1, 50 priority 2, the 6 low-shield comfort lines priority 0 and the 3 lines
  for Fox's death priority 3.
- *The radio is game state.* `Radio_Draw` advances `gRadioState` in the draw path (`src/engine/fox_game.c:569`), 31
  lines outside `fox_radio.c` read it, and it is hashed with `gRadioMsgId` (`src/port/coop/sync/state_hash.c:246-247`).
  The 2026-11-04 determinism note covers its link to audio.
- *No wingman on the map.* The planet briefing has its own path without `Radio_PlayMessage`
  (`src/overlays/ovl_menu/fox_map.c:5954`). Its 14 message pairs (`:203-213`) read as the General and Fox.

**Observed: how a line picks its speaker**

| Way | Where |
|---|---|
| Named in the call | Most level, boss and cutscene lines |
| Named in the script | Every `EVOP_PLAY_MSG` |
| The reacting actor | `this->aiType` in `ActorAllRange_*` and `ActorTeamBoss_*`, `this->iwork[12]` in `ActorEvent_*` |
| The target of a chasing enemy | `this->aiIndex`, `engine/fox_360.c:1710-1752` |
| Random living wingman: `RAND_INT(2.9f) + 1` until `gTeamShields[id] > 0`, after checking one is up | `engine/fox_play.c:5844-5848`, `engine/fox_beam.c:2287-2289`, `engine/fox_demo.c:1612-1614`, `ovl_i1/fox_co.c:565-567` |
| First living in a fixed order | `engine/fox_enmy.c:1803-1809` (Peppy, Slippy, Falco), `ovl_i3/fox_a6.c:1031-1036` and Meteo 82 (Falco, Slippy, Peppy) |
| Rotating slots, a slot is skipped if its speaker is down | `D_ctx_80177A10[8]`, `ovl_i6/fox_sy.c:348-370`, `:1305-1325` |
| One-in-four roll, Falco or Bill | `ovl_i4/fox_ka.c:2342-2358` |
| Three calls in one frame | `ovl_i6/fox_ve2.c:467-469`, see Derived |

**Observed: what happens when the speaker is down**

- *The gate.* In `GSTATE_PLAY`, a line for Falco, Slippy or Peppy returns silently when `gTeamShields[id] <= 0` and
  `!= -2` (`src/engine/fox_radio.c:80-95`). No stand-in, no delay. Other speakers are not gated, and neither are the
  `RCID_*_RED` wingman ids, which no call or script uses.
- *-2 means away, not down.* It is only ever written for Slippy: Titania start (`src/engine/fox_play.c:2691`), Sector X
  boss (`src/overlays/ovl_i2/fox_sx.c:113`). The gate lets it speak and the portrait shows no DOWN. What -1 and 0 mean
  is #61.
- *Stand-in lines.* Level-complete cutscenes test `gTeamShields` first and let ROB speak instead: 86 sites in 12
  functions, usually one line for -1 (ship in for repairs or in the docking bay) and one for 0 (repairs complete), for
  example `src/overlays/ovl_i6/fox_sy.c:2353-2370`. `Andross_80193C4C` (`src/overlays/ovl_i6/fox_andross.c:4303-4342`),
  `Katina_LevelComplete` (`src/overlays/ovl_i4/fox_ka.c:2046`) and Slippy in `Titania_LevelComplete`
  (`src/overlays/ovl_i5/fox_ti_cs.c:439`) have none, so the gate just drops the line.
- *Skipped scenes.* `EVC_FALCO_ACTIVE`, `EVC_PEPPY_ACTIVE` and `EVC_SLIPPY_ACTIVE` branch only when `gTeamShields > 0`
  (`src/engine/fox_enmy2.c:2408-2424`). 216 of the 245 reachable scripted lines sit behind one, usually with the whole
  scene: no branch, no teammate actor (Corneria 17). The other 29 rely on the gate; 16 of them are in Aquas, whose init
  sets all three to 255 (`src/overlays/ovl_i3/fox_aq.c:437`).
- *Forced past the gate.* Some code writes `gTeamShields[id] = 1` around the call. Each retreat does this, then writes
  -1 (`src/engine/fox_360.c:1448-1466`, `src/engine/fox_hud.c:4247-4262`, `src/engine/fox_enmy2.c:2077-2090`). So does
  `Aquas_CsLevelComplete`, restoring the old value (`src/engine/fox_hud.c:6064-6088`), and Slippy in the Titania boss
  (`src/overlays/ovl_i5/fox_ti.c:5053-5134`).
- *The portrait.* A wingman speaking with `gTeamShields <= 0`, not -2, flashes DOWN with the wrench, and the box always
  shows a shield bar from `gTeamShields` (`src/engine/fox_radio.c:729-760`). Only a line already playing or forced gets
  that far.

**Observed: story lines.** Nothing here reads how the wingman is flying, only whether it is up.

| Trigger | Where | Reads | Count |
|---|---|---|---|
| Level-start cutscene | `Corneria_LevelStart` (`ovl_i1/fox_co.c:3135`, `:3190`, `:3221`), `Meteo_LevelStart` (`ovl_i2/fox_me.c:2293`, `:2299`), `Area6_LevelStart` (`ovl_i3/fox_a6.c:2565`) | `csTimer`, `gCsFrameCount` | 6 |
| Level-complete cutscene | every `*_LevelComplete*`, `Cutscene_CoComplete2` (`engine/fox_demo.c:1203-1219`), `Aquas_CsLevelComplete`, `Andross_80193C4C`, `func_demo_80048AC0` (`engine/fox_demo.c:25-75`) | `gCsFrameCount` | 59 |
| All-range event clock | `Bolse_UpdateEventHandler` (`ovl_i4/fox_bo.c:241-259`, `:382`), `Fortuna_UpdateEvents` (`ovl_i4/fox_fo.c:126`), `Katina_UpdateEvents` (`ovl_i4/fox_ka.c:2332`, `:2356`), `SectorZ_UpdateEvents` (`ovl_i4/fox_sz.c:578`), `SectorZ_EnemyUpdate` (`:365`), `Venom2_UpdateEvents` (`ovl_i6/fox_ve2.c:176`) | `gAllRangeEventTimer`, `gStarWolfMsgTimer`, actor timers | 14 |
| Boss clock or boss state | 34 functions in `src/overlays/`, for example `Corneria_CoGranga_Update` (`ovl_i1/fox_co.c:691-704`), `SectorY_SyShogun_Update` (`ovl_i6/fox_sy.c:1698-1724`), `Titania_TiGoras_Update` (`ovl_i5/fox_ti.c:4994-5138`) | `gBossFrameCount`, boss `state` and `swork`, hits on a boss part | 86 |
| Level script, no teammate on screen | 10 levels, for example Corneria 33, Zoness 0, Venom 1 39 | spawn position, then script order and `EVC_*` | 143 |

- Script actors spawn when `gPathProgress` reaches their entry in the level's object list
  (`src/engine/fox_enmy.c:630-638`, `:412`); Venom 1 also compares `gPlayer[0].xPath` (`:631-633`). Fortuna, Bolse,
  Katina, Sector Z and Venom 2 place no event actors, so their lines are all in code.
- This group holds the hints too, mostly Peppy's: controls in scripts (Corneria 27, Meteo 133), boss weak points in
  code (`src/overlays/ovl_i1/fox_co.c:700`). All are addressed to Fox.
- `func_demo_80048AC0` is a story slot whose wording reads shields: Slippy and Peppy by their own (above 160, above
  64, else), Falco by his own under 64, then by Fox's (`src/engine/fox_demo.c:25-75`). 19 calls from six levels.
- Slippy's own story in Sector X and Titania runs on boss state in this group (`src/overlays/ovl_i2/fox_sx.c:35`,
  `:109`, `src/overlays/ovl_i5/fox_ti.c:5005-5134`). The route is #59.

**Observed: scripted teammate scenes on rails.** 102 scripted lines are in scripts that bring a wingman's Arwing on
screen as an event actor (`EVID_TEAMMATE`, `EVOP_SET_TEAM_ID`, `src/engine/fox_enmy2.c:1262-1271`).

- 79 are fixed steps of the scene. **The on-rails calls for help are among them**: they play at their step, whatever
  has happened, for example Corneria 17.8, Macbeth 26.8 and 26.25, Area 6 12.8, Zoness 46.7. Beyond the check that the
  wingman is up, nothing reads its state to choose them.
- 23 sit behind a condition that reads how the scene went: `EVC_NO_TARGET` 11 (`src/engine/fox_enmy2.c:2377-2382`),
  `EVC_NOT_CHASED` 9 (`:2788-2792`, fed by the chaser at `:3235-3239`), `EVC_NO_FOLLOWER` 2 (`:2637-2645`),
  `EVC_ATTACK_GROUP_CLEARED` 1 (`:2685-2692`). These are the on-rails thanks and "that was my target" lines, for example
  Corneria 17.33, Macbeth 28.35, Corneria 9.51.
- Levels: Corneria 15, Macbeth 16, Zoness 14, Area 6 12, Titania 11, Solar 10, Sector X 8, Sector Y 6, Meteo 5, Venom 1
  5. The damage steps between the lines (`EVOP_DAMAGE_TEAM`, `src/engine/fox_enmy2.c:1384-1394`) are #59 and #61.

**Observed: lines that react to the wingman's own state.** 63 sites, in the code of the actor that carries the wingman:
`ActorAllRange` in all-range (`src/engine/fox_360.c:375`), `ActorTeamBoss` at bosses (`src/engine/fox_demo.c:861-873`,
`src/engine/fox_enmy.c:465`), `ActorEvent` with a team id on rails.

| Reaction | All-range `engine/fox_360.c` | Boss `engine/fox_hud.c` | On rails `engine/fox_enmy2.c` | Count |
|---|---|---|---|---|
| Shields ran out, leaves the fight | `:1444-1469` | `ActorTeamBoss_Retreat` `:4243-4262` | `ActorEvent_800700A4` `:2071-2094` | 9 |
| Hit by an enemy | `:1185-1224`, only after a chase; wording by shields, 90 and up or below | `:4569-4580`, `dmgSource` 2 or 100 only | scripted | 9 |
| Hit by Fox's shot, or ran into something | `:1129-1140`, `:1172-1184` | `:4541-4567` | `:2257-2285` | 18 |
| Enemy on its tail, the call for help | `:1705-1752`, also sets the help marker `gTeamHelpActor` (`engine/fox_display.c:47`) | none | scripted | 9 |
| Rescued by Fox, thanks | `:530-552`, `:625-643` | none | scripted | 6 |
| Fox shot the enemy it called | `:1042-1053` | none | scripted | 3 |
| Near Fox for 200 frames, small talk; wording reads Fox's shields | `:1395-1433` | none | none | 9 |

**Observed: lines about the wingman's own action.** 19 sites, all-range only: attack call
(`src/engine/fox_360.c:1781-1806`), enemy shot down (`:2204-2219`, armed at `:1074`, `:1077`), last Star Wolf
(`:1060-1072`), missile down (`:928-937`), and leaving for a missile in Sector Z
(`src/overlays/ovl_i4/fox_sz.c:309-352`).

**Observed: lines that react to Fox, spoken by a wingman.** 21 sites, plus script conditions.

| Trigger | Where | Count |
|---|---|---|
| Player shields under 64, at most every 960 frames | `Player_LowHealthMsg`, `engine/fox_play.c:5834-5875` | 6 |
| Player died | `Cutscene_KillPlayer`, `engine/fox_demo.c:1609-1630` | 3 |
| Lock-on shot worth 7 hits or more, on rails, no boss | `PlayerShot_UpdateShot`, `engine/fox_beam.c:2284-2301` | 3 |
| Enemy dropping `DROP_TEAM_MESG` killed | `Actor_Despawn`, `engine/fox_enmy.c:1802-1809` | 3 |
| Granga one-up check passed | `Corneria_CoGranga_1UpCheck`, `ovl_i1/fox_co.c:561-578` | 3 |
| Enemy on Fox's tail | `engine/fox_360.c:1696`, `:1819` | 2 |
| Third warp ring | `Player_CollisionCheck`, `engine/fox_play.c:2175` | 1 |

Scripted lines reach the same kind of trigger through `EVC_CLOSE_*`, `EVC_HEALTH_0p`, `EVC_30_HITS` and `EVC_80_HITS`
(`src/engine/fox_enmy2.c:2546-2550`, `:2806-2816`), all on `gPlayer[0]` or `gHitCount`.

Counts: 268 code sites name a wingman (165 story, 63 own state, 19 own action, 21 about Fox); 261 script commands do.

**Derived (read, not run)**

- *The owner's split does not follow the code on rails.* In all-range a call for help is an AI reaction. On rails it is
  a script step in the same scene as the story around it, and the scene needs a teammate actor. So what a later record
  says about each scene (#59) decides these lines too.
- *Reaction and action lines depend on the wingman's slot in `gActors`.* They run in its actor's code, or read that slot
  (chase, rescue, Sector Z). With no actor in the slot they should not fire. Story lines and lines about Fox need no
  actor, only `gTeamShields`.
- *Every down check reads `gTeamShields`, not a `Player`.* Whatever co-op keeps there decides whether a human-flown
  character's story lines play and which ROB stand-in appears. Also, `Radio_Draw`'s shield bar shows that value.
- *Dropping a line changes game state.* `gRadioState == 0` gates AI code, including whether the chase block draws
  random numbers (`src/engine/fox_360.c:1705`) and damage lines (`src/engine/fox_hud.c:4537`). A line dropped on one
  client only would change `gRadioState`, the hash and then the simulation.
- *Friendly-fire lines only know player 0.* They test `dmgSource == AI360_FOX + 1`, and `dmgSource` is `sourceId + 1`
  with `sourceId = player->num` (`src/engine/fox_beam.c:696`, `src/engine/fox_play.c:3091`). A shot from player index 1
  gives 2, which is `DMG_SRC_2`, so `ActorTeamBoss_HandleDamage` takes it as an enemy hit (`src/engine/fox_hud.c:4569`).
  Indexes 2 and 3 give 3 and 4, which none of these lines test.
- *Lines about Fox are not limited to Fox.* `Player_LowHealthMsg` runs for every active player
  (`src/engine/fox_play.c:5957`, loop at `:7039-7045`) and its timer is one global, so two active players count it
  twice per frame. `Cutscene_KillPlayer` has no player 0 check. The random pick does not know who is human: a character
  can comfort itself.
- *Scripted lines follow player 0.* While `gPlayer[0]` is not active they are skipped and lost, and `EVC_CLOSE_*`
  measures to `gPlayer[0]`.
- `Venom2_LevelComplete` makes three equal-priority calls in one frame, so the last living of Falco, Peppy, Slippy
  ends up on the radio.

**Guess**

- The `gTeamShields[TEAM_ID_SLIPPY] = 1` writes in the Titania boss look unneeded, since -2 passes the gate anyway.

**Unknown**

- Which lines a real run reaches: counts cover every script a level places, whatever route or branch that needs. 15
  commands sit in scripts no object list places or links to (Corneria 81, Meteo 10, Sector X 60, Solar 27, Venom 1 42,
  63, 91, 106) and 1 is unreachable (Meteo 140.10). These are left out of the 245.
- Whether US 1.1 scripts differ: only US 1.0 was decoded.
- Whether a line can be hidden without moving `gRadioState`: the state machine waits on the voice
  (`src/engine/fox_radio.c:628`), and nothing here tried it.
- How enemies choose a target when a wingman's slot is empty, which the chase lines depend on. Not read.

### 2026-11-05, #6 (WP-06), on-rails: player position, path progress, camera

Read from the source only; nothing was run. Line numbers are for `origin/coop` at 1ec6c677.

**Observed**

- *Two z values per player.* `Player.pos.z` is the player's z in level space; `trueZpos` is `pos.z + camDist`, the
  value collision and shots use (`include/sf64player.h:190`, `:227`; set in `Player_UpdatePath`,
  `src/engine/fox_play.c:3649`; shot hit test `src/engine/fox_beam.c:857`). `camDist` is not 0 in all normal flight:
  `Player_ArwingBoost` pulls it towards -400 while boosting (`src/engine/fox_play.c:5228`; -130 in a meteo warp,
  `:5172`), `Player_ArwingBrake` towards 180 while braking (`:5326`) and back towards 0 on every call (`:5343`). Both
  run each frame for the Arwing (`:5707-5708`). Cutscenes such as `Cutscene_EnterWarpZone` also write it
  (`src/engine/fox_demo.c:469-490`).
- *Path progress is per player, then copied into one global.* `Player_UpdatePath` (`src/engine/fox_play.c:3627`)
  adds `-vel.z` to `player->zPath`, sets `gPathVelZ` and `gPathProgress` from that player, and adds its `zPathVel` to
  the running total `gPathTexScroll` (`:3632-3637`). So every caller overwrites the first two and adds to the third:
  `Player_UpdateOnRails` for the Arwing (`:5716`) and the
  Blue Marine (`:5746`), the Landmaster (`src/engine/fox_tank.c:1159`), cutscenes (`src/engine/fox_demo.c:1886`,
  `src/overlays/ovl_i5/fox_ti_cs.c:358`) and Macbeth (`src/overlays/ovl_i5/fox_ma.c:5544`, `:7043`, `:7679`,
  `:7709`, `:7751`).
- *Forward speed is the player's own.* `Player_MoveArwingOnRails` (`src/engine/fox_play.c:3981`) builds `vel` from
  `baseSpeed`, `boostSpeed` and the stick, then adds `vel.z` to `pos.z` (`:4173`). Boost and brake therefore change
  how fast that player's `zPath`, and with it `gPathProgress`, grows.
- *The flight box follows the path centre.* `pos.x` is clamped to `xPath ± pathWidth`, `pos.y` to
  `yPath + pathFloor … yPath + pathHeight` (`src/engine/fox_play.c:4148-4170`). The box sizes come from
  `Player_Setup` (`:4566`, starting sizes at `:4621-4659`); after that `Player_Update` eases `pathWidth` towards 1100
  for the Arwing and 700 for other forms (`:6182-6195`; `gFovYMode` is only ever 0). `pathWidth` is a half-width.
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
- *Culling mixes both.* Objects are removed when `gPlayer[0].cam.eye.z + cullDistance < obj.pos.z + gPathProgress`
  (`src/engine/fox_enmy.c:2694`, `:2798`) or when they are too far from `gPlayer[0]`'s `xPath`/`yPath`
  (`:2695-2696`). On rails eye z stays near 400, so the depth test follows the global progress; x and y follow
  player 0.
- *Drawing.* Objects are drawn at `obj.pos.z + gPathProgress` (`src/engine/fox_edisplay.c:983` and many more).
  A player is drawn at `trueZpos + zPath` with **its own** `zPath` (`src/engine/fox_display.c:1519` for the Arwing,
  `:1466` for the Landmaster, `:1500` on foot).
- *Ground scroll.* `Background_DrawGround` scrolls the ground texture from `gPathTexScroll`, not `gPathProgress`
  (`src/engine/fox_bg.c:1418`, `:1538`, `:1645`, `:1738`, `:1813`). Titania draws its ground from
  `gPathGroundScroll` instead (`src/engine/fox_display.c:1951-1957`), which `Player_UpdatePath` sets from the last
  caller when `gGroundType == 4` (`src/engine/fox_play.c:3639-3641`).
- *Floor collision.* `Play_CheckDynaFloorCollision` (`src/engine/fox_play.c:1149`) adds the global `gPathProgress` to
  the z it is given (`:1194`, `:1201`, `:1237`). The player's hit points go through it while `gUseDynaFloor` is set
  (`:2309-2341`; set at `:2713`), and so do shots (`src/engine/fox_beam.c:907`). Ground checks for
  `gGroundType == 4` use the player's own `zPath` instead (`:1800-1801`, `src/engine/fox_tank.c:314`).
- *Camera.* `Camera_Update` (`src/engine/fox_play.c:6664`) picks `Camera_UpdateArwingOnRails` (`:6211`) for the
  Arwing. Eye and look-at x/y follow the player's offset from `xPath`/`yPath`, scaled by `unk_148`/`unk_14C`
  (0.777, set in `Player_Setup`); eye z is 400 plus `sOverheadCamDist`, and 200 more in a somersault (`:6258-6264`),
  look-at z is `trueZpos + gPathProgress - 1`
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
  rises by the same amount, so `pos.z + zPath` stays the same. The Arwing is drawn at that depth plus `camDist` in
  front of a camera whose eye sits at z = 400, so boost moves it forward and brake moves it back. Wherever else
  `pos.z` is written on rails (knockback, cutscenes) this can drift.
- With two players the global progress is whatever the last player in the loop wrote, so player 1 would drive object
  loading, dynamic-floor collision, the depth part of culling and every object's draw position, while path turns and
  the x/y part of culling still follow player 0.
- Ground scroll mostly does not follow the last writer: `gPathTexScroll` takes every player's `zPathVel`, so two
  players would scroll the ground at the sum of their speeds, about twice as fast as one. Titania is the exception:
  `gPathGroundScroll` follows the last writer.
- If the two players fly at different speeds (one boosts, one brakes), their `zPath` values drift apart. Each Arwing
  is still drawn at its own depth, but objects are drawn with the shared progress, so the other player would look
  closer than it is and its shots would not line up with where it is drawn. Hit tests against shots compare level z
  (`trueZpos`) and should stay correct. Dynamic-floor collision would not: it adds the shared `gPathProgress` to a
  player's z, so where `gUseDynaFloor` is set, the player whose `zPath` differs from the global is tested against
  the wrong stretch of floor.
- Player 2 has its own `xPath`/`yPath`. Since path items only turn `gPlayer[0]`, player 2's flight box and camera
  would stay on the old path centre after a turn.
- `Camera_Update` does not set `gInputPress` again, so each player's on-rails camera sways with whatever controller
  the last `Player_Update` used.
- Simplest model for WP-12: one shared path. Player 0 (the host or a fixed slot) owns `zPath`, `xPath`, `yPath`,
  the turn state and the speed along the path; the other players copy them each frame and only move freely in x/y
  inside the box. Boost and brake would then either be shared, or become a per-player z offset from the shared
  progress (`camDist` already offsets draw position and hit tests that way). That is a design choice for
  WP-07/WP-12, not decided here.

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

### 2026-11-05, #5 (WP-05), single-player assumptions audit

Read at `d653ee21` (branch `coop`). Full list with a verdict per site: `docs/coop/audit/single-player.md`, generated
by `python tools/coop/sp_audit.py` from `docs/coop/audit/sp-verdicts.tsv` (`--check` fails when it is stale or a site
has no verdict). Rerun it after an upstream sync: line numbers move, and new sites show up as missing verdicts
[wrong, see 2026-11-06, #56].

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
  Start [wrong, see 2026-11-06, #56] (`src/overlays/ovl_menu/fox_title.c:171`, `Title_CheckControllers`).
  `Player_Update` then reads `gInputHold`/`gInputPress`/`gControllerRumble` from it for every player
  (`src/engine/fox_play.c:5886-5888`). Pause,
  intro skip and the R-button radio [wrong, see 2026-11-06, #56] (`Play_Main`), bomb detonation and charge-shot
  release (`src/engine/fox_beam.c`)
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

### 2026-11-05, #8 (WP-10), player count apart from camera count

Observed:

- `gCamCount` means two things. It is the number of players and cameras, and in about 25 places it is also the
  versus flag: `gCamCount != 1` or `gCamCount == 4` turns on player-vs-player hits, the versus HUD, no radio, no
  positional sound and arena wrapping (list in `docs/coop/audit/sp-verdicts.tsv` from WP-05, rows with `gCamCount`).
  So co-op cannot just set `gCamCount = 2`.
- Outside versus, every player reads the main controller: `Player_Update` sets `gInputPress` from
  `gControllerPress[gMainController]` unless `gVersusMode` (`src/engine/fox_play.c:5884-5891`). A second player
  copies the first player's input until WP-14 gives it its own input source.
- `Play_Update` leaves `gPlayerNum` at the last player after its update loops (`src/engine/fox_play.c:7036-7053`).
  In versus that is fine; with more players than cameras, code after the loop would act for the last player.
- The per-player arrays have 4 slots (`gPlayerGlareAlphas[4]`, `gPlayerScores[4]`, `gControllerRumbleTimers[4]`,
  `gVsLockOnTimers[4][4]`, `include/sf64context.h:124-293`), so the player count is capped at 4.

What changed (`src/port/coop/game/players.c`, every game-side site marked `// COOP:`):

- `CoopPlayers_Count()` is the player count. With `gCamCount != 1` (versus) it returns `gCamCount`; in the campaign
  it returns the count fixed by `CoopPlayers_LevelInit()` in `Play_Init`. Until the session exists (WP-21, WP-24)
  that count comes from the debug CVar `gCoopDebugPlayerCount` (default 1, clamped to 1..4). `gCamCount` stays 1.
- Switched to the player count, because they run once per player: allocation and init in `Play_Init`
  (`fox_play.c:2950-2953`), `Player_InitializeAll` (`fox_play.c:2610`), the "last player set up" check in
  `Player_Setup` (`fox_play.c:4933`; back on `gCamCount` since WP-11), the update and camera loops in `Play_Update`
  (`fox_play.c:7036, 7048`), the sound cleanup when a level is quit (`fox_play.c:7188`), the five player draw loops in `Display_Update`
  (`fox_display.c:1969-2068`), the engine noise loop (`audio_general.c:2137`) and the object RAM viewer
  (`src/mods/object_ram.c:323`).
- After each loop in `Play_Update`, with co-op on, `gPlayerNum` is set back to `CoopPlayers_LocalIndex()` (0 for
  now; WP-28 makes it per client).
- Left on `gCamCount` on purpose: every versus check, the loops inside `if (gVersusMode)`, viewports
  (`fox_game.c`), lock-on indicators and target positions (`Display_LockOnIndicator`, `fox_edisplay.c:1272`), radar
  marks (`HUD_RadarMarks_Setup`) and player-shot hits on players (`PlayerShot_CollisionCheck`, `fox_beam.c:837`).
  Those are view (WP-28) or game rules (friendly fire, WP-07 and WP-16).

Tested (Windows 11 Home 10.0.26200, MSVC 14.44.35207, Debug, US 1.0 ROM, default graphics backend from a fresh
config; work on top of `coop` 1ec6c677, merged locally with the WP-03 replay code 070256bd for the test only):

- `gCoopDebugPlayerCount` unset (co-op off): `tools/coop/replay.py play` of a recorded Corneria run (13545 frames,
  recorded on the pre-WP-10 build) gave PASS, `first_mismatch=-1`, level complete at frame 11747, same as the
  recording.
- `gCoopDebugPlayerCount = 2`, same replay: the game kept running for all 13545 frames and after, no crash. First
  checkpoint mismatch at frame 4926, where only the RNG seeds differed and player 1's position still matched. The
  level was not completed; the run was stopped after 600 s.

Derived:

- The second player's update draws random numbers, so the RNG drifts as soon as it is in the level. That is
  expected and why the mismatch shows up in the seeds first.

Unknown:

- What player 2 does in Corneria with this change: where it spawns, whether it is drawn where expected, why the
  run with 2 players did not reach level complete. That is WP-11 (all-range) and WP-12 (on-rails).
- Whether `Camera_Update` for player 2 writes globals that the local camera reads (WP-28).
- `gCoopDebugPlayerCount` changes the simulation, so it belongs in the sim CVar list of WP-21 once both are merged.
  (Done in #55.)

Found later (2026-11-06, while testing WP-11 by watching a 2-player Corneria run):

- Over water the graphics broke, and the run later ended in an MSVC debug assertion (`back() called on empty
  vector`, in `<vector>`). Cause: in `Display_Update` the loop that draws reflected player features over water pushes
  the matrix once before the loop but popped once per player inside it (`src/engine/fox_display.c`, the
  `gGroundSurface == SURFACE_WATER` block after `Effect_Draw`). With one player that balances; with two the matrix
  stack underflowed every frame over water. Versus never reaches it (no water). Fix: one pop after the loop.
  Verified: the owner watched a 2-player run over Corneria's water, graphics normal, no assertion; 1-player replay
  still PASS. That the assertion came from this underflow is a guess; it did not come back after the fix.

### 2026-11-05, #9 (WP-11), player 2 in Corneria's all-range section

Observed:

- Corneria's only all-range section is the Granga fight on the normal route. `Corneria_Granga_Init` switches player 0
  alone: `gPlayer[0].state = PLAYERSTATE_START_360` (`src/overlays/ovl_i1/fox_co.c:102-110`). The cutscene
  `Cutscene_AllRangeMode` sets `gChangeTo360` on its frame 37 (`src/engine/fox_demo.c:771-788`), and `Play_Main`
  then sets `gLevelMode = LEVELMODE_ALL_RANGE` (`src/engine/fox_play.c`, `if (gChangeTo360)`).
- In the recorded Corneria run (`local/replays/corneria.sfcr`, normal route), the trace shows: rails until frame
  6454, `START_360` from 6454, mode all-range from 6493, player 0 `ACTIVE` from 6696, `LEVEL_COMPLETE` at 8621
  (trace frames are `gGameFrameCount`, not replay frames).
- `Player_Setup` resets level-wide state besides the player: radio (`gRadioState`, `gRadioMsgRadioId`), the start
  screen timer, the intro flag `D_ctx_8017782C`, the music, `gPlayCamAt`/`gPlayCamEye`
  (`src/engine/fox_play.c`, `Player_Setup`). Running it for a player who joins mid-level would disturb player 0.
- `PLAYERSTATE_STANDBY` is not a quiet state: each frame it sets `gShowHud = false` and `gPauseEnabled = false`
  (`src/engine/fox_play.c`, `Player_Update`, `case PLAYERSTATE_STANDBY`), so it cannot park a second player.
- `Player` holds two pointers, `sfx.srcPos` and `sfx.srcVel` (`include/sf64player.h:159-160`). A copy of player 0
  starts with them pointing into player 0; `Display_SetupPlayerSfxPos` (`src/engine/fox_display.c:1748`) points them
  back at the player's own `sfxSource` and `sfxVel` on every draw (`:1977`). `gPlayer` lives for the whole level, so
  copying one `Player` into another is safe.
- Shots from player 1 can hit player 0. The all-range hit loop walks `gCamCount` players and only skips the shot's
  own source (`src/engine/fox_beam.c:837`, `:855`), so a shot with `sourceId` 1 is tested against player 0, while
  player 0's shots are never tested against player 1. Friendly fire is a game rule (#7); this does not change it.
- `Player_Update` sets `draw = true` after `Player_Update360` (`src/engine/fox_play.c:5981`), so hiding a player from
  inside `Player_Down` lasts only until that line.
- The all-range camera code (`Camera_UpdateArwing360` to `Camera_Update360`) writes no globals. The starfield is set
  from the draw code (`Camera_SetStarfieldPos`, `src/engine/fox_display.c:1940`).

What changed (`src/port/coop/game/players.c`, two `// COOP:` checks in `Play_Update`):

- In co-op, players 1 and up start parked: allocated but not updated, no camera update, not drawn (their `draw` flag
  stays 0 from `Player_InitializeAll`).
- When the level is in all-range mode and player 0 is `ACTIVE`, each parked player joins as a copy of player 0,
  250 units to the side and 80 up per index, with the per-player arrays `Player_Setup` fills (wing health,
  timers, laser, bombs, lives) copied from index 0. It updates from the next frame on.
- `tools/coop/player_trace.py` and `src/port/coop/test/player_trace.c` (`SF64_COOP_TRACE`) log and check each
  player's state and position per frame.
- A joined player goes out (parked and hidden, `draw = 0`) as soon as player 0 is no longer `ACTIVE` or `U_TURN`
  (level complete, down), and `Player_Down` returns early for co-op players 2+ (one `// COOP:` check), so they go
  out instead. Out players stay out until the next `Play_Init`; respawn is a game rule (WP-16). `draw` is cleared
  again on every frame while out, so a player that goes down is drawn for that one frame only.
- The "last player set up" check in `Player_Setup` is back to upstream's `gCamCount` (`src/engine/fox_play.c:4933`).
  Only player 0 runs `Player_Setup` now, so with `CoopPlayers_Count()` the check never fired, `gClearPlayerInfo`
  stayed set, and every level start reset lives, bombs, laser and team shields: game over could not be reached.

Tested (Windows 11 Home 10.0.26200, MSVC 14.44.35207, Debug, US 1.0 ROM, default graphics backend from a fresh
config; this branch on top of WP-10 8b78fbf5, merged locally with the WP-03 replay code 070256bd for the test only):

- Co-op off (count 1): Corneria replay PASS, `first_mismatch=-1`, level complete at replay frame 11747.
- Count 2, same replay, `player_trace.py check`: PASS. Player 1 parked for all 6493 rail frames, active in all-range
  on frame 6696 (the same frame as player 0), 3819 active frames, 41.9 units per frame on average, distance to
  player 0 between 159 and 4347. It turned at the arena edge on its own (`U_TURN`, like player 0).
- In the same run the replay stops matching at replay frame 9887, player 0 goes down at trace frame 11145 and the
  level restarts from the checkpoint; player 1 is parked again after the restart.
- Watched by the owner, 2026-11-06 (no replay, owner playing, `gCoopDebugPlayerCount = 2`), first build: player 2
  appeared at Granga, but graphics broke over water (WP-10 bug, fixed there) and after Granga player 2 kept flying
  during player 0's level-complete cutscene, crashed at trace frame 9941, and `Player_Down` started the fade to a
  level restart (stopped music, "player down" theme) for everyone; the run then hit the debug assertion.
  `Player_Down` (`src/engine/fox_play.c`) stops the BGM and plays `NA_BGM_PLAYER_DOWN` for any player outside versus.
- Watched again with both fixes: graphics over water normal; player 2 looked normal at Granga and flew along; at
  level complete (trace frame 8279) player 2 went out on the same frame and the cutscene ran to the end (player 0
  `NEXT` at 9612, level left normally). No assertion. Owner's report: "Wasser ok, Spieler zwei hat normal ausgesehen
  und ist verschwunden am Schluss."
- Replay with the fixes: 1 player PASS (unchanged); 2 players `player_trace.py check` PASS; when player 0 went down
  (trace frame 11145) player 1 went out on the same frame.

Tested on Linux (GCC, Debug, OpenGL software rendered, no window, US 1.0 ROM, this branch on `coop`). This is not a
recording and not the normal route: a generated replay presses A every 30 frames until Corneria starts (replay frame
3101), then L with `gDebugJumpToAllRange` starts the all-range section.

- Count 2, `player_trace.py run`: PASS. Player 1 parked for all 1779 rail frames, active on frame 1982 (the same frame
  as player 0), 901 flying frames, 41.7 units per frame on average, never still.
- Player 1 forced down through gdb (`shields = 0`): `check` PASS, drawn for at most one frame after going out. With the
  `draw` line in `CoopPlayers_SkipUpdate` removed the check fails: drawn for 621 frames while parked.
- Level complete while player 1 flies (`gDebugLevelComplete`): `check` PASS, out one frame after player 0. With the
  leave check in `CoopPlayers_SkipUpdate` removed the check fails: flying on for 203 frames.
- `gClearPlayerInfo` read through gdb when player 1 joins: 0. With `CoopPlayers_Count()` back in `Player_Setup`: 1.
- Co-op off: a recording made before WP-10 (title and attract only, 4331 frames) PASS, no mismatch.
- `tools/coop/player_trace.py` against bad traces and a fake game (crash, hang, early exit, freeze, wrong player count,
  player flying on, hidden while flying, not parked after a restart): every case gives the expected exit code.

Not covered by these runs: a level restart, enemy contact with player 1, and Windows with these fixes (build only).

Derived:

- Player 1 copies player 0's controller (both read `gMainController`), but from another position, so it flies a
  different path. Its update draws random numbers, so the enemies act differently and the recorded inputs no longer
  fit. Expected until each player has its own input (WP-14) and the run is recorded with two players. That this is
  why player 0 goes down is a guess; shots from player 1 hitting it (above) could also play a part.

Unknown:

- The normal level-complete path is covered by the owner's watched run only; the replay diverges before it with 2
  players, and the Linux run above reaches it through `gDebugLevelComplete`.
- Whether enemies target or hit player 1: enemy and shot loops still use `gCamCount` or `gPlayer[0]`, so probably
  not (guess). Friendly fire and targeting are game rules (WP-07, WP-16).
- Player 1 has no engine noise: `Audio_StartPlayerNoise(gPlayerNum)` is called in `Player_Setup`
  (`src/engine/fox_play.c:4954`), which a joining player skips. Whether it should get one is for WP-28 (sound follows
  the local player).
- Other all-range levels (Fortuna, Katina, Bolse, Sector Z, Venom 2) start in all-range; there player 1 would join
  once player 0 is active after the intro. Not run.

## Determinism

### 2026-11-06, #53, state hash: every co-op player, negative numbers, free slots

Identity: ROM US 1.0, branch `issue/53-hash-players` on top of `a69c811a`, Linux, GCC, Debug, OpenGL on a software
renderer, no window, one game per run, no transport. Input by rule (`tools/coop/hash_check.py`,
`write_generated_replay`): 5800 frames, Corneria from frame 3096, all-range from frame 4880, player 2 flying from
between frames 5000 and 5200. These runs are Corneria only, on one machine.

Observed (code):

- `CoopPlayers_Count()` returns `gCamCount` unless that is 1, and then the count fixed in `Play_Init`, which stays 1
  while `gCoopDebugPlayerCount` is unset (`src/port/coop/game/players.c:26-50`). `CoopPlayers_IsCoop()` is 0 before the
  first level, even with the CVar set.
- `Memory_Allocate` does not clear (`src/sys/sys_memory.c:12-17`). `gScenery360` comes from it, and the game then sets
  only `obj.status` of each slot (`src/engine/fox_play.c`, `if (gChangeTo360)` and `Play_Init`).
- The menus put the star field at the start of the same buffer, 36000 bytes (`src/engine/fox_play.c:642-644`,
  `src/overlays/ovl_menu/fox_title.c:196-198`). Space levels and Training set up a star field before `gPlayer`
  (`src/engine/fox_play.c:2906-2907`), so there `gScenery360` lies behind it.
- The game writes negative numbers to `iwork`: `-1` (`src/engine/fox_360.c:1367`, `src/overlays/ovl_i6/fox_sy.c:3605`,
  `src/overlays/ovl_i5/fox_ma.c:1168`) and unclamped steps (`this->iwork[0] -= 7` while it is not 0,
  `src/engine/fox_demo.c:2897-2899`). No store truncates a pointer to 32 bits: every pointer store into `iwork` uses
  `(uintptr_t)` (grep, `src/overlays/ovl_i5/fox_ti.c` only).
- `sRandSeed1..3` are multiplied modulo the primes 30269, 30307 and 30323 (`src/sys/sys_math.c:29-31`).

Observed (runs):

- **Two Linux runs of the same input matched** on every frame both logs hold, including `rng`: 1 player (5801 frames
  before the fix, 5800 after), 2 players (5803 before, 5800 after), and 2 players with L to level complete (12007,
  after). Other games and builds ran on the machine at the time.
- **Player 2 was not hashed.** `hash_check.py probe --players 2` on the test commit: flipping a bit of player 2 changed
  nothing, parked (10 of 10 probes) or flying (4 of 4), while player 0 showed on 14 of 14.
  `hash_check.py run --players 2 --poke-player 1`: with the bit flipped at frame 5400, the first hash difference was
  at frame 5401, in `effects` and `shots`. So the hash saw only what player 2's difference did to other objects, a
  frame late. After the fix: 14 of 14 probes, and the first difference is at frame 5400, in `players` only.
- **Co-op off is unchanged.** The 1-player log of `a69c811a` and of this branch are equal in frame, game state, level,
  total and all 12 regions on all 5801 frames.
- **Negative numbers collapse:** with `iwork` at -1 and at -2, the hash stayed the same (14 of 14 probes). **No natural
  case was seen:** no actor held an `iwork` value above 32 bits on any frame of the three measured runs (5800 frames
  with 1 player, 5800 and 12000 with 2), neither negative numbers nor foreign pointers.
- **Free slots hold old bytes:** in Corneria's all-range section 67 slots are active and 133 free, and all 133 are not
  zero on every measured frame. Flipping a bit in one changed the hash (5 of 5 probes). One-player and 2-player runs
  first differ at frame 4880, in `scenery360` only, before player 2 has joined: the active slots hash the same in both
  (`3575d50e9bbd95cf`), the free ones do not (`f8a1b945780158fb`, `f21f981611287fec`). `gScenery360` starts 1264 bytes
  behind `gPlayer` with 1 player and 2528 with 2.

What changed (`src/port/coop/sync/state_hash.c`):

- `CoopPlayers_Count()` at the three player sites. `gCamCount` stays in the globals, as a value.
- In co-op only, a negative number stays what it is, and a free `gScenery360` slot counts as empty. With co-op off both
  stay as they were, so single-player logs still line up with older ones (`docs/coop/QA.md`, test plan item 1).

Derived:

- A non-zero seed below its prime modulus never becomes 0, so the re-seed in `Rand_ZeroOne` (`src/sys/sys_math.c:24`)
  fires only if a seed starts at 0 or something else writes one. A replay's seeds are not 0, which fits the equal `rng`
  regions above. This narrows the WP-02 note, it does not close it: the upstream comment says something corrupts the
  seeds on Linux, and the check still reads `sRandSeed2` twice and `sRandSeed3` never.
- Two-run comparisons are not blocked on Linux for this input.
- In Corneria the free slots lie inside the star field block, so with one player count their bytes should be the same
  for every client.

Guess:

- The bytes in Corneria's free slots are the star field. Follows from the code above; the bytes were not compared.
- In space levels the bytes behind the star field survive the menus, so a client that played another all-range level
  before the session would hash differently from one that did not. Not run. It is the case the free slot fix is for.

Unknown:

- Other levels: none was run, so whether the game reaches a negative `iwork` value, and whether free slots ever differ
  between clients with the same player count.
- The frames between two levels with 2 players. In this input the level-complete state lasts past frame 12000, so no
  second level was reached, and frames where `gPlayer` still points at the last level's memory did not run.
- Other platforms and compilers: the Linux and Windows runs used different input.
- State that is still not hashed for any player: the per-player arrays such as `gLeftWingHealth`, `gLaserStrength` and
  `gChargeTimers`, and the co-op slot state in `src/port/coop/game/players.c`.

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

### 2026-11-04, #2 (WP-02), determinism spike on one PC

Identity: ROM US 1.0, branch `issue/2-determinism` on top of `e8c4e6a2` (#40), Windows 11, MSVC 19.44 (VS 2022 Build
Tools), Debug, DirectX 11 unless a run says otherwise. One PC: the comparison across two machines is not done.

Observed (code):

- **Order within one frame:** `push_frame` (`src/port/Game.cpp:23-30`) runs `Graphics_ThreadUpdate`, which does the
  whole game update and then `Audio_Update` (`src/sys/sys_main.c:232-281`). Only after that does it wake the audio
  thread (`StartAudioFrame`) and wait for it (`EndAudioFrame`, `src/port/Engine.cpp:420-435`). Game logic and audio
  never run at the same time.
- Per game frame the audio thread renders `gVIsPerFrame` audio updates (`src/port/Engine.cpp:383`, `:399-402`). Each
  update first advances the sequences by a fixed `ticksPerUpdate` (`src/audio/audio_synthesis.c:665-668`). Only then
  does it render a sample count picked from the output device's fill level: 560 or 528 at 32 kHz
  (`src/port/Engine.cpp:349-350`, `:387-388`). That fill level depends on wall-clock time.
- Game logic reads audio state: the radio waits for the voice sequence (`Audio_GetCurrentVoice`,
  `Audio_GetCurrentVoiceStatus` in `src/engine/fox_radio.c:615-645`, `src/overlays/ovl_menu/fox_map.c:6014`, `:6045`;
  defined in `src/audio/audio_general.c:1890-1925`). `Audio_GetCurrentVoiceStatus` checks whether the voice layer
  still holds a note.
- `gAudioRandom` mixes in `osGetCount()` (`src/audio/audio_thread.c:87`, `:165`). It is only read in `src/audio/`
  (sound pitch, `src/audio/audio_general.c:499`, `:2000-2001`).
- Clock reads that can reach the simulation: `Rand_Init` (`src/sys/sys_math.c:15-18`; the replay restores the seeds)
  and the boot logo timer (`src/engine/fox_game.c:402`, see #3). `osGetTime` in `src/engine/fox_versus.c:1657` is
  versus mode only. No other `osGetTime`, `osGetCount`, `SDL_GetTicks`, `rand()` or `<chrono>` in `src/engine`,
  `src/overlays` or `src/sys` outside `sys_fault.c` (searched with grep).
- The Linux and Switch re-seed in `Rand_ZeroOne` checks `sRandSeed2` twice and `sRandSeed3` never
  (`src/sys/sys_math.c:24`). Seeding from the clock in the middle of a level breaks lockstep on those platforms.
- `__sinf` and `__cosf` (and so `SIN_DEG`/`COS_DEG`) call the C runtime's `sinf` and `cosf`
  (`src/port/Engine.cpp:664-677`). On Windows x64 the MSVC runtime picks an FMA3 code path when the CPU has one;
  `_get_FMA3_enable()` returned 1 on this PC.
- Game code reads gameplay CVars such as `gRapidFire`, `gInvincible`, `gUnbreakableWings`, `gForceExpertMode`,
  `gSzActorFix`, `gMaCameraFix` and `gRestoreBetaBombExplosion`
  (`grep -rn 'CVarGetInteger("g' src/engine src/overlays src/sys`). Clients that differ in any of them simulate
  different games.
- The state hash log was flushed only every 60 frames (`src/port/coop/sync/state_hash.c`, WP-04). When a test tool
  stopped the game right after the replay report, the end of the log was lost. Under load this happened in 2 of 7 runs
  (1237 of 1251 lines). It is fixed now: the log is flushed every frame.

Observed (runs, `python tools/coop/determinism.py <replay>`, build `e8c4e6a2` plus this branch's changes):

- The script plays one replay with the state hash in fresh folders under `local/`, first once alone (`base`), then six
  runs at the same time: `base-2` (defaults again), `opengl`, `window` (1280x960, vsync off, interpolation 144 fps),
  `audio-low` and `audio-high` (fixed sample count per audio update), and `no-fma3`. Every run is compared with `base`
  on every recorded frame.
- The switches took effect: each game log shows `coop: test switch: FMA3 in the C runtime math functions 1 -> 0`,
  `... audio samples per update high 528, low 528` or `... high 560, low 560`. The `opengl` and `window` settings
  were in the run folder's `starship.cfg.json` (backend id 1; 1280x960, `gVsyncEnabled` 0, `gInterpolationFPS` 144).
  The game log does not name the backend.
- `local/replays/smoke-idle.sfcr` (1251 frames: title, intro, attract demo): 6/6 variants equal to base, 99 s.
- `local/replays/corneria.sfcr` (13545 frames: menus, all of Corneria, map): **6/6 variants equal to base on all
  13545 frames**, every replay `PASS`, 1040 s. `base` alone took 457 s. With six runs at once each took 464 to 484 s,
  and `window` took 582 s.

Derived:

- On one PC and one build, the hashed state of a full Corneria run depends only on inputs and seeds. Five things that
  differ between machines did not change it: start time, CPU load, graphics backend, window and frame pacing, and
  the audio device's timing. This includes the radio, whose logic reads audio state (`gRadioState` and
  `gRadioMsgId` are hashed).
- Turning off the runtime's FMA3 path did not change any hashed value either. A CPU without FMA3 therefore probably
  plays the same game as this one, at least for `sinf` and `cosf` in Corneria. No other CPU was involved.
- For decision 0002 this is the result option A needs on one machine. Lockstep also requires: equal gameplay CVars on
  every client, a session start after boot (#3), and no mid-level re-seed on Linux (`src/sys/sys_math.c:24`).

Guess:

- Sample count does not matter because sequences advance per audio update, not per sample
  (`src/audio/audio_synthesis.c:665-668`). Whether a voice layer still holds a note would then also follow from the
  sequence ticks. Not checked in the audio code beyond that loop.

Unknown:

- A second PC with the same build (another CPU model, another driver), the actual "two machines" half of #2.
- Other levels: only Corneria (on-rails) was played. All-range levels, the Landmaster and Blue Marine, bosses and
  Andross (`gScenery360` allocated mid-level, see #4) are not covered.
- Release builds, other compilers (GCC, Clang) and cross-platform play (Windows against Linux).
- Whether the MSVC runtime's FMA3 path gives other results than the non-FMA3 path at all, or the two agree bit for
  bit by design. Either way the run shows no difference for this replay.

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

### 2026-11-06, #55, what the session tests can and cannot see

Read at `a69c811a` (origin/coop).

**Observed**

- Nothing could fail the hold-back checks. `Check_Refused` and `check_refused` assert that no game message crossed a
  refused handshake, but both ends only sent one after JOINED, and `CoopSession_Send` refuses before that. Both ends
  now push game messages straight into the transport (`--eager` for real processes), and a clean run must have
  dropped some.
- `CoopIdentity_Read` returns 0 on failure, and the callers compared that with the size, so an empty identity passed
  (`session.c:193, 309, 325` at `a69c811a`). The result: an empty hello got an ordinary refusal for every field instead
  of the malformed one, a one-byte welcome let the client in with an empty host identity, and a four-byte refusal
  read as a valid refusal. `Identity_ReadAll` now checks for 0 first.
- Two CVars that change game flow were not in the table. With the level selector, `gSkipBriefing` starts the level
  straight from the map instead of through the briefing (`src/mods/levelselect.c:115`; the menu entry is
  `src/port/ui/ImguiUI.cpp:816`). `gCoopDebugPlayerCount` sets how many players the level has
  (`src/port/coop/game/players.c:30`, default 1).
- On a bad link a refused client can miss the refusal. In the virtual network (35 percent loss, seed 25 of
  `Sim_Lossy`) all five copies of `SESSION_REFUSE` within the 3000 ms linger time were lost, at 180, 380, 780, 1580 and
  2580 ms into the run. The host then dropped the client, which read "The other player closed the connection."
  Nothing got through, so the rule holds, but the player gets a worse text.

**Derived**

- A check that no input can make fail looks like coverage. The mutation table (`tools/coop/session_mutants.py`) is
  there to find those, and it found a gap in `check_joined` too: the Python check did not compare the two texts of a
  warning, only the C one did.
- Eight changes to `session.c` cannot be seen from outside, in the plain or the sanitizer build; they are listed at
  the top of `session_mutants.py` with the reason. Three more only show in the sanitizer build (`--asan`): a leak, a
  read past the end of a short refusal, and an out-of-range peer index that stays inside the session struct.

**Unknown**

- Whether a player on such a link is better served by a longer linger time. It is a trade against a slot held by a
  client that already gave up.

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
