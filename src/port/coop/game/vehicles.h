#pragma once

// One Landmaster per player (WP-18, #16; decision 0009, item 6).
//
// fox_tank.c keeps its tank's state in file globals, one set for the level. With a second tank each would read and
// write the other's: knockback, the rail wobble, the first frame of boost, collision flags and two sound gates
// (decision 0009, table under item 6). Player 0 keeps the globals as upstream; every other player gets its own copy,
// swapped in around its Player_Update. Co-op off, or player 0: nothing changes.

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

// Clears every copy. Call it with CoopPlayers_LevelInit.
void CoopVehicles_LevelInit(void);

// Registers the swap around Player_Update.
void CoopVehicles_Init(void);

#ifdef __cplusplus
}
#endif
