#pragma once

// A wingman a human flies (WP-17, #15): what gTeamShields[id] holds for that character, and who flies its scenes.
//
// Decision 0006 rules 1, 3 and 4: while a human flies Falco, Slippy or Peppy, gTeamShields[id] holds that player's
// shields, held to 1..255, and -1 once the player went down; no script hurts or restores a human id, and a scene actor
// that stands in for a human takes no damage. So every read of gTeamShields (gates, medals, radio, routes) means the
// human.
//
// Decision 0012 (A2, C2): a scene (a wingman's fly-in or rescue script and the scripts tied to it) that would show a
// human-flown wingman is flown by the next free computer wingman in the order Falco, Slippy, Peppy, round. The
// scene's id is remapped in EVOP_SET_TEAM_ID, EVC_*_ACTIVE, EVOP_DAMAGE_TEAM and EVOP_SET_TARGET, and the taken
// wingman's lines play in the substitute's voice where the game recorded the line for all three, and not otherwise.
// With no free computer wingman the scene keeps its stand-in (0006 C1); Katt and Bill (0012 B4) are a later step.
//
// Co-op off, or a character no human flies: every function here leaves the game as upstream.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Actor;

// Call from CoopPlayers_LevelInit: forgets the scene choices of the last level.
void CoopTeam_LevelInit(void);

// Call once per frame after the player loop: writes each joined human's shields to its character's gTeamShields.
void CoopTeam_Update(void);

// Call when co-op player `index` goes down: its character's gTeamShields becomes -1.
void CoopTeam_OnPlayerDown(int32_t index);

// Nonzero when `teamId` (TEAM_ID_FALCO to TEAM_ID_PEPPY) is flown by a human in this co-op level.
int32_t CoopTeam_IsHuman(int32_t teamId);

// The team id `actor`'s current script means by `teamId`: the substitute if the script belongs to a scene of that
// wingman and a human flies it, otherwise `teamId`.
int32_t CoopTeam_SceneTeam(struct Actor* actor, int32_t teamId);

// For EVOP_PLAY_MSG in `actor`'s script. Returns 0 when the line must not play; otherwise it may have changed
// *msgId and *speaker to the substitute's line of the same set.
int32_t CoopTeam_SceneMessage(struct Actor* actor, int32_t* msgId, int32_t* speaker);

// The scene choices of this level, for the state hash: one byte per scene, 0 while not chosen, else the team id.
const uint8_t* CoopTeam_Choices(int32_t* count);

#ifdef __cplusplus
}
#endif
