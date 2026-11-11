#pragma once

// How many Player structs the game simulates, apart from how many cameras it renders (WP-10).
//
// Upstream ties both to gCamCount: 1 in the campaign, 4 in versus. gCamCount is also read as "is this versus?" in
// about 25 places (gCamCount != 1, gCamCount == 4), so co-op keeps gCamCount at 1 and counts players here instead.
// Loops that run once per player use CoopPlayers_Count(); loops and checks that mean "per camera" or "versus" keep
// gCamCount. See docs/coop/NOTES.md (WP-10) for the list of sites.
//
// Co-op off: CoopPlayers_Count() == gCamCount, so the game behaves as upstream.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COOP_PLAYERS_MAX 4 // the per-player arrays in sf64context.h have 4 slots

// Fixes the player count for the level being started. Call it in Play_Init, after gCamCount is set and before
// gPlayer is allocated. The count comes from the roster of the co-op session; without one, from the debug CVar
// COOP_PLAYERS_DEBUG_CVAR (default 1, clamped to 1..COOP_PLAYERS_MAX). Outside the campaign (gCamCount != 1) it is
// always gCamCount.
void CoopPlayers_LevelInit(void);

// Who is in the levels from the next Play_Init on: one CoopCharacter per player, 0 for nobody. The count goes up to the
// last player there, and a slot below that with nobody in it stays out of the level. NULL goes back to the debug CVar:
// call it when the session ends.
void CoopPlayers_SetRoster(const uint8_t* roster);

// What player `index` flies in that roster, as a CoopCharacter; 0 without a roster. Until WP-17 nothing reads it.
int32_t CoopPlayers_Character(int32_t index);

// Number of Player structs in gPlayer for the current level.
int32_t CoopPlayers_Count(void);

// Nonzero when more players are simulated than cameras rendered, which only co-op does.
int32_t CoopPlayers_IsCoop(void);

// Index of this client's own player, parked or not: who it is, so who its controller steers. Always 0 outside co-op,
// and when the index asked for is not a player of this level. For what a client sees and hears use
// CoopPlayers_ViewIndex.
int32_t CoopPlayers_LocalIndex(void);

// Index of the player this client looks through: what Game_Draw draws, and what its camera, HUD and sound follow. The
// local player while it flies; while that is parked, before it joins, in a cut or after it went down, player 0. The
// simulation reads it only to pick the sounds a client plays, through CoopPlayers_IsViewed; what it hashes never depends
// on it: outside Game_Draw gPlayerNum is 0 or the player being updated (src/port/coop/game/view.h).
int32_t CoopPlayers_ViewIndex(void);

// Nonzero when what player `index` does is for this client's eyes and ears: the player it looks through in co-op,
// always outside it.
int32_t CoopPlayers_IsViewed(int32_t index);

// The player whose numbers the HUD shows. gPlayerNum stays 0 for the HUD in co-op, because the HUD also writes the
// simulation (lives, the bomb limit, the pause menu), so what it only shows comes from here. gPlayerNum outside co-op.
int32_t CoopPlayers_HudIndex(void);

// Asks for a local player. 0 until something calls this: lockstep with the playerIndex the session gave this client
// (src/port/coop/sync/lockstep_game.h), or SF64_COOP_TEST_LOCAL_INDEX (src/port/coop/test/switches.h).
void CoopPlayers_SetLocalIndex(int32_t index);

// Nonzero while player `index` is parked: allocated, but not updated, not given a camera update and not drawn.
// In co-op, players 1 and up start parked and join when the level reaches a section that supports them.
int32_t CoopPlayers_IsParked(int32_t index);

// Call before Player_Update for player `index`; nonzero means skip it this frame. Brings a parked player in when its
// section starts: once player 0 is active on rails in an Arwing (WP-12) or in all-range mode (WP-11), as a copy of
// player 0 placed beside it. It parks the player again when player 0 stops flying, for example in the cut from rails
// to all-range, and brings it back when player 0 flies again.
int32_t CoopPlayers_SkipUpdate(int32_t index);

struct Player;

// Call at the start of Player_UpdatePath. Nonzero means `player` is a co-op player other than player 0: it took player
// 0's place on the path and Player_UpdatePath must return, because the rest would write the path globals and move the
// path by this player's speed. The player keeps its own x and y inside the flight box and its own camDist, so boost
// and brake move it ahead of and behind player 0; the speed along the path is the team's (CoopPlayers_PathBoostSpeed).
int32_t CoopPlayers_FollowPath(struct Player* player);

// The boostSpeed Player_MoveArwingOnRails flies with: the team's in a session (#194), the player's own otherwise.
float CoopPlayers_PathBoostSpeed(const struct Player* player);

// Points gInputHold, gInputPress and gControllerRumble at player `index`, in co-op only: its pad slot, which holds that
// player's input for this frame whoever steers it (input.h), and its rumble flag.
void CoopPlayers_SelectInput(int32_t index);

// The pad slot to read for something that belongs to player `index`, such as a shot of that player: its own slot in
// co-op, gMainController otherwise, as upstream.
int32_t CoopPlayers_PadOf(int32_t index);

struct Actor;

// Call in Actor_Update where the campaign runs down its lock-on. Nonzero means co-op, and the lock-ons of every player
// on `actor` were handled here: each player's runs down on its own A. Upstream only looks at player 0's.
int32_t CoopPlayers_ReleaseLockOns(struct Actor* actor);

// Call at the start of Player_Down. Nonzero means a co-op player other than player 0 went down: it is taken out of
// the level and Player_Down must return, because it also stops the music, plays the "player down" theme and starts
// the fade to a level restart for everyone. The player comes back beside player 0 after 150 game frames, for one life
// from the pool gLifeCount[0], with full shields (decision 0007 rule 4); CoopPlayers_SkipUpdate brings it back. Player
// 0 going down is not handled here: CoopPlayers_RefillFox keeps it from going down in a level open for 0007 rule 3,
// and otherwise it keeps the original restart.
int32_t CoopPlayers_OnPlayerDown(struct Player* player);

// Call where Player_Update is about to call Player_Down, as part of that test: nonzero means player 0 was refilled in
// place and must not go down (decision 0007 rule 3). That is so in co-op, in a level open for it (rule 12; Corneria),
// while the pool gLifeCount[0] holds a life: the life is spent and player 0 gets full shields, both wings, 3 bombs, a
// single laser and the mercyTimer of a return, where it flies. No fade, no restart, the path does not stop and the
// other players fly on. Zero otherwise (not player 0, co-op off, a level that is not open, pool 0): the caller goes
// on to Player_Down, as upstream. Reads and writes simulated state only, inside the player loop.
int32_t CoopPlayers_RefillFox(struct Player* player);

// Test switch SF64_COOP_TEST_FOX_REFILL_OFF: nonzero closes every level for the refill.
void CoopPlayers_SetFoxRefillOff(int32_t off);

// How often player 0 was refilled since the last Play_Init. For the co-op log; not simulation state.
int32_t CoopPlayers_FoxRefillCount(void);

// ---- Spike #183 (decision 0007 rule 13): passing the lead. An experiment behind SF64_COOP_TEST_LEAD_SWAP; with the
// switch off none of it does anything, and nothing of it is a rule yet. See docs/coop/NOTES.md, 2026-11-11.

// The session player this client is (what CoopPlayers_SetLocalIndex was given). CoopPlayers_LocalIndex is the slot that
// player flies: the same number unless a lead swap moved it.
int32_t CoopPlayers_SeatIndex(void);

// The session player that flies slot `slot`: `slot` itself unless a lead swap moved it. After CoopInput_Route has
// filled the pad slots by session player, input_game.c puts them in slot order with this.
int32_t CoopPlayers_SeatOfSlot(int32_t slot);

// Test switch: at pool 0, where player 0 would go down while another player flies, that player takes slot 0.
void CoopPlayers_SetLeadSwapOn(int32_t on);

// Lead swaps since the last Play_Init.
int32_t CoopPlayers_LeadSwapCount(void);

// What Actor.dmgSource holds after a hit by `sourceId`: a player index, or an NPC's shot id. Upstream writes
// sourceId + 1, which for player index 1 is DMG_SRC_2, the value an enemy's hit on a wingman has. So in co-op, players
// 1 and up get COOP_DMG_SRC_PLAYER + index instead, a value no game code writes or tests: rules that test for player 0
// (AI360_FOX + 1) or for an enemy (DMG_SRC_2) do not fire for them. Co-op off, player 0 and NPCs stay sourceId + 1.
// Whether a rule should count them is up to the work package that owns it, see the shooter rows of sp-verdicts.tsv.
int32_t CoopPlayers_DmgSource(int32_t sourceId);

#define COOP_DMG_SRC_PLAYER 10 // 5 to 99 are free: 1 to 4 are versus players, 100 and up enemies and NPCs

// Call at the start of Player_LowHealthMsg. Nonzero means `player` is a co-op player other than player 0 and
// Player_LowHealthMsg must return: gTeamLowHealthMsgTimer is one timer, so it would step once per player. It stays on
// player->num, never on CoopPlayers_LocalIndex: the line it leads to draws from the RNG, so every client has to take
// this branch for the same player. That player 0's shields decide is a neutral pick, not a rule (NOTES.md, 2026-11-06,
// #87); a later record can change it here.
int32_t CoopPlayers_SkipLowHealthMsg(struct Player* player);

// Call after every change of player->arwing.laserGunsYpos (Player_Shoot, Player_Setup). In co-op it sets
// laserGunsXpos from it, as Display_ArwingWings does when it draws the wings, so the field ends every frame the same on
// every client: a client that looks through the player in cockpit view, or has it out of view, does not draw the wings
// (#192). Nothing outside co-op, where the draw alone writes it, as upstream.
void CoopPlayers_SetLaserGunsX(struct Player* player);

// Nonzero when Start went down on this frame for whoever may pause or resume, or skip a level intro (#191): any player
// in co-op, by the owner's decision on #12, gMainController otherwise. The pad slots hold every player's input on every
// client (input.h), so all of them pause on the same frame. What the pause menu does next stays on player 0, as
// gInputPress does.
int32_t CoopPlayers_PausePressed(void);

// Call in Play_UpdateLevel's LEVEL_SOLAR case, after the heat drain of player 0 (#200). In co-op it drains the shields of
// every other player who flies (not parked, PLAYERSTATE_ACTIVE) by that player's own height, at the rate player 0's
// drain has, and plays the shield warnings only for the player this client views. gInvincible stops it, as upstream.
// Nothing outside co-op. Reads simulation state only.
void CoopPlayers_SolarHeat(void);

#define COOP_PLAYERS_DEBUG_CVAR "gCoopDebugPlayerCount"

// The slot of player `index` as a plain number, for the state hash: a number has the same size on every compiler, an
// enum need not. Which number means what is private to players.c.
int32_t CoopPlayers_SlotState(int32_t index);

// Test only: sets what CoopPlayers_SlotState returns. The state hash probe changes a slot, hashes and puts it back.
void CoopPlayers_SetSlotState(int32_t index, int32_t state);

// Nonzero while player `index` is out after going down and has not come back (decision 0007 rule 4). It is parked then.
int32_t CoopPlayers_IsDown(int32_t index);

// Nonzero while player `index` has no human in this session: its roster seat is empty (a session of three on a
// four-player count, or a player who left before the level began). Such a slot never joins and has no pad to wait for.
// Simulation state. (#252: the level clear screen does not wait for it.)
int32_t CoopPlayers_IsOut(int32_t index);

// Game frames until player `index`, down, may come back (decision 0007 rule 4); 0 when none are left or it is not down.
// Simulation state, for the state hash.
int32_t CoopPlayers_ReturnTimer(int32_t index);

// Test only: sets what CoopPlayers_ReturnTimer returns, for the state hash probe, as CoopPlayers_SetSlotState.
void CoopPlayers_SetReturnTimer(int32_t index, int32_t frames);

#ifdef __cplusplus
}
#endif
