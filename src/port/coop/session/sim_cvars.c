// The table of simulation CVars. Why each one is here, with file and line: docs/coop/NOTES.md, WP-21.

#include "sim_cvars.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char* name;
    const char* label;
    int32_t defaultValue; // the default the game code passes, so an unset CVar reads the same as in the game
} SimCvar;

static const SimCvar sCvars[] = {
    // Cheats and enhancements that change the game.
    { "gInvincible", "Invincible", 0 },
    { "gUnbreakableWings", "Unbreakable wings", 0 },
    { "gRapidFire", "Rapid fire", 0 },
    { "gHyperLaser", "Hyper laser", 0 },
    { "gLaserRangeMult", "Laser range", 100 },
    { "gInfiniteLives", "Infinite lives", 0 },
    { "gInfiniteBombs", "Infinite bombs", 0 },
    { "gInfiniteBoost", "Infinite boost", 0 },
    { "gLtoCharge", "Charge shot on L", 0 },
    { "gInvertYAxis", "Invert Y axis", 0 },
    { "gForceExpertMode", "Expert mode", 0 },
    { "gLevelSelector", "Level select", 0 },
    { "gSfxJukebox", "Sound test", 0 },
    { "gScoreEditor", "Score editor", 0 },
    { "gScoreEditValue", "Score editor value", 1 },
    { "gSpawnerMod", "Object spawner", 0 },
    // Restored beta features and bug fixes.
    { "gRestoreBetaBombExplosion", "Beta bomb explosion", 0 },
    { "gRestoreBetaBoostGauge", "Beta boost gauge", 0 },
    { "gRestoreBetaCoin", "Beta coin", 0 },
    { "gSzMissileBug", "Sector Z missile fix", 0 },
    { "gSzActorFix", "Sector Z actor fix", 0 },
    { "gMaCameraFix", "Macbeth camera fix", 0 },
    { "gTeamFaces", "Team faces", 1 },
    { "gDisableLOD", "Disable LOD", 1 },
    { "gDisableGorgonFlash", "Disable Gorgon flash", 0 },
    { "gEnemyRedRadio", "Red radio for enemies", 0 },
    { "gVoiceLanguage", "Voice language", 0 },
    // Hit64 mod.
    { "gHit64FalcoDead", "Hit64: Falco down", 0 },
    { "gHit64SlippyDead", "Hit64: Slippy down", 0 },
    { "gHit64PeppyDead", "Hit64: Peppy down", 0 },
    { "gHit64SelfDestruct", "Hit64: self destruct", 0 },
    // Developer tools.
    { "gDebugSpeedControl", "Debug: speed control", 0 },
    { "gDebugJumpToMap", "Debug: jump to map", 0 },
    { "gDebugWarpZone", "Debug: warp zone", 0 },
    { "gDebugNoCollision", "Debug: no collision", 0 },
    { "gDebugLevelComplete", "Debug: level complete", 0 },
    { "gDebugJumpToAllRange", "Debug: jump to all-range", 0 },
    { "gDebugEnding", "Debug: ending", 0 },
    { "gDebugPause", "Debug: pause", 0 },
    { "gLToDebugPause", "Debug: pause on L", 0 },
    { "gLToFrameAdvance", "Debug: frame advance on L", 0 },
};

#define FIXED_COUNT (sizeof(sCvars) / sizeof(sCvars[0]))

// Checkpoints are stored per level (src/engine/fox_play.c, gCheckpoint.%d.*). Levels 0 to 19 are the campaign and
// training; 20 is versus.
#define CHECKPOINT_LEVELS 20
#define CHECKPOINT_FIELDS 4

static const char* const sCheckpointFields[CHECKPOINT_FIELDS] = {
    "Set",
    "gSavedGroundSurface",
    "gSavedPathProgress",
    "gSavedObjectLoadIndex",
};

uint16_t CoopSimCvars_Count(void) {
    return (uint16_t) (FIXED_COUNT + CHECKPOINT_LEVELS * CHECKPOINT_FIELDS);
}

bool CoopSimCvars_Name(uint16_t i, char* out, size_t cap) {
    uint16_t c;

    if (i < FIXED_COUNT) {
        snprintf(out, cap, "%s", sCvars[i].name);
        return true;
    }
    if (i >= CoopSimCvars_Count()) {
        return false;
    }
    c = (uint16_t) (i - FIXED_COUNT);
    snprintf(out, cap, "gCheckpoint.%d.%s", c / CHECKPOINT_FIELDS, sCheckpointFields[c % CHECKPOINT_FIELDS]);
    return true;
}

const char* CoopSimCvars_Label(uint16_t i) {
    if (i < FIXED_COUNT) {
        return sCvars[i].label;
    }
    return "Checkpoint";
}

uint32_t CoopSimCvars_TableHash(void) {
    uint32_t hash = 2166136261u;
    char name[64];
    uint16_t i;

    for (i = 0; i < CoopSimCvars_Count(); i++) {
        const char* p = name;

        CoopSimCvars_Name(i, name, sizeof(name));
        // The terminating zero goes in too, so "ab"+"c" and "a"+"bc" differ.
        do {
            hash = (hash ^ (uint8_t) *p) * 16777619u;
        } while (*p++ != '\0');
    }
    return hash;
}

void CoopSimCvars_Read(int32_t* values, CoopCvarGetIntFn getInt, CoopCvarGetFloatFn getFloat) {
    char name[64];
    uint16_t i;
    int level;

    for (i = 0; i < FIXED_COUNT; i++) {
        values[i] = getInt(sCvars[i].name, sCvars[i].defaultValue);
    }
    for (level = 0; level < CHECKPOINT_LEVELS; level++) {
        int32_t* v = &values[FIXED_COUNT + level * CHECKPOINT_FIELDS];
        float progress;

        snprintf(name, sizeof(name), "gCheckpoint.%d.Set", level);
        v[0] = getInt(name, 0);
        if (v[0] == 0) {
            v[1] = v[2] = v[3] = 0;
            continue;
        }
        snprintf(name, sizeof(name), "gCheckpoint.%d.gSavedGroundSurface", level);
        v[1] = getInt(name, 0);
        snprintf(name, sizeof(name), "gCheckpoint.%d.gSavedPathProgress", level);
        progress = getFloat(name, 0.0f);
        memcpy(&v[2], &progress, sizeof(progress));
        snprintf(name, sizeof(name), "gCheckpoint.%d.gSavedObjectLoadIndex", level);
        v[3] = getInt(name, 0);
    }
}
