// State hash per game frame (WP-04). See state_hash.h for how to switch the log on.
//
// The hash is FNV-1a (64 bit) over the raw bytes of the simulation state, one hash per region plus a total over all
// regions. Regions: the players (all of them: CoopPlayers_Count(), gCamCount stays 1 in co-op), the fixed object arrays
// (src/engine/fox_context.c), the all-range scenery buffer, the RNG seeds (src/sys/sys_math.c) and a list of key
// globals.
//
// Pointers differ from run to run (address space layout randomisation) and from build to build, so they are never
// hashed as they are:
// - ObjectInfo.draw/dList, .action and .hitbox are cleared. They follow from the object id, which is hashed.
// - Effect.unk_74 (a display list) and Player.sfx.srcPos/srcVel (sound sources) are cleared.
// - Actor.iwork[] holds plain numbers and, in some actors, pointers to other objects (for example
//   src/overlays/ovl_i5/fox_ti.c:694-697). A value that points into one of the hashed arrays is replaced by
//   (array, byte offset); any other value above 32 bits is replaced by one fixed marker, except in co-op, where
//   negative numbers stay what they are.
// Padding bytes are hashed too. The game clears every object byte by byte before use (Actor_Initialize and friends
// in src/engine/fox_enmy.c:156-210, Player_InitializeAll in src/engine/fox_play.c:2605), so padding is 0 unless
// the game wrote it.
//
// Co-op hashes two things more strictly than single player: negative numbers in Actor.iwork[] (NormalizeWord) and free
// gScenery360 slots (HashScenery360). With co-op off the hash stays as it was, so a single-player log still lines up
// with one from an older build (docs/coop/QA.md, test plan item 1).
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
#include "port/coop/game/players.h"

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
static s32 sPokePlayer = -1;
static s64 sProbePeriod = 0;

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
    return (gGameState == GSTATE_PLAY) && (gPlayer != NULL) && (CoopPlayers_Count() >= 1) &&
           (CoopPlayers_Count() <= COOP_PLAYERS_MAX);
}

static bool Scenery360Valid(void) {
    return (gGameState == GSTATE_PLAY) && (gLevelMode == LEVELMODE_ALL_RANGE) && (gScenery360 != NULL);
}

static void SetRange(s32 index, const void* base, size_t size) {
    sRanges[index].base = (uintptr_t) base;
    sRanges[index].size = (base != NULL) ? size : 0;
}

static void UpdateRanges(void) {
    SetRange(RANGE_PLAYERS, PlayersValid() ? gPlayer : NULL, CoopPlayers_Count() * sizeof(Player));
    SetRange(RANGE_ACTORS, gActors, sizeof(gActors));
    SetRange(RANGE_BOSSES, gBosses, sizeof(gBosses));
    SetRange(RANGE_EFFECTS, gEffects, sizeof(gEffects));
    SetRange(RANGE_ITEMS, gItems, sizeof(gItems));
    SetRange(RANGE_SCENERY, gScenery, sizeof(gScenery));
    SetRange(RANGE_SPRITES, gSprites, sizeof(gSprites));
    SetRange(RANGE_SCENERY360, Scenery360Valid() ? gScenery360 : NULL, SCENERY360_COUNT * sizeof(Scenery360));
    SetRange(RANGE_SHOTS, gPlayerShots, sizeof(gPlayerShots));
}

// A pointer into a hashed array becomes (array, offset); any other value too large for 32 bits becomes a marker. In
// co-op a negative number stays: the game's s32 values sign-extend into the word, so its top half is all ones, and no
// user-space pointer looks like that.
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
        if (CoopPlayers_IsCoop() && (((uint64_t) value >> 32) == 0xFFFFFFFFULL)) {
            return (uint64_t) value;
        }
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
    for (i = 0; i < CoopPlayers_Count(); i++) {
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

// gScenery360 comes from Memory_Allocate, which does not clear it, and the game only sets the status of each slot. So
// a free slot keeps what the memory held before (all 133 free slots in Corneria's all-range section were not zero). In
// co-op it counts as empty.
static void HashScenery360(uint64_t* h) {
    Scenery360 copy;
    s32 i;

    for (i = 0; i < SCENERY360_COUNT; i++) {
        copy = gScenery360[i];
        ClearInfoPointers(&copy.info);
        if (CoopPlayers_IsCoop() && (copy.obj.status == OBJ_FREE)) {
            memset(&copy, 0, sizeof(copy));
        }
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
        HashScenery360(&r[COOP_HASH_SCENERY360]);
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

// Test only: flip the lowest bit of the first byte of one value. By default that is the last actor's fwork[29]: the
// last actor slot is rarely in use, so the change usually stays in memory and the hash stays different from that frame
// on. With SF64_COOP_HASH_POKE_PLAYER it is that player's pos.x, a difference the game itself then goes on with.
static void Poke(void) {
    char target[32];
    u8* p;

    if (sPokePlayer < 0) {
        p = (u8*) &gActors[ARRAY_COUNT(gActors) - 1].fwork[29];
        snprintf(target, sizeof(target), "gActors[%d].fwork[29]", (s32) ARRAY_COUNT(gActors) - 1);
    } else if ((gGameState == GSTATE_PLAY) && (gPlayer != NULL) && (sPokePlayer < CoopPlayers_Count())) {
        p = (u8*) &gPlayer[sPokePlayer].pos.x;
        snprintf(target, sizeof(target), "gPlayer[%d].pos.x", sPokePlayer);
    } else {
        return; // no such player on this frame: nothing changes, and hash_check.py reports that
    }
    *p ^= 1;
    if (sLog != NULL) {
        fprintf(sLog, "# poke frame=%u target=%s byte=0 bit=0\n", sFrame, target);
    }
}

// Test only: log what one change did to the hash. Both hashes come from the same frame of the same process, so this
// needs no second run to compare with, and it says nothing about determinism.
static void ProbeReport(const char* target, s32 parked, const CoopStateHash* a, const CoopStateHash* b) {
    s32 count = 0;
    s32 i;

    fprintf(sLog, "# probe frame=%u target=%s parked=%d changed=%d regions=", sFrame, target, parked,
            a->total != b->total);
    for (i = 0; i < COOP_HASH_REGION_COUNT; i++) {
        if (a->region[i] != b->region[i]) {
            fprintf(sLog, "%s%s", (count++ == 0) ? "" : ",", sRegionNames[i]);
        }
    }
    fprintf(sLog, "%s\n", (count == 0) ? "none" : "");
}

// Hash, flip the lowest bit of one byte, hash again and flip it back, so the game goes on as if nothing happened.
static void ProbeBit(const char* target, s32 parked, void* where) {
    CoopStateHash before;
    CoopStateHash after;

    CoopStateHash_Compute(&before);
    *(u8*) where ^= 1;
    CoopStateHash_Compute(&after);
    *(u8*) where ^= 1;
    ProbeReport(target, parked, &before, &after);
}

// Hash one word of the last actor at two values, then put the word back.
static void ProbeIwork(const char* target, uintptr_t first, uintptr_t second) {
    uintptr_t* word = &gActors[ARRAY_COUNT(gActors) - 1].iwork[0];
    uintptr_t saved = *word;
    CoopStateHash a;
    CoopStateHash b;

    *word = first;
    CoopStateHash_Compute(&a);
    *word = second;
    CoopStateHash_Compute(&b);
    *word = saved;
    ProbeReport(target, 0, &a, &b);
}

static void ProbeScenery360(const char* target, u8 status) {
    s32 i;

    for (i = 0; i < SCENERY360_COUNT; i++) {
        if (gScenery360[i].obj.status == status) {
            ProbeBit(target, 0, &gScenery360[i].obj.pos.x);
            return;
        }
    }
}

// Test only: what tools/coop/hash_check.py probe reads. Which of these must change the hash is written there.
static void Probe(void) {
    char target[16];
    s32 i;

    if ((gGameState != GSTATE_PLAY) || (gPlayer == NULL)) {
        return; // the player count is fixed by the first level, and co-op does not start before it
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        snprintf(target, sizeof(target), "player%d", i);
        ProbeBit(target, CoopPlayers_IsParked(i), &gPlayer[i].pos.x);
    }
    ProbeIwork("iwork-negative", (uintptr_t) -1, (uintptr_t) -2);
    ProbeIwork("iwork-foreign", (uintptr_t) &sFrame, (uintptr_t) &sProbePeriod); // two addresses outside the arrays
    if (Scenery360Valid()) {
        ProbeScenery360("scenery360-active", OBJ_ACTIVE);
        ProbeScenery360("scenery360-free", OBJ_FREE);
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
    if ((sProbePeriod > 0) && (((s64) sFrame % sProbePeriod) == 0)) {
        Probe();
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
    const char* pokePlayer = getenv("SF64_COOP_HASH_POKE_PLAYER");
    const char* probe = getenv("SF64_COOP_HASH_PROBE");
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
    if ((pokePlayer != NULL) && (pokePlayer[0] != '\0')) {
        sPokePlayer = strtol(pokePlayer, NULL, 10);
    }
    if ((probe != NULL) && (probe[0] != '\0')) {
        sProbePeriod = strtoll(probe, NULL, 10);
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
