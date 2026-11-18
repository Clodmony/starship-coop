# 0008: Routes, warps, hit count and medals

- Status: Accepted by the owner, all five questions as proposed (see "Walk-through, 2026-11-08" under Decision)
- Date: 2026-11-06 (proposed), 2026-11-08 (accepted by the owner)
- Issue: #7

In short: **the team earns, the host steers, the team scores.** Any human player can earn a route or a warp. Player 0,
the host, steers at a split and alone decides on the map. `gHitCount` is a team total: a kill by any human scores as
one by player index 0 does, thresholds unchanged. A medal keeps its check, reading what 0006 puts in `gTeamShields[id]`.
The Options below are kept as the proposal wrote them; the Decision is what binds.

## Context

`docs/coop/DESIGN.md` section 4 proposes "whoever triggers it; the whole team follows" for item 4, route choices and
warps, and "team total" for item 5, hit count and medals. This record checks both against the code and answers questions
6, 7, 8 and 11 at the end of the NOTES entry "#59, secret routes and events that depend on a wingman".

Scope:

- What a human-flown wingman is belongs to record 0006 (#85), drafted at the same time. Wherever this record reads
  `gTeamShields[id]`, the value is whatever 0006 says. That includes the two routes that read a wingman directly,
  Corneria and Sector X.
- Lives and respawn are record 0007, the save is record 0009.
- Decision 0002 is still Proposed, so nothing here depends on the sync model. Update 2026-11-13: 0002 is Accepted as
  deterministic lockstep (2026-11-07); nothing here changes with it.

Everything below was read at `797c678b`. **Nothing was run.** Object and script numbers (`SX` object 375, `SY S21`) are
ROM data from the `.o2r` built from US 1.0, read with a throwaway reader as in the NOTES entry, so they cannot be looked
up in the repo.

Re-checked for the walk-through on 2026-11-08, at `5447b479`, by reading only (NOTES entry "decision 0008 walk-through:
what had moved since the record was written"). Of the 97 `file:line` citations below, 37 are where they were, 55 have
moved by 1 to 11 lines in `src/engine` and `src/overlays` (more in the port files: `players.c` by about 58,
`state_hash.c` by 97), and 5 changed text. The line numbers below are those of `797c678b`; the Decision and "To do"
give today's lines for the sites work will touch. Where a fact changed, the line says "Update 2026-11-08":

- **The shooter value no longer collides.** The `dmgSource` writes in `fox_beam.c`, `fox_play.c` and `fox_tank.c` now go
  through `CoopPlayers_DmgSource` (#86): player 0 and computer wingmen keep `sourceId + 1`, players 1 to 3 in a session
  get `COOP_DMG_SRC_PLAYER + index` (11 to 13), a value no game code writes or tests (`src/port/coop/game/players.c:110`,
  `players.h:101-107`). So "Player index 1 therefore gives 2, which is `DMG_SRC_2`" holds only with co-op off. The team
  total itself is **not** built: `src/engine/fox_enmy.c:1766` and `src/engine/fox_360.c:921` still test
  `AI360_FOX + 1`, so a kill by player 1 to 3 still scores nothing on the general path.
- **Everything else the record names is as read:** the Saucerer test (`fox_beam.c:1008`), a split reading `gPlayer[0]`
  (`fox_enmy.c:2560`, `:2580`), the warp counts and the path item's death while player 0 is not active (`fox_enmy.c:2523`).
  `fox_map.c` still has no `COOP:` marker. The HUD now reads the client's own player through `CoopPlayers_HudIndex()`
  (#30), not `gPlayerNum` (`src/engine/fox_hud.c:2907`).
- **Records 0006 and 0007 are Accepted.** Under 0006 a human's `gTeamShields[id]` holds their shields, 1 to 255, and -1
  while they are down, so question 5 reads a human as it reads a computer wingman. Under 0007 retry belongs to the host
  and costs a life from the shared pool (rule 7). 0007 also says the map menus read player 0's input on every client;
  that is meant, not shown: WP-14 (#12) is closed, but `fox_map.c` has no `COOP:` marker and no test drives the map in
  a session, so what a client's map does is `unknown`.

### Observed: earning a route

The level sets `gMissionStatus`. The table of what earns each route is in the NOTES entry. Two conditions depend on who
fired:

- Katina: the Saucerer ignores every shot with `sourceId != TEAM_ID_FOX` (`src/engine/fox_beam.c:1007`), so today only
  player index 0 can destroy it and earn that route (`src/overlays/ovl_i4/fox_ka.c:1410`).
- Sector Y: `gHitCount >= 100` at clear (`src/overlays/ovl_i6/fox_sy.c:2338`), through the hit credit described below.

### Observed: warps

- Meteo has seven `OBJ_ITEM_METEO_WARP` rings. Any player can take one (`src/engine/fox_play.c:1662-1669`), and they
  share one count (`src/engine/fox_enmy.c:2446`). At 7, player 0 enters the warp (`:2447-2451`). A ring that passes
  player 0's camera untaken sets `gRingPassCount` to -1, which closes the warp for everyone (`:2808-2815`).
- Sector X counts its four gates in the per-player collision check, so a pass by any player counts
  (`src/engine/fox_play.c:2169-2173`). A gate that leaves play unpassed sets -1 (`src/engine/fox_enmy.c:2716-2717`), and
  a count of exactly 4 starts the warp (`src/engine/fox_play.c:6903-6909`).
- Corneria's seven arches work the same way: a ring-check item that any player can collect
  (`src/engine/fox_enmy.c:2508-2513`).
- Only player 0 changes state in a warp. The others are parked until it flies again (`CoopPlayers_SkipUpdate`,
  `src/port/coop/game/players.c:143`, `:153`, `:166`).

### Observed: steering on rails

Path items in the level object lists:

| Level | Items | What hangs on them |
|---|---|---|
| Corneria | 1 turn (object 649) | The Carrier route. The item loads only with 7 arches and Falco up (`src/engine/fox_enmy.c:1183-1189`) |
| Sector X | 1 split (object 375, z 117074), 2 turns back (973, 974) | **All four warp gates are on one lane:** objects 693, 766, 839 and 958, x -3820 to -4500 |
| Sector Y | 1 up-down split (243), 2 turns back (409, 410) | Not followed |
| Venom 1 | 8 splits, 9 turns | Not followed |

The warp zones of Meteo and Sector X and Training each hold one more split item, which only ends the section
(`src/engine/fox_enmy.c:2520-2527`).

- Any player can collect a path item, and `item->playerNum` keeps the last one to do so that frame
  (`src/engine/fox_play.c:1667-1668`). The player loop runs upward (`:7044-7049`).
- The item then moves player 0's path (`src/engine/fox_enmy.c:2547-2591`), and the others copy it
  (`CoopPlayers_FollowPath`, `src/port/coop/game/players.c:183`).
- **A split reads player 0's position, not the collector's** (`src/engine/fox_enmy.c:2555`, `:2575`).
- A path item reaches 1200 either way in x and y and 100 in z (`src/engine/fox_edata_info.c:82-83`, order z, y, x:
  `include/sf64object.h:26-30`, test: `src/engine/fox_play.c:1249-1252`). The flight box is at most 1100 either way in x
  (`:6193-6206`), and the items in the table sit on the centre of their lane.
- The edge arrows for a path item are set on `gPlayer[0]` only (`src/engine/fox_enmy.c:2536-2542`), and the HUD reads
  the drawn player (`src/engine/fox_hud.c:2894`).
- A path item dies at once while player 0 is not `PLAYERSTATE_ACTIVE` (`src/engine/fox_enmy.c:2518-2519`).

### Observed: the map

- `gMissionStatus` only sets the first suggestion for the next planet (`src/overlays/ovl_menu/fox_map.c:1487-1499`).
- After anything but `MISSION_COMPLETE` the menu has three entries: go on, change path, retry. After `MISSION_COMPLETE`
  it has two: go on, retry (`fox_map.c:3518-3523`, `:3541-3556`, `:3593-3605`). `Map_ChangePath` swaps between `dest1`
  and the earned route, warps included (`:3698-3735`).
- Every input read in `fox_map.c`, 24 of them, goes through `gMainController`. Only the title screen writes that value
  (`src/overlays/ovl_menu/fox_title.c:171`, `:240`).
- Retry spends a life (`fox_map.c:3512`), takes back the level's hits, medal and team status (`:3665-3670`) and restores
  the team (`:3619-3635`). With no lives left the same entry leads to game over (`:3492-3502`, `:3545-3546`).
- `fox_map.c` has no co-op code yet.

### Observed: hit count

`gHitCount` is one global, and it is in the state hash (`src/port/coop/sync/state_hash.c:328`).

**The shooter test covers only part of it.** 35 writes add to `gHitCount` in `src/engine` and `src/overlays`. Three
depend on the shooter, all with `dmgSource == AI360_FOX + 1`:

- `Actor_Despawn` (`src/engine/fox_enmy.c:1761-1762`), the general path for a kill.
- The Star Wolf time bonus, called only from there (`src/engine/fox_360.c:85`, `src/engine/fox_enmy.c:1786`).
- A Sector Z missile (`src/engine/fox_360.c:920-925`).

The other 32 add without testing who did it, checked in each function and one caller up. Examples: boss kills
(`Boss_AwardBonus`, `src/engine/fox_boss.c:40`), a charge shot's multi-hit bonus (`src/engine/fox_beam.c:2282`), killer
bees (`src/engine/fox_enmy2.c:2133`). So a kill by player index 1 scores nothing on the general path, and scores in full
at those 32 sites.

Who a shooter is:

- `dmgSource` is `sourceId + 1` (`src/engine/fox_beam.c:696`, `:1952-1974`; ramming: `src/engine/fox_play.c:2191`,
  `src/engine/fox_tank.c:1421`). `sourceId` is `player->num` (`src/engine/fox_play.c:3091`), or `NPC_SHOT_ID` plus the
  actor for a computer wingman (`src/engine/fox_enmy2.c:1718`).
- Player index 1 therefore gives 2, which is `DMG_SRC_2` (`include/sf64player.h:107`). Five enemy sites set that value
  and then call `Actor_Despawn` (`src/overlays/ovl_i3/fox_aq.c:1802`, `:2520`, `src/overlays/ovl_i3/fox_zo.c:3522`,
  `:3630`, `:3648`).

What reads `gHitCount`:

| Reader | What it does | Source |
|---|---|---|
| Medal | Threshold per level | `src/engine/fox_play.c:59-65` |
| Sector Y route | 100 at clear, plus its cutscene and radio | `src/overlays/ovl_i6/fox_sy.c:2081`, `:2338`, `:2345`, `:2363`, `:2383`, `:2403` |
| Sector Y scripts | `EVC_30_HITS` and `EVC_80_HITS` (`src/engine/fox_enmy2.c:2806-2815`): `SY S21` and `SY S30`, the only uses in any script table. Each decides if one actor appears with one radio line | ROM data |
| Lives | One life each time the campaign total crosses a multiple of 100 | `src/engine/fox_hud.c:1087-1089` |
| Wingman refill | Surviving wingmen gain up to `gHitCount / 2` shields at clear | `src/engine/fox_hud.c:1097`, `:1131-1144` |
| Katina pacing | The Saucerer comes at 10 hits, or on a timer | `src/overlays/ovl_i4/fox_ka.c:887` |
| Bolse pacing | 5 hits since the last mark open the next step, or a timer | `src/overlays/ovl_i4/fox_bo.c:306`, `:1900` |
| All-range drops | Item drops, on the shooter-tested path only | `src/engine/fox_enmy.c:1765-1782` |
| Ranking | `gTotalHits`, `gMissionHitCount` | `src/engine/fox_hud.c:1087`, `:1282` |

The count is capped at 999 (`src/engine/fox_display.c:1844-1845`).

### Observed: medals

- `Play_CheckMedalStatus` needs Slippy, Peppy and Falco above 0 and `gHitCount >= n`, read when the level clears
  (`src/engine/fox_play.c:59-65`). It does not mean "never went down": a wingman who is up at that moment passes.
- Thresholds as awarded: Fortuna 50 (`src/overlays/ovl_i4/fox_fo.c:1124`), Solar 100
  (`src/overlays/ovl_i3/fox_so.c:3414`), Sector Z 100 (`src/overlays/ovl_i4/fox_sz.c:1431`), Zoness 250
  (`src/overlays/ovl_i3/fox_zo.c:4949`), Area 6 300 (`src/overlays/ovl_i3/fox_a6.c:2908`), Meteo 200
  (`src/overlays/ovl_i2/fox_me.c:2541`), Andross 200 (`src/overlays/ovl_i6/fox_andross.c:4363`), the rest 150, for
  example Corneria (`src/overlays/ovl_i1/fox_co.c:3638`). Venom 2 sets the medal without the check
  (`src/overlays/ovl_i6/fox_ve2.c:123`).
- A medal goes into the save (`src/overlays/ovl_menu/fox_map.c:1473-1476`). All normal medals enable the expert modes
  (`src/overlays/ovl_menu/fox_option.c:394-409`).

### Derived

- "Whoever triggers it" fits earning a route. It does not describe a split. A path item is wider than the flight box, so
  every player in the box is inside its x and y, and **whoever reaches its z first collects it, without choosing to**.
  Players level with each other collect it on one frame, and the highest index wins `item->playerNum`.
- **On Sector X the split decides the warp.** Only the lane at about x -4000 has gates, so whoever picks the side of
  object 375 decides whether the team can reach Sector Z by warp. By `fox_enmy.c:2555` that is player 0, who has to be
  at or below the item's x when it is collected.
- A player who boosts ahead collects a split before player 0 gets there, and player 0's position at that moment picks
  the side. The lead is at most about 14 frames: `camDist` -400 to 180 at 40.8 per frame (NOTES, #10).
- Today a kill by player index 1 to 3 on the general path scores nothing and removes an enemy that player index 0 could
  have scored. So a team can end a level with fewer hits than Fox alone, and medals and Sector Y's route get harder as
  players join.
- Versus already reads `dmgSource` 1 to 4 as the four players (`src/engine/fox_enmy.c:1757`). The same test in the
  campaign would also score the five `DMG_SRC_2` sites, wherever the actor carries a bonus. They do not score today.
- On rails, enemies come from the level's object list, so more players do not bring more targets: they share one supply.
- A medal reads the team at clear. A human who can respawn (record 0007) is up at clear more often than a computer
  wingman, who stays down.

### Unknown

- Anything at run time. That covers how far a team total moves each level, the most hits a level can give, and how much
  of a level's count comes through the shooter-tested path.
- Whether a warp works with a second player (also NOTES, #10).
- How a gate pass is recognised. The branch at `src/engine/fox_play.c:2169` needs a negative result, which only
  `HITBOX_SHADOW` and `HITBOX_WHOOSH` boxes give (`:1314-1318`), and the decoded `aSxWarpGateHitbox` has neither marker.
  Not resolved. A related risk: the warp tests for exactly 4, so if two players could count one gate on one frame, the
  count could step past it. The hitbox swap at `:2170` looks meant to stop that.
- What the lanes of Sector Y and Venom 1 hold.
- US 1.1: only US 1.0 data was read.

## Options

Five questions. Splitting the team over two paths is not among them: there is one world and one path (NOTES, #10).

### 1. Who can earn a route or a warp

- **1a. Any human player** (DESIGN.md's proposal, and what the code already does outside the two conditions above).
  Cost: one test at `src/engine/fox_beam.c:1007`, gated on co-op; Sector Y follows from question 4. Risk: rings, arches
  and gates get easier, because players can cover them between them.
- **1b. Only player 0.** Cost: new tests at `src/engine/fox_play.c:1662` and `:2169`. Risk: players 2-4 become
  passengers for every secret in the game, and their ring passes would have to stop counting.

### 2. Who steers at a split

- **2a. Player 0** (the code today). Cost: none, and it fits "player 0 owns the path" (NOTES, #10). Risk: players 2-4
  cannot pick a lane, so on Sector X they cannot pick the warp lane except by asking the host.
- **2b. The collector** (DESIGN.md's proposal read literally). Cost: `gPlayer[this->playerNum]` at
  `src/engine/fox_enmy.c:2555` and `:2575`, a tie rule in `Player_CheckItemCollect`, because the highest index wins a
  tie today, and arrows for every player. Risk: the collector did not choose. A player boosting ahead on the wrong side
  closes Sector X's warp for everyone.
- **2c. Majority of the players' sides, tie to player 0.** Cost: a new function at both sites, and arrows for every
  player. Risk: nobody can tell what will happen without watching the others. Most code of the three, for a choice made
  on three levels.

### 3. Who decides on the map

- **3a. The host alone.** Matches the code, which reads one controller, and the host's save. Cost: every client must
  take the map's input from player 0 (WP-14). No menu work, no new messages. Risk: players 2-4 only watch, and the host
  alone can spend a life on a retry and undo a level for everyone.
- **3b. Any player's input.** Cost: merge the pads before the map reads them. Risk: two players fighting over one
  cursor, and one stray press on retry costs a life.
- **3c. Vote.** Best for a group. Cost: a cursor per player, new UI, new messages in `docs/coop/PROTOCOL.md`, a timeout
  for an absent player and a tie rule. It can come later on top of 3a, because the host's input stays the final step.
- **3d. Whoever earned the route picks.** Cost and risk: the game has no earner. `gMissionStatus` is one global, and
  Sector Y's total, Sector Z's unhurt Great Fox, Zoness' searchlights and Sector X's starting value have no single
  player behind them. A rule would have to be invented for each level.
- **3e. No second choice: the earned route is forced.** Cost: small. Risk: removes a choice the original gives, and it
  does not settle the question, because the map still has to decide go on or retry.

### 4. What counts as a hit

- **4a. As today.** Cost: none. Risk: added players lower the team's score on the general path, so Sector Y's route and
  all medals get harder with every player.
- **4b. Team total, thresholds unchanged** (DESIGN.md's proposal). A kill by any human player scores as one by player
  index 0 does. Cost: the tests at `src/engine/fox_enmy.c:1761` and `src/engine/fox_360.c:920`, gated on co-op, in a
  form that leaves `DMG_SRC_2` out. Risk: everything in the reader table moves with it. Medals and Sector Y's route come
  sooner, lives come sooner, the refill grows, and Katina's and Bolse's pacing speeds up. By how much is unknown.
- **4c. Team total, thresholds scaled by player count.** Cost: one place for medals (`Play_CheckMedalStatus`), six reads
  for Sector Y, and values that need measuring per level and player count. Risk: with one supply of enemies on rails, a
  doubled threshold can be out of reach (Area 6 300 to 600), and the cap is 999.
- **4d. A count per player.** `gPlayerScores[4]` exists and the campaign does not read it (`include/sf64context.h:293`).
  Cost: rules would need a new reader. Risk: 32 sites have no shooter to credit, and splitting one supply lowers each
  player's number, so medals get harder with every player. As a display only, it can come later on top of 4b.

### 5. What a medal needs

- **5a. Keep the check:** all three wingman slots above 0 at clear, plus the hits. Cost: no code here. It rests on 0006:
  if the value follows the player, a human who is down at clear costs the medal, as a wingman does. If 0006 fixes the
  value above 0, this quietly becomes 5b. Risk: with respawn the wingman half is softer for humans (derived above).
- **5b. Only computer wingmen count.** A slot flown by a human passes. Cost: a gated change in `Play_CheckMedalStatus`.
  Risk: with four players the medal is hits only, and a human going down costs nothing.
- **5c. Hits only in co-op.** Cost: the same change. Risk: drops the original's rule that the whole team has to finish.

## Decision

Accepted by the owner on 2026-11-08, all five questions as proposed: **the team earns, the host steers, the team
scores.** Any human player can earn a
route or a warp (1a), so Katina's Saucerer takes damage from every human player. Direction stays with player 0, the
host: a split takes its side from player 0's position, as the code does today (2a), and on the map only player 0's input
counts for go on, change path and retry (3a). Players 2-4 see the same map. `gHitCount` is a team total: a kill by any
human player scores as one by player index 0 does, thresholds stay, `DMG_SRC_2` stays unscored, and computer wingmen
score no more than they do today (4b). A medal keeps its check, reading whatever 0006 puts in `gTeamShields[id]` (5a).

This changes DESIGN.md item 4: "whoever triggers it" holds for earning, rings, gates and turns, but not for the side of
a split or for the map. Why 2a over DESIGN.md's reading: at a split the collector does not choose, and on Sector X that
accident would decide the warp. 2b stays open as a later change, since it needs no change to who owns the path.

It answers NOTES #59 questions 7, 8 and 11, and question 6 together with 0006.

### Walk-through, 2026-11-08

The owner went through the five questions item by item, each with a concrete case, after the re-check above. Every
proposed option was accepted; nothing was changed.

| # | Question | Case put to the owner | Accepted |
|---|---|---|---|
| 1 | Who can earn a route or a warp | Player 3 flies Meteo's rings, Sector X's gates or Corneria's arches, or shoots Katina's Saucerer | **1a, any human player.** Rings, gates and arches already count from anyone; the Saucerer test at `fox_beam.c:1008` is the one change |
| 2 | Who steers at a path split | Sector X's split, with all four warp gates on one lane; today any player touches it and player 0's position picks the side | **2a, player 0.** No change; 2b stays open as a later change |
| 3 | Who decides on the map | Go on and change path after a level; retry is already the host's (0007 rule 7) | **3a, the host alone.** A vote (3c) can come later on top, since the host's input stays the final step. 3d was dropped in the record: the game has no single earner |
| 4 | What counts as a hit | Player 2 shoots down an enemy; today only Fox's kill scores on the general path | **4b, team total, thresholds unchanged.** By how much medals, Sector Y, lives and the refill move is `unknown` until WP-16 is measured |
| 5 | What a medal needs | 4 players, 150+ hits on Corneria, player 3 is down at the moment of the clear | **5a, keep the check.** A human who is down at the clear costs the medal, as a computer wingman does; respawn (0007) makes it rarer for humans |

What the case in question 5 means after 0006 and 0007: a human's slot is -1 while they are down and they return after
150 frames for a life from the shared pool, so a human costs the medal only if they are down at the instant of the
clear, or stay down because the pool is at 0.

## Consequences

Easier: no new menu, no UI and no new network message for v1. Player 0 already owns the path. The upstream diff is three
tests plus keeping `DMG_SRC_2` out. Every reader of `gHitCount` and `gMissionStatus` stays as it is.

Harder: players 2-4 have no say in direction except by talking to the host. Medals and Sector Y's route will be easier
than in single player, by an unknown amount.

To do, once this is accepted:

- **WP-16 (#14):** the team total at `src/engine/fox_enmy.c:1766` and `src/engine/fox_360.c:921`, and the Saucerer at
  `src/engine/fox_beam.c:1008`, all gated on co-op (lines at `5447b479`). The way to tell player index 1 from
  `DMG_SRC_2` exists since #86: `CoopPlayers_DmgSource` gives players 1 to 3 the values 11 to 13 in a session. What is
  left is a test that takes player 0's value or a human's, and nothing else: not `DMG_SRC_2`, not an NPC's. The same
  test should also cover a Landmaster's or a Blue Marine's kills: by a grep of `dmgSource =` they set it at the same
  sites (`fox_beam.c:697`, `:1957`, `:1964`, `:1979`, and a tank's ram at `fox_tank.c:1422`), but the sites were not read
  to the end and nothing was run. Draft #179 (#16) is what puts those vehicles in a session.
- **WP-14 (#12), map input:** on the map every client takes player 0's input. `gMainController` is per client and comes
  from the title screen, so it must not pick a different pad on a client. Open: `fox_map.c` has no `COOP:` marker yet.
- **WP-22 (#20), WP-24 (#22):** keep the clients together across the map, which has no co-op code today.
- **Record 0007:** a path item dies while player 0 is not active (`src/engine/fox_enmy.c:2523`, was `:2518-2519`). If
  player 0 can be down while others fly on, splits and turns stop working, and Corneria's Carrier route with them. Update
  2026-11-08: under 0007 player 0 is down while others fly on only if the lead role can pass to a survivor (#183), since
  Fox is refilled in place in a proven level, a level not yet proven restarts, and Fox down at pool 0 ends the run. So
  this holds as long as #183 fails to pass the lead; if it passes, splits and turns need the new lead. Retry spends from the
  shared pool (0007 rule 7; the write is `src/overlays/ovl_menu/fox_map.c:3512`), and the life for 100 hits goes to the
  same index in a draw function (`src/engine/fox_hud.c:1090`, was `:1089`).
- **Record 0009:** a medal and the hit totals go into the host's save.
- **After WP-16:** measure hit totals per level with 1, 2 and 4 players. If medals become trivial, scaling them (4c)
  needs a new record.
- **DESIGN.md:** section 4, items 4 and 5 point here when this is accepted.
