#pragma once

// One Landmaster and one Blue Marine per player (WP-18, #16; decision 0009, item 6).
//
// fox_tank.c keeps its tank's state in file globals, one set for the level. With a second tank each would read and
// write the other's: knockback, the rail wobble, the first frame of boost, collision flags and two sound gates
// (decision 0009, table under item 6). Player 0 keeps the globals as upstream; every other player gets its own copy,
// swapped in around its Player_Update. Co-op off, or player 0: nothing changes.
//
// fox_aq.c does the same for the Blue Marine, in two arrays that mix the marine's state with the level's. A marine's
// steering, lock-on and torpedo are swapped like the tank's, and also around the update of a torpedo it fired. The
// level's light and fog, and the one marine lamp, stay player 0's: whatever another marine's update writes there is
// put back.

#include <stdint.h>

#include "players.h"

#ifdef __cplusplus
extern "C" {
#endif

// The fox_tank.c values one tank owns. Laid out without padding, as the state hash reads it as bytes.
typedef struct {
    float boostStart;   // D_800C9F24
    float jetStart;     // D_800C9F20
    uint8_t knockback;  // D_800C9F00
    uint8_t hitFlag;    // D_800C9F04
    uint8_t wobble;     // D_800C9F08
    uint8_t soundGate;  // D_800C9F3C
} CoopTankState;

// Index = player; slot 0 is unused, as player 0 keeps the fox_tank.c globals. Hashed in state_hash.c.
extern CoopTankState gCoopTankState[COOP_PLAYERS_MAX];

// The fox_aq.c values one Blue Marine owns. Only 4-byte fields, so no padding.
#define COOP_MARINE_INTS 6
#define COOP_MARINE_FLOATS 15
typedef struct {
    int32_t ints[COOP_MARINE_INTS];     // D_i3_801C4190[0, 1, 3, 4, 5, 7]: lock-on target, torpedo slot, bump sound
    float floats[COOP_MARINE_FLOATS];   // D_i3_801C41B8[0-11, 21-23]: aim, steering, camera offset, torpedo position
    int32_t torpedoTimer;               // D_i3_801C4454
    float torpedoSpeed;                 // D_i3_801C4458
    float torpedoTurn;                  // D_i3_801C445C
} CoopMarineState;

// Index = player; slot 0 is unused, as for the tank. Hashed in state_hash.c.
extern CoopMarineState gCoopMarineState[COOP_PLAYERS_MAX];

// Clears every copy. Call it with CoopPlayers_LevelInit.
void CoopVehicles_LevelInit(void);

// Registers the swaps around Player_Update and around a player shot's update.
void CoopVehicles_Init(void);

#ifdef __cplusplus
}
#endif
