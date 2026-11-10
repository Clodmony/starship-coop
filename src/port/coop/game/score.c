#include "score.h"

#include "global.h"
#include "port/coop/game/players.h"

int32_t CoopScore_HumanKill(int32_t dmgSource) {
    if (dmgSource == AI360_FOX + 1) {
        return 1;
    }
    return CoopPlayers_IsCoop() && (dmgSource > COOP_DMG_SRC_PLAYER) &&
           (dmgSource < COOP_DMG_SRC_PLAYER + CoopPlayers_Count());
}

int32_t CoopScore_HumanShot(int32_t sourceId) {
    if (sourceId == TEAM_ID_FOX) {
        return 1;
    }
    return CoopPlayers_IsCoop() && (sourceId > 0) && (sourceId < CoopPlayers_Count());
}

int32_t CoopScore_LifeSlot(int32_t slot) {
    return CoopPlayers_IsCoop() ? 0 : slot;
}
