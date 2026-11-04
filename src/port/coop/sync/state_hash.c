// State hash per game frame (WP-04). See state_hash.h for how to switch the log on.
//
// The hash is FNV-1a (64 bit) over the raw bytes of the simulation state, one hash per region plus a total over all
// regions. Regions: the players, the fixed object arrays (src/engine/fox_context.c), the all-range scenery buffer,
// the RNG seeds (src/sys/sys_math.c) and a list of key globals.
//
// Pointers differ from run to run (address space layout randomisation) and from build to build, so they are never
// hashed as they are:
// - ObjectInfo.draw/dList, .action and .hitbox are cleared. They follow from the object id, which is hashed.
// - Effect.unk_74 (a display list) and Player.sfx.srcPos/srcVel (sound sources) are cleared.
// - Actor.iwork[] holds plain numbers and, in some actors, pointers to other objects (for example
//   src/overlays/ovl_i5/fox_ti.c:694-697). A value that points into one of the hashed arrays is replaced by
//   (array, byte offset); any other value above 32 bits is replaced by one fixed marker.
// Padding bytes are hashed too. The game clears every object byte by byte before use (Actor_Initialize and friends
// in src/engine/fox_enmy.c:156-210, Player_InitializeAll in src/engine/fox_play.c:2605), so padding is 0 unless
// the game wrote it.
//
// The listener runs on GamePreUpdateEvent, before any game logic of the frame, so the hash of frame N is the state
// after frame N-1. It runs after the input replay listener (EVENT_PRIORITY_LOW runs first, see
// src/port/hooks/impl/EventSystem.cpp:27-29), so on frame 0 it sees the seeds the replay restored.

#include "state_hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "port/hooks/Events.h"

// src/sys/sys_math.c
extern s32 sRandSeed1;
extern s32 sRandSeed2;
extern s32 sRandSeed3;
extern s32 sSeededRandSeed1;
extern s32 sSeededRandSeed2;
extern s32 sSeededRandSeed3;

#define FNV_OFFSET 0xCBF29CE484222325ULL
#define FNV_PRIME 0x100000001B3ULL
#define FOREIGN_POINTER 0xFFFFFFFFFFFFFFFEULL
#define SCENERY360_COUNT 200 // MEM_ARRAY_ALLOCATE(gScenery360, 200), src/engine/fox_play.c:2958

static const char* sRegionNames[COOP_HASH_REGION_COUNT] = {
    "players", "actors",     "bosses", "effects", "items", "scenery",
    "sprites", "scenery360", "shots",  "misc",    "rng",   "globals",
};

typedef struct {
    uintptr_t base;
    size_t size;
} HashRange;

enum {
    RANGE_PLAYERS,
    RANGE_ACTORS,
    RANGE_BOSSES,
    RANGE_EFFECTS,
    RANGE_ITEMS,
    RANGE_SCENERY,
    RANGE_SPRITES,
    RANGE_SCENERY360,
    RANGE_SHOTS,
    RANGE_COUNT,
};

static HashRange sRanges[RANGE_COUNT];

static FILE* sLog = NULL;
static bool sStarted = false;
static u32 sFrame = 0;
static s64 sPokeFrame = -1;

static void Fnv(uint64_t* h, const void* data, size_t size) {
    const u8* p = (const u8*) data;
    size_t i;

    for (i = 0; i < size; i++) {
        *h ^= p[i];
        *h *= FNV_PRIME;
    }
}

#define HASH_VAR(h, var) Fnv((h), &(var), sizeof(var))

static bool PlayersValid(void) {
    return (gGameState == GSTATE_PLAY) && (gPlayer != NULL) && (gCamCount >= 1) && (gCamCount <= 4);
}

static bool Scenery360Valid(void) {
    return (gGameState == GSTATE_PLAY) && (gLevelMode == LEVELMODE_ALL_RANGE) && (gScenery360 != NULL);
}

static void SetRange(s32 index, const void* base, size_t size) {
    sRanges[index].base = (uintptr_t) base;
    sRanges[index].size = (base != NULL) ? size : 0;
}

static void UpdateRanges(void) {
    SetRange(RANGE_PLAYERS, PlayersValid() ? gPlayer : NULL, gCamCount * sizeof(Player));
    SetRange(RANGE_ACTORS, gActors, sizeof(gActors));
    SetRange(RANGE_BOSSES, gBosses, sizeof(gBosses));
    SetRange(RANGE_EFFECTS, gEffects, sizeof(gEffects));
    SetRange(RANGE_ITEMS, gItems, sizeof(gItems));
    SetRange(RANGE_SCENERY, gScenery, sizeof(gScenery));
    SetRange(RANGE_SPRITES, gSprites, sizeof(gSprites));
    SetRange(RANGE_SCENERY360, Scenery360Valid() ? gScenery360 : NULL, SCENERY360_COUNT * sizeof(Scenery360));
    SetRange(RANGE_SHOTS, gPlayerShots, sizeof(gPlayerShots));
}

// A pointer into a hashed array becomes (array, offset); any other value too large for 32 bits becomes a marker.
static uint64_t NormalizeWord(uintptr_t value) {
    s32 i;

    if (value == 0) {
        return 0;
    }
    for (i = 0; i < RANGE_COUNT; i++) {
        if ((sRanges[i].size != 0) && (value >= sRanges[i].base) && (value < sRanges[i].base + sRanges[i].size)) {
            return ((uint64_t) (i + 1) << 56) | (uint64_t) (value - sRanges[i].base);
        }
    }
    if ((uint64_t) value > 0xFFFFFFFFULL) {
        return FOREIGN_POINTER;
    }
    return (uint64_t) value;
}

static void ClearInfoPointers(ObjectInfo* info) {
    info->draw = NULL;
    info->action = NULL;
    info->hitbox = NULL;
}

static void HashPlayers(uint64_t* h) {
    Player copy;
    s32 i;

    if (!PlayersValid()) {
        return;
    }
    for (i = 0; i < gCamCount; i++) {
        copy = gPlayer[i];
        copy.sfx.srcPos = NULL;
        copy.sfx.srcVel = NULL;
        HASH_VAR(h, copy);
    }
}

static void HashActors(uint64_t* h) {
    Actor copy;
    s32 i;
    s32 k;

    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        copy = gActors[i];
        ClearInfoPointers(&copy.info);
        for (k = 0; k < ARRAY_COUNT(copy.iwork); k++) {
            copy.iwork[k] = (uintptr_t) NormalizeWord(copy.iwork[k]);
        }
        HASH_VAR(h, copy);
    }
}

// Objects whose only pointers are in ObjectInfo.
#define HASH_INFO_ARRAY(h, Type, array, count) \
    do {                                       \
        Type copy_;                            \
        s32 i_;                                \
        for (i_ = 0; i_ < (count); i_++) {     \
            copy_ = (array)[i_];               \
            ClearInfoPointers(&copy_.info);    \
            HASH_VAR((h), copy_);              \
        }                                      \
    } while (0)

static void HashEffects(uint64_t* h) {
    Effect copy;
    s32 i;

    for (i = 0; i < ARRAY_COUNT(gEffects); i++) {
        copy = gEffects[i];
        ClearInfoPointers(&copy.info);
        copy.unk_74 = NULL;
        HASH_VAR(h, copy);
    }
}

static void HashGlobals(uint64_t* h) {
    HASH_VAR(h, gGameState);
    HASH_VAR(h, gNextGameStateTimer);
    HASH_VAR(h, gPlayState);
    HASH_VAR(h, gLevelMode);
    HASH_VAR(h, gCamCount);
    HASH_VAR(h, gCurrentLevel);
    HASH_VAR(h, gLevelPhase);
    HASH_VAR(h, gLevelType);
    HASH_VAR(h, gMissionStatus);
    HASH_VAR(h, gBossActive);
    HASH_VAR(h, gBossHealthBar);
    HASH_VAR(h, gGameFrameCount);
    HASH_VAR(h, gCsFrameCount);
    HASH_VAR(h, gObjectLoadIndex);
    HASH_VAR(h, gPathProgress);
    HASH_VAR(h, gPathVelZ);
    HASH_VAR(h, gGroundHeight);
    HASH_VAR(h, gHitCount);
    HASH_VAR(h, gRingPassCount);
    HASH_VAR(h, gTeamShields);
    HASH_VAR(h, gLifeCount);
    HASH_VAR(h, gBombCount);
    HASH_VAR(h, gGoldRingCount);
    HASH_VAR(h, gPlayerScores);
    HASH_VAR(h, gRadioState);
    HASH_VAR(h, gRadioMsgId);
}

void CoopStateHash_Compute(CoopStateHash* out) {
    uint64_t* r = out->region;
    s32 i;

    UpdateRanges();
    for (i = 0; i < COOP_HASH_REGION_COUNT; i++) {
        r[i] = FNV_OFFSET;
    }

    HashPlayers(&r[COOP_HASH_PLAYERS]);
    HashActors(&r[COOP_HASH_ACTORS]);
    HASH_INFO_ARRAY(&r[COOP_HASH_BOSSES], Boss, gBosses, ARRAY_COUNT(gBosses));
    HashEffects(&r[COOP_HASH_EFFECTS]);
    HASH_INFO_ARRAY(&r[COOP_HASH_ITEMS], Item, gItems, ARRAY_COUNT(gItems));
    HASH_INFO_ARRAY(&r[COOP_HASH_SCENERY], Scenery, gScenery, ARRAY_COUNT(gScenery));
    HASH_INFO_ARRAY(&r[COOP_HASH_SPRITES], Sprite, gSprites, ARRAY_COUNT(gSprites));
    if (Scenery360Valid()) {
        HASH_INFO_ARRAY(&r[COOP_HASH_SCENERY360], Scenery360, gScenery360, SCENERY360_COUNT);
    }
    HASH_VAR(&r[COOP_HASH_SHOTS], gPlayerShots);
    HASH_VAR(&r[COOP_HASH_MISC], gTexturedLines);
    HASH_VAR(&r[COOP_HASH_MISC], gBonusText);
    HASH_VAR(&r[COOP_HASH_MISC], gRadarMarks);

    HASH_VAR(&r[COOP_HASH_RNG], sRandSeed1);
    HASH_VAR(&r[COOP_HASH_RNG], sRandSeed2);
    HASH_VAR(&r[COOP_HASH_RNG], sRandSeed3);
    HASH_VAR(&r[COOP_HASH_RNG], sSeededRandSeed1);
    HASH_VAR(&r[COOP_HASH_RNG], sSeededRandSeed2);
    HASH_VAR(&r[COOP_HASH_RNG], sSeededRandSeed3);

    HashGlobals(&r[COOP_HASH_GLOBALS]);

    out->total = FNV_OFFSET;
    Fnv(&out->total, r, sizeof(out->region));
}

const char* CoopStateHash_RegionName(CoopHashRegion region) {
    if ((region < 0) || (region >= COOP_HASH_REGION_COUNT)) {
        return "?";
    }
    return sRegionNames[region];
}

// Test only: flip the lowest bit of the first byte of the last actor's fwork[29]. The last actor slot is rarely in
// use, so the change usually stays in memory and the hash stays different from that frame on.
static void Poke(void) {
    u8* p = (u8*) &gActors[ARRAY_COUNT(gActors) - 1].fwork[29];

    *p ^= 1;
    if (sLog != NULL) {
        fprintf(sLog, "# poke frame=%u target=gActors[%d].fwork[29] byte=0 bit=0\n", sFrame,
                (s32) ARRAY_COUNT(gActors) - 1);
    }
}

static void CoopStateHash_OnGamePreUpdate(IEvent* event) {
    CoopStateHash hash;
    s32 i;

    (void) event;
    if (sLog == NULL) {
        return;
    }
    // Same frame 0 as the input replay: the boot logo runs on wall-clock time (replay.c).
    if (!sStarted) {
        if (gGameState != GSTATE_START) {
            return;
        }
        sStarted = true;
    }

    if ((sPokeFrame >= 0) && ((s64) sFrame == sPokeFrame)) {
        Poke();
    }

    CoopStateHash_Compute(&hash);
    fprintf(sLog, "%u %d %d %016llx", sFrame, (s32) gGameState, (s32) gCurrentLevel, (unsigned long long) hash.total);
    for (i = 0; i < COOP_HASH_REGION_COUNT; i++) {
        fprintf(sLog, " %016llx", (unsigned long long) hash.region[i]);
    }
    fputc('\n', sLog);
    // Every frame: the test tools stop the game as soon as the replay report exists, which on Windows ends the
    // process without running atexit, so anything still buffered would be lost (seen in WP-02).
    fflush(sLog);
    sFrame++;
}

static void CoopStateHash_OnExit(void) {
    if (sLog != NULL) {
        fprintf(sLog, "# end frames=%u\n", sFrame);
        fclose(sLog);
        sLog = NULL;
    }
}

void CoopStateHash_Init(void) {
    const char* logPath = getenv("SF64_COOP_HASH_LOG");
    const char* poke = getenv("SF64_COOP_HASH_POKE");
    s32 i;

    if ((logPath == NULL) || (logPath[0] == '\0')) {
        return;
    }
    sLog = fopen(logPath, "w");
    if (sLog == NULL) {
        return;
    }
    if ((poke != NULL) && (poke[0] != '\0')) {
        sPokeFrame = strtoll(poke, NULL, 10);
    }

    fprintf(sLog, "# starship-coop state hash v1 (src/port/coop/sync/state_hash.c)\n");
    fprintf(sLog, "# frame gamestate level total");
    for (i = 0; i < COOP_HASH_REGION_COUNT; i++) {
        fprintf(sLog, " %s", sRegionNames[i]);
    }
    fputc('\n', sLog);

    REGISTER_LISTENER(GamePreUpdateEvent, CoopStateHash_OnGamePreUpdate, EVENT_PRIORITY_NORMAL);
    atexit(CoopStateHash_OnExit);
}
