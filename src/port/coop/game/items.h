#pragma once

// Who an item or a level event looks at when more than one human flies (#201).
//
// Upstream, the supply crate, the laser item and the laser triggers of the level scripts look at player 0. In co-op,
// by the owner's choices on #201, they look at every active player: the crate drops what ANY of them needs, and the
// laser triggers fire on the best lasers any of them has. An active player is player 0, or a joined player who is not
// parked and flies (active or in a U-turn). Every test is the one upstream makes on player 0, applied to each of them.
//
// Co-op off: only player 0, so each function gives upstream's answer.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Nonzero when player `i` is one the functions below look at.
int32_t CoopItems_Flies(int32_t i);

// Some active Arwing player has a broken wing / shields below `limit` / single lasers (an Arwing, as upstream asks).
int32_t CoopItems_AnyWingBroken(void);
int32_t CoopItems_AnyShieldsBelow(int32_t limit);
int32_t CoopItems_AnySingleLasers(void);

// The level events EVC_SINGLE_LASER (strength 0), EVC_TWIN_LASER, EVC_HYPER_LASER and EVC_UNK3_LASER (their
// gLaserStrength): an active player has that strength with the guns out; for 0, no active player has the guns out.
int32_t CoopItems_LaserEvent(int32_t strength);

// Player `i` has a broken wing (an Arwing with a wing at WINGSTATE_BROKEN or worse).
int32_t CoopItems_WingBroken(int32_t i);

// The level event EVC_WING_BROKEN (#210): some active player has a broken wing. Upstream's test of player 0's two wing
// states, applied to each active player as the owner's rule for the laser events of #201 asks ("any active player").
int32_t CoopItems_WingBrokenEvent(void);

#ifdef __cplusplus
}
#endif
