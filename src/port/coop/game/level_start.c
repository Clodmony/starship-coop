// What every player's game starts a level from (WP-24). See level_start.h; why each value is here: docs/coop/NOTES.md,
// 2026-11-06, #22.

#include "level_start.h"

#include <string.h>

#include "global.h"

// src/sys/sys_math.c
extern s32 sRandSeed1;
extern s32 sRandSeed2;
extern s32 sRandSeed3;
extern s32 sSeededRandSeed1;
extern s32 sSeededRandSeed2;
extern s32 sSeededRandSeed3;

// src/overlays/ovl_menu/fox_map.c
void Map_LevelStart_AudioSpecSetup(LevelId level);

// What a level reads and level init does not set, so what a game brings along from before. The order is the wire order
// (PROTOCOL.md): a change here is a new COOP_PROTOCOL_VERSION.
#define LEVEL_START_TABLE(X)            \
    X(sRandSeed1, s32)                  \
    X(sRandSeed2, s32)                  \
    X(sRandSeed3, s32)                  \
    X(sSeededRandSeed1, s32)            \
    X(sSeededRandSeed2, s32)            \
    X(sSeededRandSeed3, s32)            \
    X(gExpertMode, u8)                  \
    X(gClearPlayerInfo, s32)            \
    X(gGreatFoxIntact, u8)              \
    X(gVenomHardClear, u8)              \
    X(gRadioMsgId, s32)                 \
    X(gTeamShields, s32)                \
    X(gSavedTeamShields, s32)           \
    X(gPrevPlanetTeamShields, s32)      \
    X(gPrevPlanetSavedTeamShields, s32) \
    X(D_ctx_80177C58, s32)              \
    X(gTeamDamage, s32)                 \
    X(gTeamEventActorIndex, s32)        \
    X(D_enmy_Timer_80161670, s32)       \
    X(gStarWolfTeamAlive, s32)          \
    X(gSavedStarWolfTeamAlive, s32)     \
    X(gLifeCount, s16)                  \
    X(gBombCount, s32)                  \
    X(gLaserStrength, s32)              \
    X(gGoldRingCount, u8)               \
    X(gSavedGoldRingCount, u8)

typedef struct {
    void* data;
    u8 size;  // of one value
    u8 count; // values
} StartRow;

#define START_ROW(var, type) { &(var), sizeof(type), sizeof(var) / sizeof(type) },
#define START_COUNT(var, type) +(sizeof(var) / sizeof(type))

static const StartRow sRows[] = { LEVEL_START_TABLE(START_ROW) };

_Static_assert((0 LEVEL_START_TABLE(START_COUNT)) == COOP_LEVEL_START_VALUES,
               "COOP_LEVEL_START_VALUES does not match the table");
_Static_assert(LEVEL_VERSUS == COOP_LEVEL_START_LEVELS, "COOP_LEVEL_START_LEVELS is not the first level left out");
_Static_assert(sizeof(gLaserStrength[0]) == sizeof(s32), "gLaserStrength is not read as it is stored");

static int32_t Row_Get(const StartRow* row, s32 i) {
    switch (row->size) {
        case 1:
            return ((const u8*) row->data)[i];
        case 2:
            return ((const s16*) row->data)[i];
        default:
            return ((const s32*) row->data)[i];
    }
}

static void Row_Set(const StartRow* row, s32 i, int32_t value) {
    switch (row->size) {
        case 1:
            ((u8*) row->data)[i] = (u8) value;
            break;
        case 2:
            ((s16*) row->data)[i] = (s16) value;
            break;
        default:
            ((s32*) row->data)[i] = value;
            break;
    }
}

void CoopLevelStart_FromGame(CoopLevelStart* out, uint8_t level, uint8_t phase) {
    size_t r;
    s32 i;
    s32 n = 0;

    memset(out, 0, sizeof(*out));
    out->level = level;
    out->phase = phase;
    for (r = 0; r < ARRAY_COUNT(sRows); r++) {
        for (i = 0; i < sRows[r].count; i++) {
            out->values[n++] = Row_Get(&sRows[r], i);
        }
    }
}

// What a level sets up on its first frame and the state hash already reads before that frame: Player_Setup
// (src/engine/fox_play.c:4574) sets the per-player values and gObjectLoadIndex, the path update gPathVelZ. Until then
// they hold whatever the last level this process played left, so two games whose histories differ hash apart at frame
// 0, though nothing reads them before they are set (#187). Every game clears them as a fresh process holds them.
static void LevelStart_ClearSetupValues(void) {
    memset(gLeftWingHealth, 0, sizeof(gLeftWingHealth));
    memset(gRightWingHealth, 0, sizeof(gRightWingHealth));
    memset(gChargeTimers, 0, sizeof(gChargeTimers));
    memset(gUturnDownTimers, 0, sizeof(gUturnDownTimers));
    memset(gUturnBrakeTimers, 0, sizeof(gUturnBrakeTimers));
    memset(gLoopDownTimers, 0, sizeof(gLoopDownTimers));
    memset(gLoopBoostTimers, 0, sizeof(gLoopBoostTimers));
    memset(gLeftWingDebrisTimer, 0, sizeof(gLeftWingDebrisTimer));
    memset(gRightWingDebrisTimer, 0, sizeof(gRightWingDebrisTimer));
    memset(gHasShield, 0, sizeof(gHasShield));
    memset(gShieldTimer, 0, sizeof(gShieldTimer));
    memset(gShieldAlpha, 0, sizeof(gShieldAlpha));
    gObjectLoadIndex = 0;
    gPathVelZ = 0.0f;
}

void CoopLevelStart_Enter(const CoopLevelStart* start) {
    size_t r;
    s32 i;
    s32 n = 0;

    for (r = 0; r < ARRAY_COUNT(sRows); r++) {
        for (i = 0; i < sRows[r].count; i++) {
            Row_Set(&sRows[r], i, start->values[n++]);
        }
    }
    LevelStart_ClearSetupValues();
    CoopPlayers_SetRoster(start->roster);
    gMainController = 0; // team-wide input is player 0's slot on every client, whichever port this PC's title found

    // The way the game itself changes level from anywhere (src/overlays/ovl_i3/fox_a6.c:2927): Game_SetGameState clears
    // the objects, sets up the level and lands in PLAY_STANDBY on this frame, so both ends of a session go the same way
    // whether they came from the map or the title screen. What Map_PlayLevel does on top of that is the hit count.
    gHitCount = 0;
    gNextLevel = start->level;
    gNextLevelPhase = start->phase;
    gNextGameState = GSTATE_PLAY;
    Map_LevelStart_AudioSpecSetup((LevelId) start->level);
}
