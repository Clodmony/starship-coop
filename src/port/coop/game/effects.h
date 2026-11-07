#pragma once

// Enemy effects against the players other than the one the game names (#100).
//
// Upstream, an effect tests one player: player 0, named gPlayer or &gPlayer[0]. Each function here repeats one of those
// tests for the other co-op players and sits next to the upstream test, which goes on testing player 0. Who an effect
// is aimed at does not change. A parked player cannot be hit. Outside co-op these do nothing, versus included: there
// each player has its own camera and upstream's rules stand.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Effect;

// The test of func_effect_8007A774, for every co-op player but `named`. Effect397 passes a player in a barrel roll by,
// as Bolse_Effect397_Update does for player 0.
void CoopEffects_BoxHit(int32_t named, struct Effect* effect, float size);

// The test of a shot that a barrel roll deflects and a hit uses up, for the players after player 0: the one in
// Effect_Effect353_354_Update (enemy lasers), and in Effect_Effect398_Update.
void CoopEffects_ShotHit(struct Effect* effect);

// The test of the column Effect_Effect374_Update raises, for the players after player 0.
void CoopEffects_ColumnHit(struct Effect* effect);

// The test of the beam in Effect_Effect395_Update, state 10, for the players after player 0. gCalcMatrix must hold the
// beam's rotation, and still does afterwards.
void CoopEffects_BeamHit(struct Effect* effect);

#ifdef __cplusplus
}
#endif
