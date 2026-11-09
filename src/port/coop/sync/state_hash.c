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
// Co-op hashes differently from single player: negative numbers in Actor.iwork[] (NormalizeWord), free gScenery360
// slots (HashScenery360), the per-player arrays and the co-op slots (HashPlayerArrays), the team member arrays
// (HashTeamArrays), and it leaves out the per-player values of players who are not in the level (HashPerPlayer) and
// what follows the camera a client draws and only sound or draw code reads (ClearViewState, ClearPlayerViewState). With
// co-op off the hash stays as it was, so a single-player log still lines up with one from an older build
// (docs/coop/QA.md, test plan item 1).
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
#include "port/coop/game/team.h"
#include "port/coop/test/switches.h"

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
#define VIEW_LOG_PERIOD 100  // frames between two "# view" lines

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

// One value per player, outside the Player struct, that the simulation reads. Why the other per-player arrays are left
// out: docs/coop/NOTES.md, 2026-11-06, #67. gLifeCount, gBombCount and gPlayerScores are in HashGlobals.
typedef struct {
    const char* name;
    void* array; // COOP_PLAYERS_MAX values
    size_t size; // of one value
} PlayerArray;

#define PLAYER_ARRAY(array) { #array, (array), sizeof((array)[0]) }

static const PlayerArray sPlayerArrays[] = {
    PLAYER_ARRAY(gLeftWingHealth),       PLAYER_ARRAY(gRightWingHealth), PLAYER_ARRAY(gLaserStrength),
    PLAYER_ARRAY(gChargeTimers),         PLAYER_ARRAY(gUturnDownTimers), PLAYER_ARRAY(gUturnBrakeTimers),
    PLAYER_ARRAY(gLoopDownTimers),       PLAYER_ARRAY(gLoopBoostTimers), PLAYER_ARRAY(gLeftWingDebrisTimer),
    PLAYER_ARRAY(gRightWingDebrisTimer), PLAYER_ARRAY(gHasShield),       PLAYER_ARRAY(gShieldTimer),
    PLAYER_ARRAY(gShieldAlpha),
};

static FILE* sLog = NULL;
static CoopStateDump* sDump = NULL;         // while ComputeDump runs
static const uint64_t* sDumpRegions = NULL; // its out->region, to name the region of a chunk from its hash pointer
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

// Every hashed value or element goes through here, so a dump holds exactly the bytes the hash saw (WP-23).
static void HashNamed(uint64_t* h, const char* name, int32_t index, const void* data, size_t size) {
    Fnv(h, data, size);
    if (sDump != NULL) {
        CoopStateDump* d = sDump;

        if ((d->used + size > d->cap) || (d->count >= d->chunkCap)) {
            d->overflow = true;
            return;
        }
        memcpy(d->bytes + d->used, data, size);
        d->chunks[d->count].name = name;
        d->chunks[d->count].index = index;
        d->chunks[d->count].region = (uint8_t) (h - sDumpRegions);
        d->chunks[d->count].size = (uint32_t) size;
        d->chunks[d->count].offset = (uint32_t) d->used;
        d->count++;
        d->used += size;
    }
}

#define HASH_VAR(h, var) HashNamed((h), #var, -1, &(var), sizeof(var))

// One value per team member, beyond gTeamShields, that the simulation reads. WP-17 puts human players in these slots.
// Only the slots from `first` up to `end` are read as a team member's: gTeamDamage drains for Falco to Peppy
// (src/engine/fox_play.c:7029), shields are restored from Falco up (src/engine/fox_play.c:2935).
typedef struct {
    const char* name;
    s32* array;
    s32 count;
    s32 first;
    s32 end;
} TeamArray;

#define TEAM_ARRAY(array, first, end) { #array, (array), ARRAY_COUNT(array), (first), (end) }

static const TeamArray sTeamArrays[] = {
    TEAM_ARRAY(gTeamDamage, TEAM_ID_FALCO, TEAM_ID_PEPPY + 1),
    TEAM_ARRAY(gSavedTeamShields, TEAM_ID_FALCO, TEAM_ID_MAX),
    TEAM_ARRAY(gPrevPlanetTeamShields, TEAM_ID_FALCO, TEAM_ID_MAX),
    TEAM_ARRAY(D_ctx_80177C58, TEAM_ID_FALCO, TEAM_ID_MAX),
    TEAM_ARRAY(gTeamEventActorIndex, TEAM_ID_FOX, ARRAY_COUNT(gTeamEventActorIndex)),
    TEAM_ARRAY(D_enmy_Timer_80161670, TEAM_ID_FOX, ARRAY_COUNT(D_enmy_Timer_80161670)),
};

static bool PlayersValid(void) {
    return (gGameState == GSTATE_PLAY) && (gPlayer != NULL) && (CoopPlayers_Count() >= 1) &&
           (CoopPlayers_Count() <= COOP_PLAYERS_MAX);
}

// With co-op off every slot of a per-player array counts, used or not. In co-op a slot counts only while its player is
// in the level. A waiting player's values are what an earlier level or versus match left, which clients need not
// share, and joining overwrites them (CoopPlayers_JoinFromPlayer0). An out player's stop changing, and were hashed
// until then.
static bool PlayerValuesCount(s32 i) {
    if (!CoopPlayers_IsCoop()) {
        return true;
    }
    return (i < CoopPlayers_Count()) && !CoopPlayers_IsParked(i);
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

// Where an object is for the sound engine, as seen from the camera a client draws. In co-op that differs between
// clients with nothing wrong. Only sound and draw code read it: docs/coop/NOTES.md, 2026-11-06, #96.
static void ClearViewState(f32* sfxSource) {
    if (CoopPlayers_IsCoop()) {
        sfxSource[0] = sfxSource[1] = sfxSource[2] = 0.0f;
    }
}

// The same for a player, with what draw code writes for a player the camera sees. Same note.
static void ClearPlayerViewState(Player* player) {
    if (!CoopPlayers_IsCoop()) {
        return;
    }
    ClearViewState(player->sfxSource);
    ClearViewState(player->sfxVel);
    memset(&player->sfx, 0, sizeof(player->sfx));
    player->shadowing = 0;
    player->arwing.unk_28 = 0.0f;
    player->arwing.upperRightFlapYrot = 0.0f;
    player->arwing.bottomRightFlapYrot = 0.0f;
    player->arwing.upperLeftFlapYrot = 0.0f;
    player->arwing.bottomLeftFlapYrot = 0.0f;
}

// A per-player array, slot by slot. With co-op off that is the same bytes in the same order as hashing the array whole.
static void HashPerPlayer(uint64_t* h, const char* name, const void* array, size_t size) {
    static const u8 sEmpty[sizeof(s32)] = { 0 };
    const u8* value = (const u8*) array;
    s32 i;

    for (i = 0; i < COOP_PLAYERS_MAX; i++, value += size) {
        HashNamed(h, name, i, PlayerValuesCount(i) ? value : sEmpty, size);
    }
}

#define HASH_PER_PLAYER(h, array) HashPerPlayer((h), #array, (array), sizeof((array)[0]))

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
        ClearPlayerViewState(&copy);
        HashNamed(h, "gPlayer", i, &copy, sizeof(copy));
    }
}

static void HashPlayerArrays(uint64_t* h) {
    s32 slot;
    s32 i;

    for (i = 0; i < ARRAY_COUNT(sPlayerArrays); i++) {
        HashPerPlayer(h, sPlayerArrays[i].name, sPlayerArrays[i].array, sPlayerArrays[i].size);
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        slot = CoopPlayers_SlotState(i);
        HashNamed(h, "slot", i, &slot, sizeof(slot));
    }
}

static void HashTeamArrays(uint64_t* h) {
    const TeamArray* team;
    s32 i;
    const uint8_t* choices;
    int32_t count;

    for (i = 0; i < ARRAY_COUNT(sTeamArrays); i++) {
        team = &sTeamArrays[i];
        HashNamed(h, team->name, team->first, &team->array[team->first], (team->end - team->first) * sizeof(s32));
    }
    // Who flies each scene of a human-flown wingman in this level (decision 0012, src/port/coop/game/team.h).
    choices = CoopTeam_Choices(&count);
    HashNamed(h, "sceneChoice", 0, choices, count);
}

static void HashActors(uint64_t* h) {
    Actor copy;
    s32 i;
    s32 k;

    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        copy = gActors[i];
        ClearInfoPointers(&copy.info);
        ClearViewState(copy.sfxSource);
        // SectorX_SxSlippy_Draw turns it to face the camera drawn, and only draw code reads that.
        if (CoopPlayers_IsCoop() && (copy.obj.id == OBJ_ACTOR_SX_SLIPPY)) {
            copy.obj.rot.x = copy.obj.rot.y = 0.0f;
        }
        for (k = 0; k < ARRAY_COUNT(copy.iwork); k++) {
            copy.iwork[k] = (uintptr_t) NormalizeWord(copy.iwork[k]);
        }
        HashNamed(h, "gActors", i, &copy, sizeof(copy));
    }
}

// Objects whose only pointers are in ObjectInfo.
#define HASH_INFO_ARRAY(h, Type, array, count)                 \
    do {                                                       \
        Type copy_;                                            \
        s32 i_;                                                \
        for (i_ = 0; i_ < (count); i_++) {                     \
            copy_ = (array)[i_];                               \
            ClearInfoPointers(&copy_.info);                    \
            HashNamed((h), #array, i_, &copy_, sizeof(copy_)); \
        }                                                      \
    } while (0)

// The same, for objects that also have a sound position.
#define HASH_SOUND_ARRAY(h, Type, array, count)                \
    do {                                                       \
        Type copy_;                                            \
        s32 i_;                                                \
        for (i_ = 0; i_ < (count); i_++) {                     \
            copy_ = (array)[i_];                               \
            ClearInfoPointers(&copy_.info);                    \
            ClearViewState(copy_.sfxSource);                   \
            HashNamed((h), #array, i_, &copy_, sizeof(copy_)); \
        }                                                      \
    } while (0)

static void HashEffects(uint64_t* h) {
    Effect copy;
    s32 i;

    for (i = 0; i < ARRAY_COUNT(gEffects); i++) {
        copy = gEffects[i];
        ClearInfoPointers(&copy.info);
        copy.unk_74 = NULL;
        ClearViewState(copy.sfxSource);
        HashNamed(h, "gEffects", i, &copy, sizeof(copy));
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
        HashNamed(h, "gScenery360", i, &copy, sizeof(copy));
    }
}

static void HashItems(uint64_t* h) {
    Item copy;
    s32 i;

    for (i = 0; i < ARRAY_COUNT(gItems); i++) {
        copy = gItems[i];
        ClearInfoPointers(&copy.info);
        ClearViewState(copy.sfxSource);
        // ItemMeteoWarp_Draw turns it to face the camera drawn, and only Item_Draw reads that.
        if (CoopPlayers_IsCoop() && (copy.obj.id == OBJ_ITEM_METEO_WARP)) {
            copy.obj.rot.y = 0.0f;
        }
        HashNamed(h, "gItems", i, &copy, sizeof(copy));
    }
}

static void HashShots(uint64_t* h) {
    PlayerShot copy;
    s32 i;

    for (i = 0; i < ARRAY_COUNT(gPlayerShots); i++) {
        copy = gPlayerShots[i];
        ClearViewState(copy.sfxSource);
        HashNamed(h, "gPlayerShots", i, &copy, sizeof(copy));
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
    HASH_PER_PLAYER(h, gLifeCount);
    HASH_PER_PLAYER(h, gBombCount);
    HASH_VAR(h, gGoldRingCount); // four slots, but not per player: [0] is the count, [1] what the HUD shows of it
    HASH_PER_PLAYER(h, gPlayerScores);
    HASH_VAR(h, gRadioState);
    HASH_VAR(h, gRadioMsgId);
    if (CoopPlayers_IsCoop()) {
        // The low-health radio timer (#87). A drifted one shows only at the next limit that draws a number, so it is
        // hashed on its own. Co-op only, so a single-player log lines up with one from an older build.
        HASH_VAR(h, gTeamLowHealthMsgTimer);
    }
}

static void Compute(CoopStateHash* out) {
    uint64_t* r = out->region;
    s32 i;

    UpdateRanges();
    for (i = 0; i < COOP_HASH_REGION_COUNT; i++) {
        r[i] = FNV_OFFSET;
    }

    HashPlayers(&r[COOP_HASH_PLAYERS]);
    if (CoopPlayers_IsCoop()) {
        HashPlayerArrays(&r[COOP_HASH_PLAYERS]);
    }
    HashActors(&r[COOP_HASH_ACTORS]);
    HASH_SOUND_ARRAY(&r[COOP_HASH_BOSSES], Boss, gBosses, ARRAY_COUNT(gBosses));
    HashEffects(&r[COOP_HASH_EFFECTS]);
    HashItems(&r[COOP_HASH_ITEMS]);
    HASH_SOUND_ARRAY(&r[COOP_HASH_SCENERY], Scenery, gScenery, ARRAY_COUNT(gScenery));
    HASH_INFO_ARRAY(&r[COOP_HASH_SPRITES], Sprite, gSprites, ARRAY_COUNT(gSprites));
    if (Scenery360Valid()) {
        HashScenery360(&r[COOP_HASH_SCENERY360]);
    }
    HashShots(&r[COOP_HASH_SHOTS]);
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
    if (CoopPlayers_IsCoop()) {
        HashTeamArrays(&r[COOP_HASH_GLOBALS]);
    }

    out->total = FNV_OFFSET;
    Fnv(&out->total, r, sizeof(out->region));
}

void CoopStateHash_Compute(CoopStateHash* out) {
    Compute(out);
}

size_t CoopStateHash_DumpBytes(void) {
    size_t bytes = COOP_PLAYERS_MAX * sizeof(Player) + sizeof(gActors) + sizeof(gBosses) + sizeof(gEffects) +
                   sizeof(gItems) + sizeof(gScenery) + sizeof(gSprites) + SCENERY360_COUNT * sizeof(Scenery360) +
                   sizeof(gPlayerShots) + sizeof(gTexturedLines) + sizeof(gBonusText) + sizeof(gRadarMarks);
    s32 i;

    for (i = 0; i < ARRAY_COUNT(sPlayerArrays); i++) {
        bytes += COOP_PLAYERS_MAX * sPlayerArrays[i].size;
    }
    for (i = 0; i < ARRAY_COUNT(sTeamArrays); i++) {
        bytes += sTeamArrays[i].count * sizeof(s32);
    }
    return bytes + 1024; // the slots, the seeds and HashGlobals take far less
}

void CoopStateHash_ComputeDump(CoopStateHash* out, CoopStateDump* dump) {
    dump->used = 0;
    dump->count = 0;
    dump->overflow = false;
    sDump = dump;
    sDumpRegions = out->region;
    Compute(out);
    sDump = NULL;
    sDumpRegions = NULL;
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

static void ProbePlayerValue(const char* name, s32 i, void* where) {
    char target[40];

    snprintf(target, sizeof(target), "%s[%d]", name, i);
    ProbeBit(target, !PlayerValuesCount(i), where);
}

// As ProbeBit, for the slot of player i, which only players.c can reach.
static void ProbeSlot(s32 i) {
    char target[40];
    s32 saved = CoopPlayers_SlotState(i);
    CoopStateHash before;
    CoopStateHash after;

    CoopStateHash_Compute(&before);
    CoopPlayers_SetSlotState(i, saved ^ 1);
    CoopStateHash_Compute(&after);
    CoopPlayers_SetSlotState(i, saved);
    snprintf(target, sizeof(target), "slot[%d]", i);
    ProbeReport(target, !PlayerValuesCount(i), &before, &after);
}

// Test only: what tools/coop/hash_check.py probe reads. Which of these must change the hash is written there.
static void Probe(void) {
    char target[16];
    s32 i;
    s32 k;

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
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        for (k = 0; k < ARRAY_COUNT(sPlayerArrays); k++) {
            ProbePlayerValue(sPlayerArrays[k].name, i, (u8*) sPlayerArrays[k].array + i * sPlayerArrays[k].size);
        }
        ProbePlayerValue("gLifeCount", i, &gLifeCount[i]);
        ProbePlayerValue("gBombCount", i, &gBombCount[i]);
        ProbePlayerValue("gPlayerScores", i, &gPlayerScores[i]);
        ProbeSlot(i);
    }
    ProbeBit("gTeamLowHealthMsgTimer", 0, &gTeamLowHealthMsgTimer);
    for (k = 0; k < ARRAY_COUNT(sTeamArrays); k++) {
        for (i = 0; i < sTeamArrays[k].count; i++) {
            char target[40];

            snprintf(target, sizeof(target), "%s[%d]", sTeamArrays[k].name, i);
            ProbeBit(target, 0, &sTeamArrays[k].array[i]);
        }
    }
}

static s32 sRadioPrev = 0;
static s32 sRadioChanges = 0;

// Slots of an object array that are not free. Every object type starts with an Obj, whose status is its first byte.
static s32 CountUsed(const void* array, size_t size, size_t count) {
    s32 used = 0;
    size_t i;

    for (i = 0; i < count; i++) {
        used += ((const Object*) ((const u8*) array + i * size))->status != OBJ_FREE;
    }
    return used;
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

    // Test only: the camera this client drew, which the hash must not show. tools/coop/hash_check.py views reads it, to
    // know two runs really looked through different players.
    if (CoopPlayers_IsCoop() && (gGameState == GSTATE_PLAY) && ((sFrame % VIEW_LOG_PERIOD) == 0)) {
        fprintf(sLog, "# view frame=%u player=%d eye=%.1f %.1f %.1f\n", sFrame, CoopPlayers_ViewIndex(), gPlayCamEye.x,
                gPlayCamEye.y, gPlayCamEye.z);
    }

    // Test only: how many slots of each object array are in use. Nothing frees an effect or a shot for leaving the view
    // in co-op any more (#96), so tools/coop/hash_check.py views reads this to see whether any array piles up or fills
    // (#129). With co-op off too, to have single player to compare with.
    // Which effects hold the slots: id:count for every effect id in use (Effect.obj.id).
    if ((gGameState == GSTATE_PLAY) && ((sFrame % VIEW_LOG_PERIOD) == 0)) {
        s32 counts[512] = { 0 };
        s32 k;

        for (k = 0; k < ARRAY_COUNT(gEffects); k++) {
            if (gEffects[k].obj.status != OBJ_FREE) {
                counts[gEffects[k].obj.id & 511]++;
            }
        }
        fprintf(sLog, "# effects frame=%u", sFrame);
        for (k = 0; k < 512; k++) {
            if (counts[k] != 0) {
                fprintf(sLog, " %d:%d", k, counts[k]);
            }
        }
        fprintf(sLog, "\n");
    }

    // And how often the radio state changed so far: tools/coop/hash_check.py views plays two clients at different audio
    // sample counts, and the radio follows the sound thread, so it wants to know a line was played.
    if (gGameState == GSTATE_PLAY) {
        sRadioChanges += (gRadioState != sRadioPrev);
        sRadioPrev = gRadioState;
    }
    if ((gGameState == GSTATE_PLAY) && ((sFrame % VIEW_LOG_PERIOD) == 0)) {
        fprintf(sLog, "# slots frame=%u effects=%d/%d actors=%d/%d shots=%d/%d items=%d/%d radio=%d\n", sFrame,
                CountUsed(gEffects, sizeof(gEffects[0]), ARRAY_COUNT(gEffects)), (s32) ARRAY_COUNT(gEffects),
                CountUsed(gActors, sizeof(gActors[0]), ARRAY_COUNT(gActors)), (s32) ARRAY_COUNT(gActors),
                CountUsed(gPlayerShots, sizeof(gPlayerShots[0]), ARRAY_COUNT(gPlayerShots)),
                (s32) ARRAY_COUNT(gPlayerShots), CountUsed(gItems, sizeof(gItems[0]), ARRAY_COUNT(gItems)),
                (s32) ARRAY_COUNT(gItems), sRadioChanges);
    }

    // Test only: the most draws a game frame got since the last line, counted at the draw loop. Vsync and test speed
    // can both keep a game from gInterpolationFPS without a word, and GameEngine_GetInterpolationFrameCount reads only
    // the CVars, so tools/coop/hash_check.py rates reads this to know a run really used its rate. The most, as a rate
    // that does not divide evenly gives frames of different counts, and this line lands on the same ones.
    if ((sFrame % VIEW_LOG_PERIOD) == 0) {
        fprintf(sLog, "# draws frame=%u count=%d\n", sFrame, CoopTestSwitches_TakeMostDraws());
    }

    // Test only: Play_Init runs on this frame, from these seeds. tools/coop/hash_check.py start lines two logs up here,
    // and a game that starts straight in a level starts from them (start_level.h). A restart logs another line.
    if ((gGameState == GSTATE_PLAY) && (gPlayState == PLAY_INIT)) {
        fprintf(sLog, "# level frame=%u seeds=%d,%d,%d,%d,%d,%d\n", sFrame, sRandSeed1, sRandSeed2, sRandSeed3,
                sSeededRandSeed1, sSeededRandSeed2, sSeededRandSeed3);
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

void CoopStateHash_PokeNow(void) {
    sPokeFrame = sFrame;
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
