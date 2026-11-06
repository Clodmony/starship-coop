#include "players.h"

#include "global.h"
#include "public/bridge/consolevariablebridge.h"

// Set once per level by CoopPlayers_LevelInit, so the count cannot change while gPlayer is allocated for it.
static s32 sCoopPlayerCount = 1;

void CoopPlayers_LevelInit(void) {
    s32 count = 1;

    if (gCamCount == 1) {
        count = CVarGetInteger(COOP_PLAYERS_DEBUG_CVAR, 1);
        if (count < 1) {
            count = 1;
        }
        if (count > COOP_PLAYERS_MAX) {
            count = COOP_PLAYERS_MAX;
        }
    }
    sCoopPlayerCount = count;
}

int32_t CoopPlayers_Count(void) {
    if (gCamCount != 1) {
        return gCamCount; // versus: one player per camera, as upstream
    }
    return sCoopPlayerCount;
}

int32_t CoopPlayers_IsCoop(void) {
    return CoopPlayers_Count() > gCamCount;
}

int32_t CoopPlayers_LocalIndex(void) {
    return 0;
}
