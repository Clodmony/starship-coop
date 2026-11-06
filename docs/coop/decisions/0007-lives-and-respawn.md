# 0007: Lives and respawn: a shared pool, players 2-4 return while Fox flies

- Status: Proposed
- Date: 2026-11-06
- Issue: #7 (WP-07), `docs/coop/DESIGN.md` section 4, items 2 and 3; also #88

This record covers lives, going down, game over and retry. Record 0006 (#85, drafted at the same time) says what a
human-flown wingman is in game terms, above all what `gTeamShields[id]` holds for one. "What this record answers" below
gives the split between the two.

In short: one shared pool; players 2-4 come back after 150 frames for one life while Fox flies on; Fox's death restarts
the team as in the original; `EVOP_PLAY_MSG` does not change. This differs from DESIGN's bracket in one place, explained
under Decision.

## Context

Read at `797c678b`. Nothing was run for this record. Every fact below comes from reading, except the ones marked as
runs, which are quoted from `docs/coop/NOTES.md`. How a computer wingman goes down and returns is in the NOTES entry
from #61 and is not repeated here.

### Observed: lives in the game

- **Spending.** Fox's crash ends in `Cutscene_KillPlayer`, which sets `PLAYERSTATE_NEXT` and `gFadeoutType = 7`
  (`src/engine/fox_demo.c:1596`, `:1599`) and takes a life (`:1633`). When the fade is full the game tests
  `gLifeCount[gPlayerNum] < 0`: game over if so, otherwise `PLAY_INIT` (`src/engine/fox_play.c:6164-6168`). The restart
  gives 3 bombs and a single laser (`:6170-6171`), and puts hits and the path back at the save point (`:4692`,
  `:4727-4730`).
- **Retry.** Pause retry takes a life (`src/engine/fox_hud.c:1539`, `:1552`), then sets `NEXT` and `gFadeoutType = 7`
  (`:1607`, `:1611`), so it ends in the same test. Retry on the map takes one at `src/overlays/ovl_menu/fox_map.c:3512`;
  at 0 lives it is game over instead (`:3493`, `:3499`). The campaign has no continue (NOTES, #61).
- **Gaining.** The 1-up item and the sixth gold ring add to the collector's slot (`src/engine/fox_enmy.c:2225`, `:2329`,
  and the port's copy at `src/port/mods/PortEnhancements.c:259`). The clear screen adds its hit bonus to the
  `gPlayerNum` slot (`src/engine/fox_hud.c:1089`).
- **Slot 0 is already the life count in practice.** The start value is written there (`src/engine/fox_play.c:4934`, run
  from `:5948` with `gPlayerNum` 0, since only player 0 runs `Player_Setup`; NOTES, WP-11). The ranking reads it
  (`src/overlays/ovl_menu/fox_option.c:4318`) and the infinite lives cheat writes it
  (`src/port/mods/PortEnhancements.c:126`), a CVar the handshake already compares
  (`src/port/coop/session/sim_cvars.c:21`).

### Observed: co-op today

- A co-op player 2+ who goes down spends nothing. `Player_Down` hands it to `CoopPlayers_OnPlayerDown` and returns
  (`src/engine/fox_play.c:5683`, `src/port/coop/game/players.c:229`). The slot stays `OUT` until the next `Play_Init`,
  where `CoopPlayers_LevelInit` runs (`fox_play.c:2950`).
- Once player 0 is neither `ACTIVE` nor `U_TURN`, every joined player is parked (`players.c:152`). Player 0 updates
  first in the loop (`fox_play.c:7044-7049`), so that is the same frame. Nobody flies on while Fox is down. After a
  restart each player joins again as a copy of player 0, `gLifeCount` included (`players.c:110`).
- Player 0 owns the path, and "player 0 is always there" is one of the reasons given for that (NOTES, WP-12).
- Runs (NOTES, WP-12): with player 2 forced down on rails, the path went on with player 0 alone. With player 0 going
  down, player 1 parked, the level restarted, and player 1 came back with full shields. Two runs of a two-player replay
  that held a player 0 death and a restart hashed equal on all 10500 frames.

### Observed: what depends on player 0 flying

- The path stops. `PLAYERSTATE_NEXT` zeroes the speed (`fox_play.c:6118`) and does not call `Player_UpdatePath`.
- `Player_Down` stops the music and plays the down theme (`:5689-5691`), and the `NEXT` case fades the screen (`:6135`).
- `ActorAllRange_UpdateEvents` kills its actor on `DOWN` or `NEXT` (`src/engine/fox_360.c:714-717`), and the only
  increment of `gAllRangeEventTimer` comes after that test (`:719`).
- A path change item is deleted unused while player 0 is not `ACTIVE` (`src/engine/fox_enmy.c:2518-2519`).
- Event actors skip their attack actions (`src/engine/fox_enmy2.c:1844`).
- 53 lines in 25 files of `src/engine` and `src/overlays` test `gPlayer[0].state` against `ACTIVE`, and `gPlayer[0]` is
  on 1577 lines there (`git grep`). The list includes draw code, so it sizes the work and is not a count of bugs.
- Enemy shots test player 0 only (`src/engine/fox_effect.c:1396`, the other sites in NOTES, WP-12).

### Observed: the engine's own return, in versus

A 150-frame timer (`src/engine/fox_versus.c:594`, `:622-623`), shown to the player in seconds of 30 frames (`:487`),
then `Player_Initialize`, `Player_Setup` and `Player_Update360` (`src/engine/fox_play.c:6096-6098`). Co-op cannot call
`Player_Setup` mid-level, because it also resets level-wide state (NOTES, WP-11). `CoopPlayers_PlaceBesidePlayer0`
(`players.c:71`) is how co-op puts a player back today. The versus spectator view `Camera_FollowPlayer` draws random
numbers (`fox_play.c:6350`). A game frame is 1/30 s (`gVIsPerFrame` 2, `src/sys/sys_main.c:87`), except in Aquas, where
`gVIsPerFrame` is 3 (`fox_play.c:2683`).

### Observed: what lockstep has to watch

- Two life writers run in the draw path, not in the simulation update. `HUD_Draw` (`src/engine/fox_hud.c:3695`, called
  from `src/engine/fox_display.c:2090` and `src/engine/fox_game.c:571`) calls `HUD_PauseScreen_Update`
  (`fox_hud.c:3809`), and the bonus is in `HUD_LevelClearStatusScreen_Draw` (`:1058`). Both write
  `gLifeCount[gPlayerNum]`, and there `gPlayerNum` is the player being drawn (`fox_game.c:289`).
- After the player loop `gPlayerNum` is the local player (`fox_play.c:7052`). That is 0 for everyone until WP-28
  (`players.c:59`).
- Start is read from the local pad (`fox_play.c:7222`, `:7252`) and the pause menu from `gInputPress` (`fox_hud.c:1471`,
  `:1475`).
- The state hash covers every `gLifeCount` slot (`src/port/coop/sync/state_hash.c:331`), the player slots (`:239`),
  `gRadioState` (`:335`) and, since #67, the team stores (`:129-132`).

### Derived

- A 1-up taken by player 2 lands in slot 1 and is overwritten at the next join (`players.c:110`). The team loses it
  today.
- Once WP-28 gives clients different local indexes, the draw-path and map writers would change a different slot on each
  client. That is a hash mismatch under any lives rule, so these lines need fixing whichever option wins.
- Fox cannot stop flying while others fly on unless every site in "what depends on player 0 flying" changes first.

### Unknown

- How any of this plays. Nobody has played co-op with lives, so the pool size and the delay are starting values.
- Whether it holds outside Corneria, the only level run with a second player.
- A return after going down has not been run. `CoopPlayers_PlaceBesidePlayer0` has only run for a join and a rejoin.
- The sync model: decision 0002 is still Proposed. This record assumes lockstep and says below what changes otherwise.
- A client that leaves mid-level. That is not a player going down, and it stays with the session work.

## Options

### Lives

1. **One shared pool (DESIGN's bracket).** One number the team spends and earns, shown the same on every client. Costs:
   the 11 write lines listed under Decision name slot 0, which lockstep needs anyway. Risk: one player can spend the
   lives Fox needs.
2. **Lives per player.** `gLifeCount[4]` exists, and the HUD's `gLifeCount[gPlayerNum]` would show each client its own
   count for free. Costs: the join copy goes, slots have to survive between levels, retry needs an owner, and the
   ranking reads slot 0 only. Risk: spare lives cannot help the team, because Fox at -1 still ends the run. The
   draw-path writers stay per client, which is the desync above.
3. **Fox's lives only, as in the original.** Only Fox's death spends a life, and a human wingman costs nothing.
   Cheapest: the same 11 lines, but no life check on a return. Risk: players 2-4 have no stake beyond the wait, so
   flying into walls is free.

### A player goes down

1. **Any death restarts the team.** No return code at all. Costs: every player's mistake takes everyone back, and all
   lose their hits since the save point. A player 2+ reaching that path tests its own slot, not the pool. The first
   WP-11 build did this by accident: player 2's crash faded the level for everyone and ended in a debug assertion
   (NOTES, WP-11).
2. **Humans sit out like computer wingmen.** Already in the game. Costs: a human watches the rest of the level and all
   of the next, and down in Bolse, Area 6, Venom 1 or Venom 2 means out until the end of the run (NOTES, #61). That is
   too little co-op to be a rule.
3. **DESIGN's bracket in full: anyone returns after a delay while any player is alive, Fox included.** The best outcome
   for the team, since nobody loses progress. Costs: Fox cannot leave `ACTIVE` without stopping the level, so one of two
   builds is needed.
   - *Ghost.* Fox stays `ACTIVE`, hidden and unhittable, flying the path alone until the delay ends. The `gPlayer[0]`
     reads stay valid. But enemy shots test only player 0, so during that time no shot can hit anyone; enemies aim at
     and chase a ship nobody sees; and items and level-end triggers need guards. The level gets easier when Fox is down.
   - *Refill in place.* Fox never goes down, and a life buys full shields on the spot. No gap, but co-op would never see
     a restart, checkpoints would do nothing, and the callers of `Player_Down` write speed and `csTimer` after it
     returns (`fox_play.c:5735-5743`).

   Either one needs a run on every level, and only Corneria has run with a second player.
4. **Split: players 2-4 return after a delay, Fox's death restarts the team.** The return reuses the `OUT` slot and the
   join placement. Fox's side is code that exists and has hash runs. Costs: Fox's death takes progress from players
   still flying, and the host carries more weight than the others. Everything this builds is also needed for option 3.

## Decision

Proposed: lives option 1 with down option 4. The team has one shared pool of lives in `gLifeCount[0]`. A player 2-4 who
goes down leaves the level and comes back beside Fox after 150 game frames for one life from the pool, while Fox flies
on. Fox going down ends the attempt for everyone, as in the original: one life, restart from the last save point, every
player back. Game over comes only when Fox goes down with the pool at 0, or the host retries at 0.

**This differs from DESIGN's bracket in one place.** "While any player is alive" becomes "while Fox is flying". Option 3
is deferred, not refused: a later record can supersede this one and keep everything else here. It should not be built
before the other levels have been run with a second player, and before enemy shots reach players 2-4.

### The rules

1. **Pool.** `gLifeCount[0]`, start 2 plus 1 for each player after the first: 2 for one player as in the original, 3 for
   two, 5 for four. Cap 99 as now. Slots 1-3 decide nothing.
2. **Lives in.** Anything that adds a life adds it to the pool, whoever collected it.
3. **Fox goes down.** Unchanged: one life, then restart at the last save point, or game over if that takes the pool
   below 0 (`fox_play.c:6164`). The other players park on the same frame as today and come back at `Play_Init` at no
   cost. A waiting return is dropped.
4. **Player 2-4 goes down.** The level goes on. The slot is `OUT` and a counter starts at 150 game frames, the delay
   versus uses. The player returns once the counter is 0, the pool holds a life, and player 0 is `ACTIVE` in a section
   co-op players fly in (the gate a rejoin already has, `players.c:166`). One life is spent at that moment, not at the
   crash. The player appears beside Fox as Fox does after a death: full shields, both wings, 3 bombs, single laser, plus
   a short `mercyTimer` (value for WP-16).
5. **Pool at 0.** A player 2-4 going down does not end the run. That player stays out until a life comes in, or until
   the next `Play_Init`, which costs nothing. With several waiting, the lowest player index goes first.
6. **Game over.** Fox down with the pool at 0 (the count goes to -1), or the host's retry at 0. Those two ways only. The
   meaning of 0 stays as in the original: a last try, not an empty tank.
7. **Retry.** Pause retry and retry course stay, and cost one life from the pool. They belong to the host: both spend
   the team's life and restart the team.
8. **Computer wingmen** keep the original rules: no life, sit-out, return at 255. The rule goes with who flies, so a run
   can mix both.
9. **While out,** that client looks through player 0's camera (WP-28). It is a view only, and nothing simulated may
   depend on it. `Camera_FollowPlayer` is not the way, because it draws random numbers.
10. **Landmaster and Blue Marine.** The same rules once WP-18 lets players 2-4 fly there. Until then they are parked and
    cannot go down.

The two starting values, pool size and delay, are constants to tune by play.

### Every client decides alike

- The rule reads simulated state only: `gLifeCount[0]`, the player slots, the return counter, `gPlayer[0].state`, and
  `CoopPlayers_SectionSupported`. It never reads `CoopPlayers_LocalIndex()`, `gPlayerNum` outside the player loop,
  `gMainController`, the clock, or which client is drawing.
- The counter counts in the `OUT` case of `CoopPlayers_SkipUpdate`, inside the player loop. So it runs in game frames,
  in no draw path, and it stops while paused (`Play_Update` does not run in `PLAY_PAUSE`, `fox_play.c:7230`).
- That loop also fixes the order. Player 0 updates first, so a return due on the frame Fox goes down is refused on every
  client, and the lowest index gets a lone life.
- The 11 write lines that can miss slot 0 all name it:
  - collector's slot: `src/engine/fox_enmy.c:2225`, `:2329`, `src/port/mods/PortEnhancements.c:259`
  - draw path: `src/engine/fox_hud.c:1089`, `:1092`, `:1173`, `:1539`, `:1552`
  - map: `src/overlays/ovl_menu/fox_map.c:1448`, `:1529`, `:3512`

  `fox_demo.c:1633` and `fox_play.c:4934` already reach slot 0, because only player 0 runs them. The 22 other lines that
  index `gLifeCount` this way only read it, and they follow.
- Pause, the pause menu and the map menus read player 0's input on every client, not the local pad. WP-14 owns that.
- The return counter joins the slot in the state hash (`state_hash.c:239`).
- No new network message: the rule runs from inputs that already travel, so `docs/coop/PROTOCOL.md` does not change.
- If 0002 ends in host authority, the host runs the same rule, and the pool, slots and counter join the snapshot.

### `EVOP_PLAY_MSG` while player 0 is down (#88)

No change. It keeps testing `gPlayer[0].state == PLAYERSTATE_ACTIVE` and moves on either way
(`src/engine/fox_enmy2.c:1335`). A line that falls in that window is skipped on every client, as in the original.

- The case #88 worries about does not exist under this record: nobody flies on while player 0 is down (`players.c:152`).
  That is already true of the code today.
- The test reads simulated state, so every client makes the same choice and `gRadioState` stays in step. It is hashed
  (`state_hash.c:335`) and gates AI code (`src/engine/fox_360.c:1705`).
- Derived, not run: a line skipped during a crash is not gone for good when its script starts after the save point,
  because the restart loads objects again from `gSavedObjectLoadIndex` (`fox_play.c:4727`).
- One window stays: `U_TURN`, where others do fly on while the test is false. It is all-range only
  (`Player_CheckBounds360`, `fox_play.c:3739`; the brake timer is only set in all-range, `:5310-5311`), and five
  all-range levels place no event actors (NOTES, #60). A single player loses the same line today, so it stays as it is.
- If a later record lets the team fly on with player 0 down, the test becomes "any co-op player is `ACTIVE`", read from
  simulated state in one helper in `src/port/coop/game/`, never from the local index. The same record has to go through
  the other 52 `ACTIVE` lines, because this one is not special.

A down speaker is a separate gate: `Radio_PlayMessage` reads `gTeamShields` (`src/engine/fox_radio.c:91`), so 0006
decides whether a waiting human's lines play.

### What this record answers

The open questions from the NOTES entry on #61:

| # | Question | Answer |
|---|---|---|
| 1 | `gTeamShields` or `Player` shields for a human | 0006 |
| 2 | Does a human miss a level; what do they do meanwhile | No, rules 4, 5 and 9 |
| 3 | Does a human going down cost a life | Yes, at the return, rule 4 |
| 4 | Who triggers a restart; does Fox's death restore humans | Fox's death or the host's retry; yes, rule 3 |
| 5 | Who may choose pause retry and retry course | The host, rule 7 |
| 6 | Full refill on return; clear screen heal for humans | Full, rule 4; the heal is 0006 |
| 7 | Who flies when the wingman is scripted away | 0006 |
| 8 | Do the Venom levels keep "out until the end" for a human | No: a human is never out past the next `Play_Init` |
| 9 | Does one player down cost the medal | 0006, with DESIGN item 5 |
| 10 | One rule, or can a run mix two | Mixed, rule 8 |
| 11 | Which stores join the hash | Done in #67 (`state_hash.c:129-132`); this record adds the return counter |

0006 has to keep one thing true for this record to work: a human's character must not be sent through the sit-out.
`Play_Setup` turns a saved -1 into 0 and 0 into 255 (`fox_play.c:541-553`), and a value at or below 0 stops a wingman
spawning (`fox_360.c:381`). A human is back at the next `Play_Init` whatever that value says, so the value must not
claim otherwise. If 0006 lands with a different answer to any row above, the later of the two records says which one
stands.

## Consequences

Easier:

- Player 0's path, the `ACTIVE` tests and `EVOP_PLAY_MSG` stay as they are, and Fox's death keeps the code that has hash
  runs behind it.
- A return needs no new engine state: one counter beside the `OUT` slot, one placement path, one life check.
- The lives lines get fixed for lockstep once, as part of the rule.

Harder:

- Fox's death costs everyone their progress since the save point, and Fox draws all enemy shots while they test only
  player 0. Players 2-4 go down from collisions alone until that changes. How that balances is `unknown`.
- A stray shot can trigger it. In all-range a shot from player 2-4 can hit player 0, but not the other way round
  (`src/engine/fox_beam.c:837`, `:855`; the loop walks `gCamCount` players). Friendly fire has no record yet, and this
  rule makes that gap costlier.
- A player out at pool 0 can wait a long time. Accepted: the alternative is ending the run for everyone.

Next:

- WP-16 (#14): build the rules, with replay tests for a return after 150 frames with the pool one lower, no return at
  pool 0, a 1-up by player 2 reaching the pool and releasing a waiting player, Fox down with a return pending, game over
  at -1, and two clients hashing equal through all of these. The start value is written at `fox_game.c:423`,
  `fox_play.c:4934` and `fox_option.c:4511`, `:4572`.
- WP-14 (#12): pause, pause menu and map menus from player 0's input.
- WP-28 (#30): the view while out, and lives read from slot 0.
- #88: settled by this record with no code change, once accepted.
- On acceptance: DESIGN.md section 4, items 2 and 3, and the index in `decisions/README.md` point here.
