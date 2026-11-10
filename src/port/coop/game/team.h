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
// Decision 0006 rule 3 (B3) off rails: in all-range the human's slot holds a hidden body on the human, and in boss
// fights the computer wingman of a human-flown character does not spawn (CoopTeam_Init, CoopTeam_LeavesBossFight).
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

// What a wingman's bar draws (the pause, level start and level clear status rows, the radio portrait): gTeamShields,
// but for a joined human with the team's shield upgrade (three gold rings, up to 383 shields, a gauge 1.5 times as
// wide) its share of that larger gauge on the bar's 0..255 scale. gTeamShields itself stays held to 1..255 (rule 1),
// so a human at 300 of 383 shows 199, not a full bar. Draw only: nothing in the simulation reads this.
int32_t CoopTeam_DisplayShields(int32_t teamId);

// The team id `actor`'s current script means by `teamId`: the substitute if the script belongs to a scene of that
// wingman and a human flies it, otherwise `teamId`.
int32_t CoopTeam_SceneTeam(struct Actor* actor, int32_t teamId);

// Decision 0012 B4, the guests: with no computer wingman free, Katt flies the scene, else Bill. For the scene's
// EVC_*_ACTIVE gate: nonzero when the flyer is up (a guest always is).
int32_t CoopTeam_SceneActive(struct Actor* actor, int32_t teamId);
// For EVOP_SET_TARGET: the actor index of the scene's flyer (gTeamEventActorIndex has no slot for a guest).
int32_t CoopTeam_SceneTarget(struct Actor* actor, int32_t teamId);
// For EVOP_INIT_ACTOR: EVID_KATT or EVID_BILL in place of EVID_TEAMMATE in a scene's own wingman script flown by a
// guest; `eventType` otherwise.
int32_t CoopTeam_SceneEventType(struct Actor* actor, int32_t eventType);
// Nonzero when `actor`'s scene is flown by a guest: its arrival fanfare does not play.
int32_t CoopTeam_SceneGuest(struct Actor* actor);

// For EVOP_PLAY_MSG in `actor`'s script. Returns 0 when the line must not play; otherwise it may have changed
// *msgId and *speaker to the substitute's line of the same set.
int32_t CoopTeam_SceneMessage(struct Actor* actor, int32_t* msgId, int32_t* speaker);

// The scene choices of this level, for the state hash: one byte per scene, 0 while not chosen, else the team id.
const uint8_t* CoopTeam_Choices(int32_t* count);

// Decision 0006 rule 3, all-range and boss fights. Call once at start-up: registers the listeners that turn the
// all-range wingman in a human-flown character's slot (gActors[1] to [3]) into a body while the level is played: no
// AI, shots, hitbox or model, placed on its human every frame so enemies aimed at the slot aim at the human, and in
// the retreat state while the human is down or not flying. Intro and level-clear scenes keep the game's wingman.
void CoopTeam_Init(void);

// Nonzero when the boss-fight wingman (ActorTeamBoss) of `teamId` must not spawn: a human flies that character.
int32_t CoopTeam_LeavesBossFight(int32_t teamId);

// What `actor` is as a human-flown wingman's all-range body, for tests.
#define COOP_TEAM_BODY_NONE 0 // not a body: any other actor, or the wingman's own AI flies it
#define COOP_TEAM_BODY_ON 1   // a body on its human, who flies
#define COOP_TEAM_BODY_AWAY 2 // a body in the retreat state, its human down or not flying
#define COOP_TEAM_BODY_OFF 3  // a body somewhere its human is not: a fault
int32_t CoopTeam_Body(const struct Actor* actor);

#ifdef __cplusplus
}
#endif
