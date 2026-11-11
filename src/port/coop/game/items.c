#include "items.h"

#include "global.h"
#include "port/coop/game/players.h"

int32_t CoopItems_Flies(int32_t i) {
    const Player* p = &gPlayer[i];

    if ((i == 0) || !CoopPlayers_IsCoop()) {
        return i == 0;
    }
    return !CoopPlayers_IsParked(i) && ((p->state == PLAYERSTATE_ACTIVE) || (p->state == PLAYERSTATE_U_TURN));
}

static s32 PlayerCount(void) {
    return CoopPlayers_IsCoop() ? CoopPlayers_Count() : 1;
}

int32_t CoopItems_WingBroken(int32_t i) {
    const Player* p = &gPlayer[i];

    return (p->arwing.rightWingState <= WINGSTATE_BROKEN) || (p->arwing.leftWingState <= WINGSTATE_BROKEN);
}

int32_t CoopItems_AnyWingBroken(void) {
    s32 i;

    for (i = 0; i < PlayerCount(); i++) {
        if (CoopItems_Flies(i) && CoopItems_WingBroken(i) && (gPlayer[i].form != FORM_LANDMASTER)) {
            return 1;
        }
    }
    return 0;
}

int32_t CoopItems_WingBrokenEvent(void) {
    s32 i;

    for (i = 0; i < PlayerCount(); i++) {
        if (CoopItems_Flies(i) && CoopItems_WingBroken(i)) {
            return 1;
        }
    }
    return 0;
}

int32_t CoopItems_AnyShieldsBelow(int32_t limit) {
    s32 i;

    for (i = 0; i < PlayerCount(); i++) {
        if (CoopItems_Flies(i) && (gPlayer[i].shields < limit)) {
            return 1;
        }
    }
    return 0;
}

int32_t CoopItems_AnySingleLasers(void) {
    s32 i;

    for (i = 0; i < PlayerCount(); i++) {
        if (CoopItems_Flies(i) && (gLaserStrength[i] == LASERS_SINGLE) && (gPlayer[i].form != FORM_LANDMASTER)) {
            return 1;
        }
    }
    return 0;
}

int32_t CoopItems_LaserEvent(int32_t strength) {
    s32 i;

    for (i = 0; i < PlayerCount(); i++) {
        if (!CoopItems_Flies(i)) {
            continue;
        }
        if (strength == LASERS_SINGLE) {
            if (!(gPlayer[i].arwing.laserGunsYpos > -8.0f)) {
                return 0;
            }
        } else if ((gPlayer[i].arwing.laserGunsYpos < -8.0f) && (gLaserStrength[i] == strength)) {
            return 1;
        }
    }
    return strength == LASERS_SINGLE;
}
