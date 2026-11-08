#pragma once

// Who an enemy aims at when more than one human flies (#193).
//
// Upstream, aimed enemy shots and homing missiles aim at player 0. In co-op they aim at the nearest human player
// instead, by the owner's choice on #193 (option 1). The computer wingmen stay targets of their own rules (the team
// missiles, EVOP_SET_TARGET); this only replaces "player 0" where an enemy aims at the player.
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

#ifdef __cplusplus
}
#endif
