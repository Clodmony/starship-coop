// What every player's game starts a level from (WP-24). See level_start.h; why each value is here: docs/coop/NOTES.md,
// 2026-11-06, #22.

#include "level_start.h"

#include <string.h>

#include "global.h"
#include "port/coop/game/score.h"
#include "port/coop/log/coop_log.h"

// src/sys/sys_math.c
extern s32 sRandSeed1;
extern s32 sRandSeed2;
extern s32 sRandSeed3;
extern s32 sSeededRandSeed1;
extern s32 sSeededRandSeed2;
extern s32 sSeededRandSeed3;

// src/overlays/ovl_menu/fox_map.c
void Map_LevelStart_AudioSpecSetup(LevelId level);

// src/audio/audio_general.c: the audio spec the game last asked for
extern u8 sAudioSpecId;

// src/engine/fox_tank.c: the Landmaster's state outside the Player struct
extern u8 D_800C9F00;
extern u8 D_800C9F04;
extern u8 D_800C9F08;
extern f32 D_800C9F10;
extern s32 D_800C9F14;
extern f32 D_800C9F20;
extern f32 D_800C9F24;
extern f32 D_800C9F28;
extern Vec3f D_tank_800C9F2C;
extern u8 D_800C9F3C;

// src/overlays/ovl_i5/fox_ma.c: Macbeth's rail under the tank
extern f32 D_MA_801BE250[40];

// What a level reads and level init does not set, so what a game brings along from before. The order is the wire order
// (PROTOCOL.md): a change here is a new COOP_PROTOCOL_VERSION.
//
// From gHitCount on: what a campaign builds from level to level (the mission number, the planets flown, their hits,
// medals and team, the total, the map's paths, the clear flags). A level reads little of it, the map and the ending
// read all of it, and games that parted may hold different ones; a level start in the middle of a campaign, which is
// what the session sends when it starts a level again, has to bring them along.
#define PATHS_N 22 // gPlanetPathStatus, src/engine/fox_context.c: the header gives it no size

#define LEVEL_START_TABLE(X, XN)        \
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
    X(gSavedGoldRingCount, u8)          \
    X(gHitCount, s32)                   \
    X(gTotalHits, s32)                  \
    X(gMissionNumber, s32)              \
    X(gMissionPlanet, s32)              \
    X(gMissionTeamStatus, s32)          \
    X(gMissionHitCount, s32)            \
    X(gMissionMedal, s32)               \
    XN(gPlanetPathStatus, s32, PATHS_N) \
    X(gMissionStatus, u8)               \
    X(gLeveLClearStatus, u8)

// Behind those values: one that says whether the level starts at a checkpoint, and the checkpoint. The game saves
// these when a checkpoint is reached and reads them at the level's setup after a death (Player_Setup,
// src/engine/fox_play.c:4697-4735); entering a level clears them. sCheckpointHits is each player's own hits as
// score.c saved them there. gSavedTeamShields, gSavedGoldRingCount and gSavedStarWolfTeamAlive, which a checkpoint
// also saves, are among the values above. All 0 in a level start that begins at the level's start.
static s32 sCheckpointHits[COOP_PLAYERS_MAX];

#define CHECKPOINT_TABLE(X)          \
    X(gSavedObjectLoadIndex, s32)    \
    X(gSavedHitCount, s32)           \
    X(gSavedGroundSurface, s32)      \
    X(gAllRangeCheckpoint, s32)      \
    X(gSavedZoSearchlightStatus, u8) \
    X(gSavedPathProgress, f32)       \
    X(sCheckpointHits, s32)

typedef struct {
    void* data;
    u8 size;  // of one value
    u8 count; // values
} StartRow;

#define START_ROW(var, type) { &(var), sizeof(type), sizeof(var) / sizeof(type) },
#define START_ROW_N(var, type, n) { (var), sizeof(type), (n) },
#define START_COUNT(var, type) +(sizeof(var) / sizeof(type))
#define START_COUNT_N(var, type, n) +(n)

static const StartRow sRows[] = { LEVEL_START_TABLE(START_ROW, START_ROW_N) };

static const StartRow sCheckpointRows[] = { CHECKPOINT_TABLE(START_ROW) };

#define CHECKPOINT_VALUES (0 CHECKPOINT_TABLE(START_COUNT))
// The value that says whether the checkpoint behind it is one.
#define CHECKPOINT_FLAG (COOP_LEVEL_START_VALUES - CHECKPOINT_VALUES - 1)

_Static_assert((0 LEVEL_START_TABLE(START_COUNT, START_COUNT_N)) + 1 + CHECKPOINT_VALUES == COOP_LEVEL_START_VALUES,
               "COOP_LEVEL_START_VALUES does not match the tables");
_Static_assert(sizeof(gSavedPathProgress) == sizeof(s32), "gSavedPathProgress does not travel as its 32 bits");
_Static_assert(sizeof(gMissionPlanet[0]) == sizeof(s32), "gMissionPlanet is not read as it is stored");
_Static_assert(LEVEL_VERSUS == COOP_LEVEL_START_LEVELS, "COOP_LEVEL_START_LEVELS is not the first level left out");
_Static_assert(sizeof(gLaserStrength[0]) == sizeof(s32), "gLaserStrength is not read as it is stored");

static int32_t Row_Get(const StartRow* row, s32 i) {
    switch (row->size) {
        case 1:
            return ((const u8*) row->data)[i];
        case 2:
            return ((const s16*) row->data)[i];
        default: {
            // 32 bits as they are: an s32, or a float's bit pattern (PROTOCOL.md rule 3).
            int32_t value;

            memcpy(&value, (const u8*) row->data + 4 * i, sizeof(value));
            return value;
        }
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
            memcpy((u8*) row->data + 4 * i, &value, sizeof(value));
            break;
    }
}

// newLevel: the hit count of the level is 0, as Map_PlayLevel leaves it for a level the map starts. checkpoint: the
// level starts where this game's checkpoint is.
static void LevelStart_Fill(CoopLevelStart* out, uint8_t level, uint8_t phase, bool newLevel, bool checkpoint) {
    size_t r;
    s32 i;
    s32 n = 0;

    memset(out, 0, sizeof(*out));
    out->level = level;
    out->phase = phase;
    for (r = 0; r < ARRAY_COUNT(sRows); r++) {
        for (i = 0; i < sRows[r].count; i++) {
            out->values[n++] = (newLevel && (sRows[r].data == (void*) &gHitCount)) ? 0 : Row_Get(&sRows[r], i);
        }
    }
    out->values[n++] = checkpoint;
    if (checkpoint) {
        for (i = 0; i < COOP_PLAYERS_MAX; i++) {
            sCheckpointHits[i] = CoopScore_SavedHits(i);
        }
        for (r = 0; r < ARRAY_COUNT(sCheckpointRows); r++) {
            for (i = 0; i < sCheckpointRows[r].count; i++) {
                out->values[n++] = Row_Get(&sCheckpointRows[r], i);
            }
        }
    }
    CoopSave_FromGame(out->save);
}

void CoopLevelStart_FromGame(CoopLevelStart* out, uint8_t level, uint8_t phase) {
    LevelStart_Fill(out, level, phase, true, false);
}

void CoopLevelStart_Entering(CoopLevelStart* out) {
    LevelStart_Fill(out, (uint8_t) gNextLevel, (uint8_t) gNextLevelPhase, false, false);
}

void CoopLevelStart_Entered(CoopLevelStart* out) {
    LevelStart_Fill(out, (uint8_t) gCurrentLevel, (uint8_t) gLevelPhase, false, false);
}

bool CoopLevelStart_AtCheckpoint(CoopLevelStart* out) {
    if ((gGameState != GSTATE_PLAY) || (gNextGameState != GSTATE_NONE) ||
        ((gSavedObjectLoadIndex == 0) && (gAllRangeCheckpoint == 0))) {
        return false;
    }
    LevelStart_Fill(out, (uint8_t) gCurrentLevel, (uint8_t) gLevelPhase, false, true);
    return true;
}

void CoopLevelStart_SetAllRangeCheckpoint(CoopLevelStart* out, int32_t checkpoint) {
    size_t r;
    s32 n = CHECKPOINT_FLAG + 1;

    out->values[CHECKPOINT_FLAG] = 1;
    for (r = 0; r < ARRAY_COUNT(sCheckpointRows); r++) {
        if (sCheckpointRows[r].data == (void*) &gAllRangeCheckpoint) {
            out->values[n] = checkpoint;
            return;
        }
        n += sCheckpointRows[r].count;
    }
}

// A level start that begins at a checkpoint: its checkpoint values wait here from CoopLevelStart_Enter to the next
// frame, as entering the level clears the checkpoint.
static int32_t sCheckpointPending[CHECKPOINT_VALUES];
static bool sCheckpointIsPending = false;

void CoopLevelStart_AfterEntry(void) {
    size_t r;
    s32 i;
    s32 n = 0;

    // Game_SetGameState has taken the level by now: it runs on the frame of CoopLevelStart_Enter, right after the event
    // that called it.
    if (!sCheckpointIsPending || (gNextGameState != GSTATE_NONE)) {
        return;
    }
    sCheckpointIsPending = false;
    for (r = 0; r < ARRAY_COUNT(sCheckpointRows); r++) {
        for (i = 0; i < sCheckpointRows[r].count; i++) {
            Row_Set(&sCheckpointRows[r], i, sCheckpointPending[n++]);
        }
    }
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        CoopScore_SetSavedHits(i, sCheckpointHits[i]);
    }
    // As after the level's first setup, which a game that restarts from a checkpoint is past: with it set, the setup
    // would play the level's intro and clear the all-range checkpoint (src/engine/fox_play.c:4820-4828).
    D_ctx_8017782C = false;
    CoopLog_Write(COOP_LOG_SESSION, false,
                  "level start at the checkpoint: object index %d, path %d, all-range checkpoint %d, hits %d",
                  (int) gSavedObjectLoadIndex, (int) gSavedPathProgress, (int) gAllRangeCheckpoint,
                  (int) gSavedHitCount);
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
    // The Landmaster's state outside the Player struct, and Macbeth's rail under it: no level setup clears them
    // (Macbeth_InitLevel sets 3 of the 40 rail values, src/overlays/ovl_i5/fox_ma.c:378), so a level that starts again
    // went on from where each game's own last frames left them. The games that part stop on different frames, and in
    // Macbeth they parted again 34 frames after a restart at its checkpoint, in gPlayer[0].rot.x (#267).
    D_800C9F00 = D_800C9F04 = D_800C9F08 = D_800C9F3C = 0;
    D_800C9F10 = D_800C9F20 = D_800C9F24 = D_800C9F28 = 0.0f;
    D_800C9F14 = 0;
    D_tank_800C9F2C.x = D_tank_800C9F2C.y = D_tank_800C9F2C.z = 0.0f;
    memset(D_MA_801BE250, 0, sizeof(D_MA_801BE250));
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
    // The checkpoint, if the level starts at one, goes in place on the next frame (CoopLevelStart_AfterEntry).
    sCheckpointIsPending = start->values[CHECKPOINT_FLAG] != 0;
    memcpy(sCheckpointPending, &start->values[CHECKPOINT_FLAG + 1], sizeof(sCheckpointPending));
    LevelStart_ClearSetupValues();
    CoopPlayers_SetRoster(start->roster);
    gMainController = 0; // team-wide input is player 0's slot on every client, whichever port this PC's title found

    // The way the game itself changes level from anywhere (src/overlays/ovl_i3/fox_a6.c:2927): Game_SetGameState clears
    // the objects, sets up the level and lands in PLAY_STANDBY on this frame, so both ends of a session go the same way
    // whether they came from the map or the title screen. What Map_PlayLevel does on top of that is the hit count,
    // which is one of the values: 0 from CoopLevelStart_FromGame.
    gNextLevel = start->level;
    gNextLevelPhase = start->phase;
    gNextGameState = GSTATE_PLAY;
    // As from the map, whose spec no level uses, so the level's spec always resets the audio heap. A spec that is the
    // one already loaded resets nothing, and then the voice sequence runs on with the tempo where this game's own
    // history left it, so a radio line ends one frame apart on two games (#230). Going into Macbeth from the boot logo
    // or from Macbeth, for example: both use AUDIOSPEC_MA. After the reset the voice player starts over a fixed number
    // of frames after this call. A game that is in the level's scene already gets to Play_Init 2 frames sooner
    // (Game_ChangeScene loads no scene), so it is in step with the others only if they come from that scene too.
    sAudioSpecId = AUDIOSPEC_24;
    Map_LevelStart_AudioSpecSetup((LevelId) start->level);
}
