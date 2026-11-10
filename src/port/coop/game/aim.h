#pragma once

// Who an enemy aims at when more than one human flies (#193), and whom a boss or a dogfighter follows (#213).
//
// Upstream, aimed enemy shots and homing missiles aim at player 0. In co-op they aim at the nearest human player
// instead, by the owner's choice on #193 (option 1). The computer wingmen stay targets of their own rules (the team
// missiles, EVOP_SET_TARGET); this only replaces "player 0" where an enemy aims at the player.
//
// Bosses (Area 6's Gorgon, Andross) and all-range enemies that chase Fox follow the nearest human too (#213), but they
// keep the player they picked, so one does not flip between two players at about the same distance: see "Kept
// targets" below.
//
// Co-op off: always player 0, as upstream.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The index into gPlayer an enemy at (x, y, z) aims at: the nearest of player 0 and every co-op player that flies
// (joined, not parked, and active or in a U-turn), by straight-line distance to (pos.x, pos.y, trueZpos), in the
// simulation's own coordinates. A tie goes to the lower index, so every game of a session picks the same player. 0
// outside co-op.
int32_t CoopAim_Nearest(float x, float y, float z);

// ---- Kept targets (#213) ----
//
// One slot per boss (gBosses) and per actor (gActors): the player that object follows. A call with the object's
// position returns the kept player, and picks again with CoopAim_Nearest from that position when the slot is empty or
// its player can no longer be aimed at (parked, down, out). A slot is emptied by the level start, by a new object in
// its place (ObjectInitEvent), and by the object's own AI where it chooses a new target or a new attack
// (CoopAim_Forget*). The slots are simulation state and join the state hash. Co-op off: always 0, nothing is kept.

int32_t CoopAim_BossTarget(int32_t boss, float x, float y, float z);
void CoopAim_ForgetBoss(int32_t boss);
// CoopAim_ForgetBoss, then CoopAim_BossTarget: picks again now, where the boss's AI makes a new choice.
int32_t CoopAim_RepickBoss(int32_t boss, float x, float y, float z);
int32_t CoopAim_ActorTarget(int32_t actor, float x, float y, float z);
void CoopAim_ForgetActor(int32_t actor);

// ---- Held players (#213) ----
//
// The Andross brain catches one player at a time (owner's choice A on #213): the player it catches is held until the
// catch ends, whatever its kept target does meanwhile.

// The player the Andross brain at (x, y, z) catches: of the players in its reach (Andross_AndBrain_Update's box, 300
// around (x, y - 300, z)) the nearest, a tie to the lower index; players after player 0 only while they fly. -1 for
// nobody. Outside co-op: 0 when player 0 is in reach, else -1, as upstream.
int32_t CoopAim_BrainCatch(float x, float y, float z);
// Sets the player boss `boss` holds (-1 for none).
void CoopAim_SetHeld(int32_t boss, int32_t player);
// The player boss `boss` holds; 0 when none, and outside co-op.
int32_t CoopAim_Held(int32_t boss);

struct Actor;

// The player an all-range actor whose target is Fox (aiIndex AI360_FOX) goes after: for an enemy (Star Wolf, or an
// aiType from AI360_ENEMY up to the Great Fox) its kept target, CoopAim_ActorTarget from its position; for Fox's own
// side (Falco, Slippy, Peppy, Katt, Bill), who escort Fox, player 0 as upstream. 0 outside co-op.
int32_t CoopAim_Chased(const struct Actor* actor);

// Registers the ObjectInitEvent listener that empties the slot of a new boss or actor. Call once at startup.
void CoopAim_Init(void);

// Empties every slot. Call from CoopPlayers_LevelInit.
void CoopAim_LevelInit(void);

// Every slot: the bosses' kept targets, the actors', then the players the bosses hold; -1 for an empty one, else a
// player index. For the state hash.
int8_t* CoopAim_KeptSlots(int32_t* count);

#ifdef __cplusplus
}
#endif
