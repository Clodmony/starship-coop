# 0006: What a human-flown wingman is: the character's shields follow the player

- Status: Accepted
- Date: 2026-11-06 (proposed), 2026-11-08 (accepted by the owner, with changes to D and E)
- Issue: #85

## Context

Decision 0005 lets players 2 to 4 fly Falco, Slippy or Peppy in place of the computer wingman, and opened three points:
routes and events, radio, damage and return. The research on #59, #60 and #61 ended on the same first question: what
does `gTeamShields[id]` hold for a human flyer? This record answers it, then goes through every open question in those
three `docs/coop/NOTES.md` entries. Decision 0005 stays as it is.

Drafted at the same time, referred to here and not decided for:

| Record | Owns |
|---|---|
| 0007 | Lives, down and respawn (`DESIGN.md` section 4, items 2 and 3), and what `EVOP_PLAY_MSG` does while player 0 is down (#88) |
| 0008 | Route choices and warps, hit count and medals (items 4 and 5) |
| 0009 | Vehicles, cutscene and radio timing, save (items 6, 7 and 8) |

All of this was read at `797c678b`; nothing was run. The hand-offs below were checked against the drafts of those three
records as they stood on 2026-11-06. Script references such as `CO S59:34` are ROM data, not in the repo: they come from
the #59 and #60 notes, decoded from US 1.0 only.

Observed, the value:

- 325 lines in `src/engine` and `src/overlays` name `gTeamShields` (27 files, `fox_context.c` left out): 278 read it and
  47 write it. It is an `s32` array (`include/sf64context.h:23`).
- Nearly all reads ask if the character is up: medals (`src/engine/fox_play.c:59-66`), Corneria's path turn
  (`src/engine/fox_enmy.c:1183-1189`), the script conditions (`src/engine/fox_enmy2.c:2408-2424`), the radio gate
  (`src/engine/fox_radio.c:91`), spawning (`src/engine/fox_360.c:381`, `src/engine/fox_hud.c:4851-4853`), the level
  clear heal (`src/engine/fox_hud.c:1131-1144`).
- A few read the amount: the radio's shield bar (`src/engine/fox_radio.c:755-759`, scaled `arg2 / 255.0f` at
  `src/engine/fox_hud.c:510`), the wording of `func_demo_80048AC0` (`src/engine/fox_demo.c:25-75`) and the help marker
  (`src/engine/fox_360.c:1720`).
- The value has four meanings (table in the #61 note): 1 to 255 flying, 0 sitting this level out, -1 went down in the
  level just played, -2 Slippy away for a story event.

Observed, the player:

- `Player.shields` is a separate value (`include/sf64player.h:301`). Fox starts every level at 255
  (`src/engine/fox_play.c:4582`); the maximum is 383 with three gold rings, from one shared count
  (`src/engine/fox_play.c:73-78`). Wingmen carry their value from level to level.
- Players 1 and up do not run `Player_Setup`: they join as a copy of player 0, shields included
  (`src/port/coop/game/players.c:74`).
- A co-op player going down ends in `CoopPlayers_OnPlayerDown` (`src/engine/fox_play.c:5683`,
  `src/port/coop/game/players.c:229`), which writes no `gTeamShields`.
- Player index is not team id. The roster is by player index (`src/port/coop/session/session.h:41-44`), so index 1 may
  fly Peppy, whose `TEAM_ID_PEPPY` is 3 (`include/sf64player.h:16-24`).
- Most enemy fire does not reach a human yet. All-range tests shots against players in a loop to `gCamCount`
  (`src/engine/fox_beam.c:836-837`), which co-op keeps at 1 (`src/port/coop/game/players.h:5-6`), and rails has no such
  test. Effects are #100. What does reach a human is what `Player_CollisionCheck` walks: scenery, actors, bosses
  (`src/engine/fox_play.c:2138`, #10 note).

Observed, the three kinds of wingman actor:

- **Rails, event actor.** `EVOP_SET_TEAM_ID` stores it (`src/engine/fox_enmy2.c:1262-1267`) and chasers aim at that
  index (`:1451-1456`). Damage comes off the value (`:2121-2122`). Katt and Bill already take none (`:2117-2119`).
- **Boss fights, `ActorTeamBoss`.** Removes itself at 0 or below (`src/engine/fox_hud.c:4851-4853`). Nothing targets it:
  the only reads by id are the radio's hit check (`HUD_RadioDamage_Type`, `src/engine/fox_hud.c:2554`) and a cleanup
  (`src/overlays/ovl_i2/fox_me.c:1563`).
- **All-range, `gActors[1]` to `[3]`.** Enemies are aimed at these slots: Star Wolf (`src/engine/fox_360.c:453`), and
  level code (`src/overlays/ovl_i4/fox_fo.c:46-52`, `src/overlays/ovl_i4/fox_bo.c:164-178`,
  `src/overlays/ovl_i4/fox_sz.c:399-411`, `src/overlays/ovl_i4/fox_ka.c:796`).

Observed, new here. This settles the #60 unknown on empty slots:

- An enemy whose target slot is free leaves the chase (`src/engine/fox_360.c:1874-1880`) and comes back only when the
  slot is active again (`:1947-1951`). Only Star Wolf is sent to Fox instead, and only while the value is at or below 0
  (`:567-575`). One read uses the slot on the value alone: `gTeamShields[...] > 0` leads to
  `ActorAllRange_ChooseNewTarget(&gActors[...])` (`:624-625`). Enemies also leave a target in `STATE360_6`, the retreat
  (`:1874-1876`).
- `timer_0C2 != 0` skips both shots and collisions (`src/engine/fox_beam.c:771`, `src/engine/fox_play.c:2139`), but
  enemies read it too (`src/engine/fox_360.c:1948`). `gNoHitbox` exists (`src/engine/fox_edata_info.c:70`), and
  all-range sets a hitbox per actor (`src/engine/fox_360.c:420`).
- Actor update and draw can be cancelled from a hook: `ObjectUpdateEvent` (`src/engine/fox_enmy.c:2874`),
  `ObjectDrawPreSetupEvent` (`src/engine/fox_edisplay.c:1118`, `:1179`).

Observed, radio and scripts (counts from the #60 note):

- `EVOP_PLAY_MSG` moves on whether or not the line played (`src/engine/fox_enmy2.c:1333-1339`).
- 102 scripted lines sit in scripts that bring a wingman on screen: 79 fixed steps, the calls for help among them, and
  23 behind a condition on how the scene went. 216 of the 245 reachable lines sit behind `EVC_*_ACTIVE`.
- Those conditions also pick content, not only lines: Meteo S6 runs 44 objects when Peppy is down (#59 note).
- Only all-range has real calls for help, in the chasing enemy's code (`src/engine/fox_360.c:1705-1752`).
- `gRadioState == 0` gates AI code (`src/engine/fox_360.c:1705`), so the radio is simulation state.

Observed, story events that take a wingman away:

- Sector X: from 2200 boss frames with Slippy up, the boss goes to state 85 (`src/overlays/ovl_i2/fox_sx.c:971`,
  `:1031`). A stand-in `OBJ_ACTOR_SX_SLIPPY` is hit, the value becomes -2, `swork[6]` is cleared and player 0 goes to
  standby with the camera on Slippy (`:112-114`, `:127-152`).
- Titania: `Play_InitLevel` writes -2 on every load (`src/engine/fox_play.c:2691`). He returns when the third Goras part
  falls (`src/overlays/ovl_i5/fox_ti.c:3813`, `src/engine/fox_hud.c:4835`).
- `EVOP_RESTORE_TEAM` writes 255 (`src/engine/fox_enmy2.c:1360`); only `SX S94` uses it.
- `EVOP_DAMAGE_TEAM` hits the script's own actor or `gTeamDamage` (`src/engine/fox_enmy2.c:1384-1391`). `CO S59:34` is
  255, the only scripted hit that downs a wingman from full; the next largest is 70 (#59 note).

Correction to the #61 note: it says the state hash covers `gTeamShields` only. Since #67 it covers `gTeamDamage`,
`gSavedTeamShields`, `gPrevPlanetTeamShields`, `D_ctx_80177C58` and `gTeamEventActorIndex` too
(`src/port/coop/sync/state_hash.c:129-133`).

Derived:

- Every read means the same thing if the value keeps its meanings, so the cheapest correct answer is one that leaves the
  278 reads alone and deals with the 47 writes.
- A positive value with an empty all-range slot is a state the original does not reach, as far as this reading shows.
  It is the one thing to avoid.
- A human can only go down through `Player.shields` for real once enemy fire reaches them, so this rule needs the
  `src/engine/fox_beam.c:837` loop and #100.

Guess:

- The all-range body in option B3 can be done from those hooks alone, with no upstream edit.
- Players read a stand-in as one more Arwing rather than as a double of the human.

Unknown:

- Anything at run time. No wingman replacement exists yet.
- How long a scene keeps its actor on screen, and how a stand-in looks beside its human.
- Whether an all-range actor with no AI upsets anything that reads its other fields, for example the speed at
  `src/engine/fox_360.c:1641`.
- Whether US 1.1 scripts differ, and which lines a real run reaches: only US 1.0 was decoded, and counts cover every
  placed script.

## Options

Five choices. A is the first question; the others follow from it.

### A. What `gTeamShields[id]` holds

1. **The human's shields.** `Player.shields` is the truth. The co-op layer writes it to the value every frame the human
   flies, and -1 when they go down. All 278 reads keep their meaning. Costs: one writer, and each of the 47 writes has
   to be harmless or stopped for a human id (classed under Consequences). Risk: a write inside a frame and the mirror
   can disagree until the next frame.
2. **A fixed value, 255, while a human holds the slot.** No writer at all. Costs: a human who is down still speaks, the
   bar shows full, the medal and the Corneria gate cannot be lost, and 0007 gets no signal to work with.
3. **Only up or down, 255 or -1.** Same writer as option 1, so it saves nothing, and it loses the bar, the wording in
   `func_demo_80048AC0` and the help marker.
4. **Away, -2, while a human flies.** Uses a state the game has: no actor spawns, scenes are skipped, the radio still
   passes (`src/engine/fox_radio.c:91`), `Play_Setup` leaves it alone (`src/engine/fox_play.c:545-553`), Star Wolf turns
   on Fox. Every path is one the original runs. Costs: every `> 0` read says no, so no medal, no Corneria route, no
   Sector Y S92 and no heal; Meteo's down-wingman waves run; 216 of 245 scripted lines go for that character. Against
   the owner's "story lines kept" in 0005.
5. **A new reader, the value stays for computers.** Replace the reads with `CoopTeam_IsUp(id)`. The cleanest model.
   Costs: 278 lines in 27 upstream files, each marked `// COOP:`, against the fork's rule to keep upstream edits small,
   and all of them in the way of every upstream sync.

### B. What sits in the wingman's actor slot

1. **Nothing, in any mode.** Simple to state. Costs: with A1 this is the state to avoid, so gates at every slot read
   above; enemies aimed at the slot wander, so the human never draws fire and Fox draws all of it; every rail scene has
   to go (C3).
2. **The computer wingman as before, beside the human.** No code. It is option 1 of decision 0005, which the owner did
   not choose: a full AI ship that scores, and shares its shields value with the human.
3. **By kind of actor.** Rails: the scene actor stays as a stand-in. Boss: not spawned, the human takes its place.
   All-range: the slot holds a body, with no AI, no shots, no hitbox and no model, placed on the human every frame, so
   enemies aim at the human with their own code. Costs: three rules instead of one, and a new kind of actor nobody has
   run.

### C. The on-rails scenes and their calls for help

Decision 0005 records a preference: lines that no longer fit, such as calls for help, "may be dropped", story lines
kept. That split does not fit the on-rails code: the call is a fixed script step inside the scene, chasers are other
scripts aimed at the scene's actor, and the outcome lines read how the scene went. Because 0005 says "may", keeping them
does not contradict it.

1. **Keep the scene whole, lines included.** The stand-in flies its script, on screen, and takes no damage. Chasers keep
   a target, all conditions work, nothing in the scripts changes. Costs: a second Arwing of that character while the
   scene lasts, and the human hears their character call for help while it is the stand-in that is chased.
   Hiding the stand-in is the same simulation with a draw change, so that can be flipped after a watched run; chasers
   would then weave around nothing.
2. **Keep the scene, drop the calls for help.** A check in `EVOP_PLAY_MSG` by message id; the script moves on either
   way. Costs: someone has to class 79 fixed steps and 23 outcome lines by id from ROM data, the chasers go unexplained,
   and thanks lines go too or arrive with no call before them. Can be added on top of C1 later without touching anything
   else.
3. **Drop the scene.** Treat the wingman as not active for scene scripts. Costs: `EVC_*_ACTIVE` also picks content, so
   this needs a list per script rather than one gate; chasers are left with a stale index
   (`src/engine/fox_enmy2.c:1453`); the enemies that give those hits are gone; most of that character's lines go with
   it.
4. **Rewrite the scene around the human.** The actor is hidden and pinned to the human, so chasers chase the human, and
   scripted hits become real. The best story. Costs: fighting the script's own flight, an actor lifetime nobody has
   checked, a test per scene across ten levels, and a decision on what 255 means for a human. Needs #100 first.

### D. Story events that take a wingman away

1. **A script never takes a human out of play.** Sector X's event does not fire for a human Slippy (two conditions),
   Titania does not write -2 and does not spawn him at Goras. Every player plays every level. Costs: the radio goes on
   talking about a missing Slippy in Titania, and a slow Spyborg no longer forces Titania, though the map still offers
   it (`src/overlays/ovl_menu/fox_map.c:3698-3735`).
2. **The story holds, the human is parked.** Nothing to change in the events. Costs: that player sits out the end of
   Sector X and, every time, Titania up to the third Goras part, which needs a view for a player with no ship.

### E. Shields between levels

Record 0007 asks that a human's character is not sent through the sit-out: a human is back at the next level load
whatever the value says, so the value must not claim otherwise.

1. **Carry them, as a wingman does.** The human joins with the value the level load left. Costs: a human can start low
   while Fox starts at 255, it needs an exception for values at or below 0 to meet 0007, and with that exception it
   turns upside down: a human who ended the last level down starts at 255, one who stayed up on 30 starts at 30.
2. **Refill, as Fox does.** Every level load sets a human's value to 255, and the human joins at that. One rule, no
   exception, and it meets 0007 by itself. It can sit in `CoopPlayers_LevelInit`, which runs after the copy from saved
   and before the team spawns (`src/engine/fox_play.c:2936`, `:2950`, `:2970`). A join already copies player 0's shields
   today. Costs: the stores and the clear screen heal do nothing lasting for a human, so lost shields cost nothing past
   the level. The lasting cost of going down is 0007's lives.

## Decision

**A1, B3, C1, D1 with the radio about the absent Slippy dropped, and shields that carry over and heal.** Proposed
as A1, B3, C1, D1, E2; the owner accepted A1, B3 and C1 as proposed on 2026-11-08 and changed two:

- **D: D1, and the radio follows.** A human-flown Slippy stays in play, as D1 says, and the lines that speak of Slippy
  as absent or lost are dropped, by message id, the same way on every client. Which lines those are is unknown: nobody
  has listed them yet (Sector X after the event, Titania until the third Goras part). That list is a research task
  before the code.
- **E: neither option, carry and heal.** Every player's shields carry over to the next level and heal a bit there, and
  a player who went down heals a little and comes back. That holds for every player in a co-op session, Fox included;
  single player stays as the original, where Fox refills every level. The heal is a starting value of 64, a quarter of
  the bar, both for the heal at each level load and for a return, to be tuned after playing.

A human-flown wingman is a `Player` whose shields the game reads through `gTeamShields[id]`: the value holds the
human's shields, so every down check, gate, medal, radio speaker and portrait keeps working unchanged and means that
human. Shields carry over from level to level and heal by 64 at each level load, and a player who went down comes back
at 64; a human loses shields only through their own ship. On rails the scripted scenes stay whole with a stand-in that
cannot be hurt; in
boss fights the computer wingman is left out; in all-range a hidden body keeps enemies aimed at the human. No script
takes a human out of play, and the radio does not say otherwise.

The rules:

1. **The value.** While a human's slot is joined, `gTeamShields[id]` is `Player.shields` held to 1..255, written once
   per frame in `src/port/coop/game/`. The floor is 1 because 0 means "sitting out"; the ceiling is 255 because of the
   bar and the heal. On going down the value is -1, written in `CoopPlayers_OnPlayerDown`, as all three retreats do, and
   it stays -1 until the human flies again. A parked player's value is left alone. The id comes from the roster, never
   from the player index. Humans and computer wingmen share these meanings, and a run can mix them (0005).
2. **Level load.** In a co-op session every level load gives each player, Fox included, the shields they ended the last
   level with plus 64, at most 255; a player who went down, or whose value is at or below 0, comes back at 64. The
   human joins at that, and a human id's value is set to it. So no human goes through the sit-out, as 0007 asks.
   `COOP_LEVEL_HEAL`, 64, is one constant, to be tuned after playing. The first level of a run starts every player at
   255, as the original does. If 0007 changes, this rule has to be read again.
3. **Actors.** As B3. Scene actors take no damage, through the Katt and Bill check (`src/engine/fox_enmy2.c:2117-2119`).
   The all-range body uses `gNoHitbox`, not `timer_0C2`, which enemies read. While its human is out it stays in the slot
   in `STATE360_6`, so enemies leave it, and it is ready for 0007's return. If a human goes down during a scene, the
   stand-in retreats by the existing check (`src/engine/fox_enmy2.c:2070-2097`).
4. **Damage.** Nothing but the human's own `Player` takes their shields. `EVOP_DAMAGE_TEAM` does nothing to a human id,
   the 255 at `CO S59:34` included. Slippy and Peppy's double damage (`src/engine/fox_360.c:910-912`) is computer-only.
5. **Radio.**
   - The gate stays: a human who is up speaks, one who is down does not, and that includes a human waiting to return.
     Story lines and ROB's stand-in lines follow.
   - On rails, scenes keep their lines, calls for help included.
   - In all-range, calls for help and thanks for a human-flown speaker are dropped, as 0005 prefers, and the help marker
     goes with them. Own-action lines outside the actor's code go too (`src/overlays/ovl_i4/fox_sz.c:309-352`).
   - Lines in the wingman's own actor code end with its AI. The retreat line is kept, because the team needs to hear it:
     it plays from the down hook, once, with the game's write of 1 around it (`src/engine/fox_enmy2.c:2077`).
   - Lines about Fox stay about Fox: `Player_LowHealthMsg` reads player 0's shields only, and the random speaker may be
     any character that is up. #87 owns its timer.
   - Friendly-fire lines stay for player 0's shots. A shot from another human plays none, and must not take the
     enemy-hit line it gets today (`src/engine/fox_hud.c:4569`). #86 owns telling the shooters apart.
   - Every radio choice is simulation and the same on all clients. No client drops a line for itself. Muting your own
     voice is not decided here: the radio waits on the voice (`src/engine/fox_radio.c:628`), so it could move
     `gRadioState`.
6. **Slippy.** As D1, with the level's vehicle as 0009 gives it. `EVOP_RESTORE_TEAM` and the Goras spawn write nothing
   for a human id: a human comes back by 0007's rule only. Lines that speak of a human-flown Slippy as absent or lost
   are dropped by message id, in the simulation, the same on every client.

Each open question, answered or handed on:

| Source | Question | Answer | Where |
|---|---|---|---|
| #59 1 | What the value holds | The human's shields, 1..255, -1 when down | Rule 1 |
| #59 2 | Scripted actor, chasers, scripted damage | The actor stays as a stand-in, chasers go after it, no scripted damage | Rules 3, 4 |
| #59 3 | Corneria: what replaces "Falco is up" | Nothing: the same read now means the human. A human down when the item loads closes the route, as for a computer Falco. The rescue cannot fail by script, so the route rests on the arches and on that human being up | Rules 1, 4 |
| #59 4 | Sector X at 2200 frames | Not taken out. A slow fight ends as `MISSION_ACCOMPLISHED`, and the map still offers Titania | Rule 6 |
| #59 5 | Titania until the third part | In the level from the start, in the level's vehicle, once WP-18 opens Titania | Rule 6, 0009 |
| #59 6 | Medals, "all three up" | The read is unchanged: a human at 1 or more is up. 0008 keeps the check | Rule 1, 0008 |
| #59 7 | Hit credit, Sector Y's 100 | Left to a later record | 0008, #86 |
| #59 8 | Katina: hurting the Saucerer | Left to a later record | 0008, #86 |
| #59 9 | Lost help at Fortuna and Sector Z | Accepted: the human's shots replace it. No balancing unless a run shows a route out of reach | This record |
| #59 10 | Boss health bar | Follows the value on all clients, as in the original: shown while Slippy is up, gone while down | Rule 1 |
| #59 11 | Who picks on the map | Left to a later record | 0008 |
| #59 12 | `gMissionTeamStatus` and ranking | A human counts as a wingman; the reads are unchanged | Rule 1 |
| #60 | On-rails scenes, calls for help | Kept whole | Rule 5 |
| #60 | All-range calls for help | Dropped | Rule 5 |
| #60 | Lines that react to Fox, a character comforting itself | Fox only, so it cannot happen | Rule 5 |
| #60 | Scripted lines while player 0 is not active (#88) | Left to a later record | 0007 |
| #60 | Hiding a line without moving `gRadioState` | Not needed: nothing is hidden per client | Rule 5 |
| #60 | Enemy targets with an empty slot | Read now, see Context; the body avoids it | Rule 3 |
| #60 | Friendly-fire lines know only player 0 | Player 0's shots only | Rule 5, #86 |
| #60 | Which lines a run reaches, US 1.1 | Still unknown, for WP-17's tests | Consequences |
| #61 1 | Down through what | `Player.shields`, with the value kept in step | Rule 1 |
| #61 2 | Missing a level, what the player does meanwhile | Left to a later record | 0007 |
| #61 3 | Does going down cost a life | Left to a later record | 0007 |
| #61 4 | Who triggers a restart | Left to a later record. Rule 2 brings every human back, at 64 after going down | 0007 |
| #61 5 | Who may retry | Left to a later record | 0007 |
| #61 6 | Return at 255, clear screen heal | The heal runs and carries over, as the next load adds 64 to what is left. A return comes back at 64 | Rule 2, 0007 |
| #61 7 | Scripted away and scripted hits | No script takes a human away, hurts them or brings them back | Rules 3, 4, 6 |
| #61 8 | Venom levels, "out until the end" | Left to a later record. Rule 2 does not keep anyone out | 0007 |
| #61 9 | One player down costs the medal | Yes, if that human is down at clear: the read is unchanged. 0008 keeps the check | Rule 1, 0008 |
| #61 10 | One rule or two | One meaning of the value for both, and a run can mix them. How each comes back is 0007's | Rule 1, 0007 |
| #61 11 | Which stores join the hash | Done in #67, see Context, and the writer adds no state. A snapshot waits for 0002. The roster must match on all clients | This record |
| #85 | Who decides a route (`Map_ChangePath`) | Left to a later record | 0008 |
| #85 comment | Any player collects a path item, the split reads player 0 (`src/engine/fox_enmy.c:2555`, `:2575`) | Left to a later record | 0008 |

Named here, in no record yet:

- **Friendly fire between humans.** Once the `src/engine/fox_beam.c:837` loop covers every player, player shots reach
  players too, as shots from index 1 already reach player 0. Rails has none (`:836`). 0007 names the same gap. It needs
  an owner before WP-17 widens that loop.
- **A player leaving during a level.** The #58 note and 0007 leave it to the session work. The character could go back
  to a computer wingman at the next level start; WP-24 has to say.

## Consequences

Easier:

- 278 reads need no edit, and upstream stays close. Routes, medals, radio and ROB's lines need no work of their own.
- The value is derived from player state every frame, so it needs no message of its own under either model in decision
  0002.
- Intros and clear cutscenes show the human's character, because they test the value, for example
  `src/overlays/ovl_i1/fox_co.c:3663-3671`.

Harder:

- A rail scene shows a second Arwing of a character a human is flying, which bends 0005's "in place of". If that looks
  wrong in a watched run, hide it (C1) or go to C4.
- Shields that carry over make a hard level harder for the next one, and 64 decides how much. Nobody has played it.
- Rescue scenes lose their stakes when a human flies the wingman, and Corneria's Falco gate then rests on the arches and
  that human staying up.
- Titania's and Sector X's radio would not fit a Slippy who is flying; those lines have to be found and dropped.
- Lost shields cost a human nothing past the level. E1 is a small change at the join if that plays too easily.

The 47 writes, for a human id:

| Writes | Lines | What happens |
|---|---|---|
| Level and run start | 10 | Before rule 2, or 255 anyway (`src/engine/fox_play.c:2936`, `:2940-2946`, `:4927`, `src/overlays/ovl_menu/fox_map.c:1578-1580`, `:3628`, `src/overlays/ovl_i3/fox_aq.c:437`) |
| All-range and boss actor code | 6 | Not reached: no AI, not spawned (`src/engine/fox_360.c:1448`, `:1466`, `:1478`, `src/engine/fox_hud.c:4247`, `:4262`, `:4535`) |
| Rail actor | 4 | Damage is 0; the retreat writes -1 only once the value is already at or below 0, which agrees (`src/engine/fox_enmy2.c:2077`, `:2090`, `:2122`, `:3337`) |
| `gTeamDamage` drain | 2 | No input once `EVOP_DAMAGE_TEAM` is stopped (`src/engine/fox_play.c:7037`, `:7039`) |
| Level clear heal | 3 | The human is parked by then, so it stands until rule 2 (`src/engine/fox_hud.c:1135-1140`) |
| Aquas clear cutscene | 8 | Forces 1 and restores the old value around each line, so nothing changes (`src/engine/fox_hud.c:6065-6088`) |
| Slippy story | 14 | Rule 6 (`src/engine/fox_play.c:2691`, `src/overlays/ovl_i2/fox_sx.c:113`, `src/overlays/ovl_i5/fox_ti.c:5053-5134`, `src/engine/fox_hud.c:4835`, `src/engine/fox_enmy2.c:1360`) |

Titania's boss code leaves -2 in the value after each line, until the next mirror write. WP-17 has to check nothing
reads it in between, or gate those lines.

To do, once accepted:

- WP-17 (#15): the writer and the level load rule in `src/port/coop/game/`, a roster lookup from team id to player, the
  all-range body, and small `// COOP:` edits at the Katt and Bill check, `EVOP_DAMAGE_TEAM`, `EVOP_RESTORE_TEAM`,
  `ActorTeamBoss_Init` (before its 255 at `src/engine/fox_hud.c:4835`), the chase and thanks lines
  (`src/engine/fox_360.c:530-552`, `:625-643`, `:1705-1752`), Sector X (two conditions) and Titania.
- WP-17 or #100: enemy fire has to reach every player, or a human can only crash.
- Research, before the Slippy code: list the lines, by message id, that speak of Slippy as absent or lost after the
  Sector X event and in Titania.
- WP-16 (#14): 0007 and 0008 build on the value as defined here. The return in 0007 is what puts the value back above 0.
- Tests with two clients: a human going down gives -1 and silences that character; a scene runs with its stand-in unhurt
  and the human's shields untouched by `EVOP_DAMAGE_TEAM`; Star Wolf chases a human in all-range and leaves them while
  they are out; each level load carries shields over and adds 64, and a player who went down comes back at 64. Only Corneria has been run, so the scene and Slippy rules need other
  levels first.
- `DESIGN.md` section 4, item 1 and the index in `decisions/README.md` get a line for this record.
