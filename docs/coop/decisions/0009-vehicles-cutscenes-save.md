# 0009: Vehicles, cutscene timing and save

- Status: Proposed; item 6 Accepted (V1 with V2 until each level is proven); item 8 Accepted (S1, changed: no machine
  writes a real save in a session, the host's copy is sent once at the start)
- Date: 2026-11-06 (proposed), 2026-11-08 (item 6 accepted by the owner, as proposed), 2026-11-10 (item 8 accepted by
  the owner as S1, changed)
- Issue: #7

This record covers `DESIGN.md` section 4, items 6, 7 and 8. Each item has its own options and one recommendation, so the
owner can accept them one by one (V1, T1, S1 below). Read at `797c678b`; nothing was run.

## Context

**This record assumes lockstep.** Decision 0002 is still Proposed, and its two-machine half (#2) is open. Everything
below assumes its option A: every client runs the whole simulation and only inputs travel. "If 0002 goes another way"
under Consequences says what changes. Update 2026-11-13: 0002 is Accepted as option A, deterministic lockstep
(2026-11-07), so that section does not apply.

Titania, Macbeth and Aquas have not been run in this project, with one player or with two. WP-02 covered Corneria only
(NOTES.md, 2026-11-04, #2).

### Item 6: Landmaster and Blue Marine

Observed:

- The level picks the vehicle, and the choice is per `Player`: Macbeth and Titania set `FORM_LANDMASTER`, Aquas sets
  `FORM_BLUE_MARINE` (`Player_Setup`, `src/engine/fox_play.c:4645-4647`, `:4663-4664`; field at
  `include/sf64player.h:263`). All three are on rails (`src/engine/fox_play.c:2862-2867`, `include/sf64level.h:97-100`).
- Co-op players stay parked in these levels today: `CoopPlayers_SectionSupported` only accepts all-range, or rails in an
  Arwing (`src/port/coop/game/players.c:137-141`). A player that does join is a copy of player 0
  (`src/port/coop/game/players.c:71-74`), so it would get the same form.
- Versus runs several Landmasters, but through `Player_Update360` (`src/engine/fox_play.c:5804-5818`). The on-rails
  code, `Tank_UpdateOnRails` (`src/engine/fox_tank.c:1120`), has never had a second tank. Versus does not offer the Blue
  Marine (`src/engine/fox_versus.c:431-442`), and `Aquas_Update360` is empty (`src/overlays/ovl_i3/fox_aq.c:6806-6808`).
- The pointer on #16 says Landmaster state is one value for all players (`src/engine/fox_tank.c:14-23`). True, but the
  values differ in what sharing them costs:

| Value (`src/engine/fox_tank.c`) | What it does | With two tanks |
|---|---|---|
| `D_800C9F00` (`:449-450`, `:896-897`, `:956-958`, `:1276`) | Knockback timer after a wall hit, Macbeth | Pushes the other tank too |
| `D_800C9F08` (`:1312-1339`) | Rail wobble in Macbeth: flag 8 flips on every call | On the rails together, one tank only goes up, the other only down |
| `D_800C9F24` (`:558-559`, `:569`, `:577`) | First frame of boost: engine glow and sound | Reset by the other tank, so it repeats every frame |
| `D_800C9F04` (`:481`, `:862`, `:981`, `:1158`, `:1272`) | Set by a collision, read next frame: gates ground effects | Each tank reads the other's flag |
| `D_800C9F20` (`:610`, `:619`, `:632`), static `D_800C9F3C` (`:599`, `:666-671`, `:690`) | Gate a sound | Sound repeats or stays away |
| `D_800C9F14` (`:1125-1130`), `D_800C9F10` (`:319`, `:878-881`) | Scratch: cleared and read inside one tank's update | Nothing |
| `D_800C9F28` (`:585`, `:589`) | Written, never read | Nothing |

- Macbeth's `D_MA_801BE250[40]` (`src/overlays/ovl_i5/fox_ma.c:46`) is also shared and read by the tank
  (`src/engine/fox_tank.c:1187-1252`). Slots 27 to 29 are scratch inside `func_tank_800444BC` (`:317-323`). The rest was
  not traced.
- Blue Marine state in `src/overlays/ovl_i3/fox_aq.c:201-202` is four things in two arrays:

| What | Where | Kind |
|---|---|---|
| Steering and camera offset | `D_i3_801C41B8[3]`, `[4]`, `[9]`, `[10]`, read at `:826-840`, `:896-965` | Per marine |
| Lock-on and torpedo | `D_i3_801C4190[0]`, `[1]`, `[3]`, `[5]`, `D_i3_801C41B8[0-2]`, `[5]`, `[11]`, `[21-23]`, in `Aquas_BlueMarineShoot` (`:1250-1356`) | Per marine, but one for the level |
| Level light and fog | `D_i3_801C41B8[12-17]`, `[25]`, `[26]`, `D_i3_801C4190[8]`, in `Aquas_801A99D4` (`:482-537`) | Level, stepped once per marine (`:879`) from that marine's `trueZpos` |
| Marine's lamp | `gLight3x` and its `y`, `z` (`:1357`) | One light, last writer wins |

- One torpedo slot, `gPlayerShots[15]` (`:1214`), and three laser slots (`:1234`) serve every marine. The aiming marker
  is drawn for player 0 (`:655`, `:669-670`). `Aquas_801A8E30` draws random numbers on every marine update (`:880`).
- Level code names `gPlayer[0]` 152 times in `fox_ma.c`, 117 in `fox_aq.c` and 82 in `fox_ti.c` and `fox_ti_cs.c`.
- Story values are forced at level start: Slippy is -2 in Titania (`src/engine/fox_play.c:2691`), and all three wingmen
  are 255 in Aquas (`src/overlays/ovl_i3/fox_aq.c:437`).

Derived:

- **Sharing these values does not cause a desync.** Every client updates the same players in the same order
  (`src/engine/fox_play.c:7044-7050`), so the values stay equal. They cause wrong play, the same on every client. That
  is a behaviour fix, not a sync fix.
- Of the nine Landmaster values, two change play, four only effects or sound, three need nothing. NOTES.md (#67) asks
  for a slot each; only the first six need one.
- With player 0 owning the path (NOTES.md, WP-12), a follower's wall hit in Macbeth would change the speed of the whole
  level: it sets `D_800C9F00`, and player 0's next update writes its own `vel.z` from it
  (`src/engine/fox_tank.c:449-450`).
- Tank and marine both call `Player_UpdatePath` (`src/engine/fox_tank.c:1159`, `src/engine/fox_play.c:5756`), so
  `CoopPlayers_FollowPath` applies to them as written. Not run.

Unknown:

- Whether two tanks fit Macbeth's rails and tunnels, or two marines Aquas's passages. The box is 500 and 700 wide
  (`src/engine/fox_play.c:4653`, `:4666`); the level layouts were not read.
- How much of the 351 `gPlayer[0]` sites changes play with a second vehicle. Most audit verdicts for these files are by
  name, not by site (266 of 361 sites).
- Whether these levels are deterministic at all (WP-02 did not cover them).

### Item 7: cutscenes, radio and level intros

Observed:

- A cutscene is simulation. `Player_Update` runs it as a player state (`src/engine/fox_play.c:5953-5958`, `:6052-6074`),
  counted in game frames (`gCsFrameCount`, `src/engine/fox_demo.c:599`). In co-op only player 0 runs it: the others go
  `AWAY` when it stops flying (`src/port/coop/game/players.c:149-156`) and get no camera update
  (`src/engine/fox_play.c:7061-7063`).
- **Parts of it run in the draw step.** `Radio_Draw` advances `gRadioState` (`src/engine/fox_game.c:569`). The status
  screen timers count down there (`src/engine/fox_hud.c:767-768`, `:800-801`, via `src/engine/fox_game.c:597`). The
  level clear screen writes `gTotalHits` and `gLifeCount[gPlayerNum]` (`src/engine/fox_hud.c:1085-1089`). The pause menu
  runs inside `HUD_Draw` (`src/engine/fox_hud.c:3809`). `gRadioState` and `gRadioMsgId` are hashed
  (`src/port/coop/sync/state_hash.c:335-336`).
- The radio waits on the voice sequence (`src/engine/fox_radio.c:615`, `:628`, `:645`). Those reads look at the sequence
  player and its notes, not at volume (`src/audio/audio_general.c:1891-1925`).
- Frame length is not fixed. `gVIsPerFrame` is 2 by default (`src/engine/fox_play.c:2839`), 3 in Aquas (`:2683`), and
  cutscene records change it (`src/engine/cutscene_record.c:13`). A game frame lasts `gVIsPerFrame` sixtieths of a
  second (`src/port/Engine.cpp:503`), and the value also sets radio timers (`src/engine/fox_radio.c:593`). It is not
  hashed.
- Input can change flow in few places. Start skips a level intro (`src/engine/fox_play.c:7199-7201`) and pauses
  (`:7222`, `:7252`). The pause menu can retry, which costs a life (NOTES.md, #61). The map skips the briefing and picks
  a route (`src/overlays/ovl_menu/fox_map.c:3957-3970`, `:3593-3597`). Any button counts down
  `PLAYERSTATE_ANDROSS_MOUTH` (`src/overlays/ovl_i6/fox_andross.c:1438`). Start skips a seen ending
  (`src/overlays/ovl_ending/fox_end1.c:1053`). The level clear screens read no input.
- Flow reads use `gControllerPress[gMainController]`: 80 sites of that form in `src/engine` and `src/overlays`, 63 of
  them in menus (a few of the rest are play, such as bomb detonation, not flow). `gMainController` is 0 on every client,
  and port 0 is the local controller on every client (NOTES.md, #56 and #68).
- Three reads differ: the pause menu uses `gInputPress` (`src/engine/fox_hud.c:1345`, `:1471`, `:1475`), and co-op
  leaves that pointer on the local player's slot (`src/engine/fox_play.c:7052-7053`, `:7069-7070`).
- The view reads `gPlayer[gPlayerNum]` (`src/engine/fox_display.c:1834`).

Derived:

- **"Host's timing; clients follow" has nothing to follow in lockstep.** No client plays a cutscene ahead of another:
  all run it on the same game frames, and nothing needs to be sent. What is left of "host's timing" is two things:
  - *Whose input the flow reads.* That can be the host's, and it costs no message.
  - *Pace.* It cannot be the host's. Each frame waits for every player's input (0002), so wall-clock time follows the
    slowest client. Time is counted in game frames, and each client takes `gVIsPerFrame` from its own simulation.
- The flow reads become the host's only if **slot index means player index on every client**: host's input in slot 0,
  everywhere. Port 0 is the local pad today, so without this a client's Start would pause that client alone, and the
  game would desync on that frame.
- The three `gInputPress` reads desync the pause menu as soon as `CoopPlayers_LocalIndex()` differs between clients:
  host and client would read different slots in one simulated menu. Hidden today, because it is 0 everywhere.
- The draw step is part of the simulation, so WP-28 must run it once per frame on every client, whatever that client
  shows. `gLifeCount[gPlayerNum]` will need a fixed index when `Game_Draw` stops passing 0
  (`src/engine/fox_game.c:519`).
- A client whose player is parked must look through player 0's camera, because its own is not updated. This is view
  state only.

Guess:

- WP-28 gives each client its own sound. `Audio_GetCurrentVoiceStatus` reads whether a voice layer holds a note, and
  notes are shared with sound effects, so a different mix might change `gRadioState` on one client. The hash would show
  it.

Unknown:

- Whether the radio's link to audio holds between two machines. WP-02 tested it on one, in Corneria.
- How cutscenes outside Corneria behave with parked players (#17).

### Item 8: save

Observed:

- A save holds planet flags, sound settings and rankings (`include/sf64save.h:37-53`). It holds no lives, route position
  or shields, so **no run can be resumed from a save**.
- `Save_Write` works in memory, then calls `Save_WriteData`, the only step that reaches the disk
  (`src/engine/fox_save.c:142-149`, `src/sys/sys_joybus.c:103-105`). That writes `default.sav`
  (`libultraship/src/public/libultra/os_eeprom.cpp:38-43`; libultraship was read at its pinned `eaaf9d0f`).
- `Save_Read` runs at boot only (`src/engine/fox_game.c:63`, `:392`). It also resets `sPrevSaveData`
  (`src/sys/sys_save.c:69`).
- Saves happen at level end (`src/overlays/ovl_menu/fox_map.c:1479`, `src/engine/fox_hud.c:1326`), game over
  (`src/overlays/ovl_menu/fox_map.c:1538`), after Venom and in the ranking (`src/overlays/ovl_menu/fox_option.c:448`,
  `:459`, `:4404`), after the ending (`src/overlays/ovl_ending/fox_end2.c:843`) and in the menus
  (`src/overlays/ovl_menu/fox_option.c:1406`, `:1891`).
- **The simulation reads the save:**

| Read | Where | Effect |
|---|---|---|
| `normalClear` of the level | `src/engine/fox_play.c:7199-7201` | Start may skip the intro |
| `played` of the planet | `src/overlays/ovl_menu/fox_map.c:3951-3970`, `:4120` | Start and A may skip the briefing |
| `padEE[0]`, ending seen | `src/overlays/ovl_ending/fox_end1.c:1051-1053`, `fox_end2.c:1107-1111` | Ending can be skipped, `gControllerLock` |
| `normalMedal` of all planets | `src/overlays/ovl_menu/fox_option.c:395-409` | Expert modes offered |

- Neither the state hash nor the handshake sees the save: `src/port/coop/sync/state_hash.c` and `src/port/coop/session/`
  do not name `gSaveFile`.
- Local settings live in the same struct (`include/sf64save.h:40-43`). `GSTATE_INIT` reads them
  (`src/engine/fox_game.c:459`, `:476-478`), and that state runs again after a game ends
  (`src/overlays/ovl_menu/fox_option.c:4510`, `:4570`). The ImGui sliders write them (`src/port/ui/ImguiUI.cpp:245`,
  `:253`, `:261`).
- `gExpertMode` is not in the save (`src/engine/fox_context.c:43`, set at `src/overlays/ovl_menu/fox_option.c:953`).
- A `Save` is 256 bytes (`include/sf64save.h:61`); one message holds 1176 (`docs/coop/PROTOCOL.md:25`).

Derived:

- **A blank throwaway save would desync.** `DESIGN.md` item 8 can be read that way. A host who cleared Corneria presses
  Start in the intro: the host skips, a client with a blank save does not. So the clients need the host's data.
- Every `Save_Write` runs on every client on the same frame, so `gSaveFile` stays equal in memory if it starts equal.
  "Only the host saves" is then one gate at `Save_WriteData`, with no rule in game code.
- A client's own file cannot be damaged by a session, crash included, because nothing writes it. `Save_Read()` at the
  end brings its data back.
- "The host's save file holds the progress" (`DESIGN.md` section 1) can only mean planets, medals and rankings. A run
  ends with the session.

Unknown:

- Whether a session starts in the menus or at the map (WP-24). The save copy must arrive first either way, and
  `gExpertMode` must reach clients too.

## Options

### Item 6: who drives what

- **V1. A vehicle per player, the level's own kind** (`DESIGN.md` proposal). One rule for all three levels; form code
  already works per `Player`, and the join copy gives the form for free. Costs: six Landmaster values and the marine's
  steering, lock-on and lamp per player, as `[4]` arrays or swapped around `Player_Update`; Aquas's light stepped once
  per frame; more torpedo and laser slots; a review of the 351 level sites, which the other levels need too. Risks:
  never run; the marine's lock-on is the largest piece; levels may be too narrow.
- **V2. Player 0 only, the others watch.** Costs nothing: this is today's code. Risk: players 2-4 do nothing for three
  whole levels.
- **V3. Humans fly Arwings over the tank.** Fits the story of 0005: the wingmen fly Arwings there (NOTES.md, #60,
  scripted scenes in Macbeth and Titania). Costs: a form and box other than player 0's at the join, and
  `Player_CollisionCheck` has no Arwing case for Titania's ground (`src/engine/fox_play.c:1795-1806`). Risks: the levels
  were built for the ground, and how it plays is unknown. No answer for Aquas: wingmen are not there, so it still needs
  V1 or V2.
- **V4. One vehicle, player 2 as gunner.** No code to build on: new controls, aiming and view. Most work, least like the
  original, and no use to players 3 and 4.

### Item 7: what "host's timing" means

- **T1. The host drives.** Flow reads stay on slot 0, which is the host on every client. Costs: the slot rule in WP-14
  and WP-22, three pause-menu reads pinned to slot 0, the camera rule in WP-28. No new message, and no other flow read
  changes. Risks: a client cannot pause or skip, and an absent host stops the map and pause.
- **T2. Anyone may pause or skip.** The Start reads take every player's slot (`src/engine/fox_play.c:7199`, `:7222`,
  `:7252`). No message: Start is already in the input stream. Costs: a rule for whose pause menu it is. Risks: a client
  can spend a life on retry or skip for everyone, and two players can fight over pause. T1 does not block adding this.
- **T3. A vote: skip when everyone pressed.** New simulation state in the hash, and new UI. Only intros on cleared
  planets, briefings and seen endings can be skipped, which is too little to pay for it.
- **T4. Each client on its own: play or skip alone, meet at the end.** Not possible in lockstep. The cutscene is
  `gPlayer[0]`, actors and random numbers, so a client past it holds a different world. It needs 0002 option B or C.

### Item 8: what clients save

- **S1. The host's save is the session's save.** At session start the host sends its `Save`. A client keeps its own
  sound bytes, uses the rest in memory, writes nothing, and calls `Save_Read()` at the end. Costs: one reliable message,
  one gate, one restore. Risks: clients earn nothing, and co-op runs land in the host's single-player rankings.
- **S2. Clients run on a blank save** (`DESIGN.md`, read literally). Smallest, and wrong: desync at the first skip the
  host's save allows. Making it safe means changing every read in the table above.
- **S3. S1, plus clients keep planet flags.** At the end a client merges the `planet` bits into its own save. Costs:
  merge code that writes a real save, so a bug there does the one kind of damage S1 cannot. Rankings cannot be merged.
  Can come later without changing S1.
- **S4. A separate co-op save, on the host too.** Keeps co-op out of single-player rankings. Costs: `default.sav` is
  fixed in libultraship, which this repo does not edit, so co-op needs its own file code; the host's unlocks are not
  there, so skips and Expert start locked. Goes against `DESIGN.md` section 1.

## Decision

Items 6 and 8 are accepted. Item 7 is proposed, for the owner to accept.

**Item 6: V1, with V2 until each level is proven.** Accepted by the owner on 2026-11-08. Every player gets the level's vehicle, on player 0's path. WP-18
opens one level at a time, Landmaster first; a level it has not passed keeps today's behaviour. The character a player
flies does not change the vehicle. Whether a human Slippy is "away" in Titania belongs to #85.

**Item 7: T1.** "Host's timing" means: the flow reads the host's input, on the same game frames on every client. Nothing
is sent for cutscenes, radio or intros. Pace follows the slowest client. A client's player waits out cutscenes and sees
them through player 0's camera. A client cannot pause or skip in v1; T2 for pause alone is the first thing to add if
players miss it.

**Item 8: S1, changed.** Accepted by the owner on 2026-11-10, after the first real session parted on the map because
the two PCs held two different saves (NOTES.md, 2026-11-10, the map and the host's save). The host's progress is the
session's save, and every game, the host's too, plays the session on a copy of it in memory. The owner changed S1 in
two points: **no machine writes a real save during a session, the host's included**, and the host's copy is sent
**once, at the session start**, not per level. In the owner's words: sync the host's save to the clients, a dedicated
save per online session, no fiddling with the game code. Chosen over keeping the host's real file as the session's
save (S1 as proposed) and over a fixed all-cleared copy with nothing sent. What that means:

- At the level start that begins the session the host sends its `Save` (`SESSION_SAVE`, PROTOCOL.md), just ahead of the
  level start; every game takes the progress (planets, rankings, the ending-seen byte) before it simulates a frame and
  keeps its own sound settings and language bytes. No re-send per level: every `Save_Write` of a session runs on every
  game on the same frame (Derived, above), and no write runs on one machine only (NOTES.md, 2026-11-10).
- `Save_WriteData` writes nothing while the session runs (one `// COOP:` line in `src/sys/sys_joybus.c`); every end of
  a session, a failure or the host's leaving included, reads each game's own save back from its file. Every
  `default.sav` is byte for byte as before the session. What a session earns is not kept, on any machine; S3 (a merge)
  can still come later.
- The progress is in the state hash (region `globals`), so a copy that drifted is a desync with `gSaveFile` named.
  That is the save hash at the session start, frame 0's hash being the first one compared.
- `CoopLevelStart.cleared` (#191) is gone: the intro skip reads `gSaveFile` again, as upstream does.

## Consequences

Easier: cutscenes, radio and intros need no messages and no per-cutscene code, and a client's save is safe by
construction. Harder: nothing a client draws may skip the draw step, and clients gain no progress.

If 0002 goes another way: item 6 stands as a game rule, and most of the "desync" arguments fall away. T4 becomes
possible and T1 stays valid. S1 stands, though clients would then need the copy for display only.

To do, once accepted:

- **WP-14, WP-22 (#12, #20):** slot index means player index on every client; the client's own pad moves from port 0 to
  its slot, the host's input fills slot 0. Everything in item 7 rests on this. Hash `gVIsPerFrame`.
- **WP-18 (#16):** the per-player state above, one level at a time, and lift `CoopPlayers_SectionSupported` per level.
  Its "Done when" names Titania and Aquas; Macbeth is the third level.
- **WP-19 (#17):** pin the three pause-menu reads to slot 0 in co-op.
- **WP-24 (#22):** send the `Save` and `gExpertMode` before the first simulated frame, with a message id in
  `PROTOCOL.md`. Compare a save hash in the handshake or at session start. Done: `gExpertMode` is a level start value,
  the `Save` is `SESSION_SAVE` (0x0017), and the save is in the state hash from frame 0 on (2026-11-10).
- **WP-28 (#30):** player 0's camera while the local player is parked; keep `Radio_Draw` and the status screens running
  once per frame; fix the index of `gLifeCount[gPlayerNum]` with the record for item 2.
- **Save gate:** `Save_WriteData` in co-op on clients, `Save_Read()` at session end, including when the host leaves.
  Done, on every game, the host's too (item 8 as accepted, 2026-11-10).
- **Not decided here:** lives, respawn, routes, score (`DESIGN.md` items 2 to 5), and what a human-flown wingman is
  (#85).
