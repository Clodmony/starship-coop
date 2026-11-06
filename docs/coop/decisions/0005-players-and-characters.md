# 0005: Players and characters: player 1 is Fox, players 2-4 pick a wingman

- Status: Accepted
- Date: 2026-11-06
- Issue: #7

## Context

Observed:

- The team has six ids: `TEAM_ID_FOX`, `TEAM_ID_FALCO`, `TEAM_ID_SLIPPY`, `TEAM_ID_PEPPY`, `TEAM_ID_KATT`,
  `TEAM_ID_BILL` (`include/sf64player.h:16-24`). The wingmen are not players: they are actors with their own shields
  in `gTeamShields[6]` and `gSavedTeamShields[6]` (`include/sf64context.h:23-24`), copied in at level init
  (`src/engine/fox_play.c`, `Play_Init`).
- Player data has room for four: `gPlayer` is allocated per player, and the per-player arrays have 4 slots
  (`gLifeCount[4]`, `gLaserStrength[4]`, `gBombCount[4]`, `include/sf64context.h`). Versus mode runs four players.
  Co-op counts players apart from cameras in WP-10 (#8, PR #49), capped at 4
  (`src/port/coop/game/players.h` on that branch).
- Much of the campaign assumes player 0 is Fox and the player the camera follows: 1577 uses of `gPlayer[0]` in the
  game code (WP-05 audit, `docs/coop/audit/single-player.md`, #45).
- WP-11 (#50) brings player 2 into all-range sections as a separate Arwing beside player 0; who that Arwing is in the
  story was left open.

Unknown:

- Which secret routes and events depend on a wingman, and how. Known from play, not checked in code: in Corneria,
  saving Falco leads to the other route.
- How the original brings back a wingman whose shields ran out (from memory: they miss the next level and return
  after it; not checked in code).
- Which radio lines a wingman says on its own (calls for help, warnings) and which are story lines.

## Options

1. **Player 2+ as extra Arwings, wingmen unchanged.** Least story work, but four or more Arwings plus three wingmen
   crowd the screen, and the radio talks about a team the players are not part of.
2. **Player 2-4 fly in place of wingmen, player 1 is always Fox** (DESIGN.md section 4, item 1, extended to a free
   choice). Keeps the team of four and the radio. A wingman with no player stays a computer wingman. Costs: replacing
   a wingman actor by a player (WP-17), and deciding what the story events of that wingman do when a human flies it.
3. **Anyone plays anyone, Fox included.** Most freedom. Costs: every place that treats player 0 as Fox and as the
   camera's player has to be found and changed; large, see the WP-05 audit.

## Decision

Up to four human players. Player 1, the host, is always Fox. Players 2 to 4 each choose Falco, Peppy or Slippy; no two
players fly the same character. A wingman no player chose stays a computer wingman, as in the original. "Anyone plays
anyone", Fox included, is a goal for later and needs its own record.

Open, to research before code depends on it, each to be settled in a later record:

- **Secret routes and wingman events:** what happens to an event that needs a wingman (such as saving Falco) when a
  human flies that wingman.
- **Radio:** the owner prefers keeping the original radio; lines that no longer fit a human-flown character (calls
  for help) may be dropped, story lines kept.
- **Damage and return:** whether a human-flown wingman whose shields run out misses the next level as in the original,
  or comes back sooner. Belongs with the lives and respawn rules (DESIGN.md section 4, items 2 and 3; WP-16).

## Consequences

- WP-17 (#15) becomes "player 2-4 replace the chosen wingman" instead of "player 2 as Falco". Its radio and team
  shield work follows this record.
- The join flow (WP-24, #22) and the co-op menu (WP-25, #23) need a character choice for players 2-4 and must refuse a
  character already taken.
- The session (WP-21, #19) has to carry each player's character to all clients.
- Engine code keeps player 0 as Fox; nothing needs to change for that now.
- Research tasks to open: wingman events and secret routes; wingman radio lines; how a downed wingman returns.
