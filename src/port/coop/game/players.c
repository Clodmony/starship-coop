#include "players.h"

#include "global.h"
#include "public/bridge/consolevariablebridge.h"

// Set once per level by CoopPlayers_LevelInit, so the count cannot change while gPlayer is allocated for it.
static s32 sCoopPlayerCount = 1;

// Players 1 and up in co-op. A player that is not JOINED is parked: allocated but not updated, not given a camera
// update and not drawn (its draw flag is 0). They start WAITING and join when the level reaches a section that
// supports them (WP-11: all-range; rails are WP-12). A joined player that goes down, or whose section ends, is OUT
// until the next Play_Init; when and how it comes back is a game rule (WP-07, WP-16).
typedef enum {
    COOP_SLOT_JOINED, // also every player outside co-op, and player 0
    COOP_SLOT_WAITING,
    COOP_SLOT_OUT,
} CoopSlot;

static CoopSlot sSlot[COOP_PLAYERS_MAX];

// How far a joining player is placed from player 0, per player index (x to the side, y up), in world units.
#define COOP_JOIN_OFFSET_X 250.0f
#define COOP_JOIN_OFFSET_Y 80.0f

void CoopPlayers_LevelInit(void) {
    s32 count = 1;
    s32 i;

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

    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        sSlot[i] = ((gCamCount == 1) && (i > 0) && (i < count)) ? COOP_SLOT_WAITING : COOP_SLOT_JOINED;
    }
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

int32_t CoopPlayers_IsParked(int32_t index) {
    if ((index < 0) || (index >= COOP_PLAYERS_MAX) || !CoopPlayers_IsCoop()) {
        return 0;
    }
    return sSlot[index] != COOP_SLOT_JOINED;
}

// Player i joins as a copy of player 0, set beside it. Player_Setup is not run for it: besides the player struct it
// resets level-wide state (radio, music, the level start screen, intro flags), which would disturb player 0. So the
// per-player arrays that Player_Setup fills for its player index are copied from player 0 instead.
static void CoopPlayers_JoinFromPlayer0(s32 i) {
    Player* p = &gPlayer[i];

    *p = gPlayer[0];
    p->num = i;
    p->pos.x += COOP_JOIN_OFFSET_X * i;
    p->pos.y += COOP_JOIN_OFFSET_Y * i;
    p->cam.eye.x += COOP_JOIN_OFFSET_X * i;
    p->cam.eye.y += COOP_JOIN_OFFSET_Y * i;
    p->cam.at.x += COOP_JOIN_OFFSET_X * i;
    p->cam.at.y += COOP_JOIN_OFFSET_Y * i;

#define COPY_FROM_0(arr) (arr)[i] = (arr)[0]
    COPY_FROM_0(gLeftWingHealth);
    COPY_FROM_0(gRightWingHealth);
    COPY_FROM_0(gUturnDownTimers);
    COPY_FROM_0(gUturnBrakeTimers);
    COPY_FROM_0(gLoopDownTimers);
    COPY_FROM_0(gLoopBoostTimers);
    COPY_FROM_0(gShieldTimer);
    COPY_FROM_0(gHasShield);
    COPY_FROM_0(gChargeTimers);
    COPY_FROM_0(gRightWingFlashTimer);
    COPY_FROM_0(gLeftWingFlashTimer);
    COPY_FROM_0(gRightWingDebrisTimer);
    COPY_FROM_0(gLeftWingDebrisTimer);
    COPY_FROM_0(D_ctx_80177990);
    COPY_FROM_0(D_ctx_801779A8);
    COPY_FROM_0(gMuzzleFlashScale);
    COPY_FROM_0(gShieldAlpha);
    COPY_FROM_0(gLaserStrength);
    COPY_FROM_0(gBombCount);
    COPY_FROM_0(gLifeCount);
#undef COPY_FROM_0
}

static void CoopPlayers_SetOut(s32 i) {
    sSlot[i] = COOP_SLOT_OUT;
    gPlayer[i].draw = false;
}

int32_t CoopPlayers_SkipUpdate(int32_t index) {
    if ((index <= 0) || !CoopPlayers_IsCoop() || (index >= COOP_PLAYERS_MAX)) {
        return 0;
    }
    switch (sSlot[index]) {
        case COOP_SLOT_JOINED:
            // Leave with player 0: once it is no longer flying (level complete, down), this player's section is over.
            if ((gPlayer[0].state != PLAYERSTATE_ACTIVE) && (gPlayer[0].state != PLAYERSTATE_U_TURN)) {
                CoopPlayers_SetOut(index);
                return 1;
            }
            return 0;

        case COOP_SLOT_WAITING:
            if ((gLevelMode == LEVELMODE_ALL_RANGE) && (gPlayer[0].state == PLAYERSTATE_ACTIVE)) {
                CoopPlayers_JoinFromPlayer0(index);
                sSlot[index] = COOP_SLOT_JOINED;
            }
            return 1; // a player that joined this frame starts updating on the next one

        case COOP_SLOT_OUT:
        default:
            return 1;
    }
}

int32_t CoopPlayers_OnPlayerDown(Player* player) {
    s32 i = player->num;

    if ((i <= 0) || (i >= COOP_PLAYERS_MAX) || !CoopPlayers_IsCoop()) {
        return 0;
    }
    CoopPlayers_SetOut(i);
    return 1;
}
