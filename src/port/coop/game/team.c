#include "team.h"

#include "players.h"
#include "port/coop/session/session.h"

#include "global.h"
#include "sfx.h"
#include "port/hooks/Events.h"

// ---- 0006 rules 1 and 4: the value -------------------------------------------------------------------------------

// The team id player `index` flies, or -1: the roster holds CoopCharacter values, FOX to PEPPY in TeamId order.
static s32 CoopTeam_OfPlayer(s32 index) {
    s32 character;

    if ((index <= 0) || (index >= CoopPlayers_Count()) || !CoopPlayers_IsCoop()) {
        return -1;
    }
    character = CoopPlayers_Character(index);
    if ((character < COOP_CHARACTER_FALCO) || (character > COOP_CHARACTER_PEPPY)) {
        return -1;
    }
    return TEAM_ID_FALCO + (character - COOP_CHARACTER_FALCO);
}

int32_t CoopTeam_IsHuman(int32_t teamId) {
    s32 i;

    if ((teamId < TEAM_ID_FALCO) || (teamId > TEAM_ID_PEPPY)) {
        return 0;
    }
    for (i = 1; i < CoopPlayers_Count(); i++) {
        if (CoopTeam_OfPlayer(i) == teamId) {
            return 1;
        }
    }
    return 0;
}

// Decision 0006 rule 2: shields carry from a cleared level to the next, plus COOP_LEVEL_HEAL, at most 255; a player who
// was down comes back at COOP_LEVEL_HEAL. Taken when the level is cleared, used once per player at the next level's
// Player_Setup. Without one (the first level of a run, a retry) every player starts at 255, as the original does.
#define COOP_LEVEL_HEAL 64

static s16 sCarry[COOP_PLAYERS_MAX];      // shields at the clear, -1 for a player who was down or out
static bool sCarryPending[COOP_PLAYERS_MAX];
static bool sCarryTaken = false; // this level's clear was already taken

static void CoopTeam_TakeCarry(void) {
    s32 i;

    if (sCarryTaken || (gPlayer[0].state != PLAYERSTATE_LEVEL_COMPLETE)) {
        return;
    }
    sCarryTaken = true;
    for (i = 0; i < CoopPlayers_Count(); i++) {
        bool down = CoopPlayers_IsParked(i) || (gPlayer[i].state == PLAYERSTATE_DOWN) || (gPlayer[i].shields <= 0);

        sCarry[i] = down ? -1 : (s16) MIN(gPlayer[i].shields, 255);
        sCarryPending[i] = true;
    }
}

void CoopTeam_ForgetCarry(void) {
    s32 i;

    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        sCarryPending[i] = false;
    }
}

void CoopTeam_LevelShields(Player* player) {
    s32 i = player->num;

    if (!CoopPlayers_IsCoop() || gVersusMode || (i < 0) || (i >= COOP_PLAYERS_MAX) || !sCarryPending[i]) {
        return;
    }
    sCarryPending[i] = false;
    player->shields = (sCarry[i] < 0) ? COOP_LEVEL_HEAL : MIN(sCarry[i] + COOP_LEVEL_HEAL, 255);
}

void CoopTeam_Update(void) {
    s32 i;
    s32 id;
    s32 shields;

    CoopTeam_TakeCarry();
    for (i = 1; i < CoopPlayers_Count(); i++) {
        id = CoopTeam_OfPlayer(i);
        if ((id < 0) || CoopPlayers_IsParked(i)) {
            continue; // a parked player's value is left alone (0006 rule 1), the -1 of a down included
        }
        shields = gPlayer[i].shields;
        gTeamShields[id] = (shields < 1) ? 1 : (shields > 255) ? 255 : shields;
    }
}

void CoopTeam_OnPlayerDown(int32_t index) {
    s32 id = CoopTeam_OfPlayer(index);

    if (id >= 0) {
        gTeamShields[id] = -1;
        gTeamDamage[id] = 0;
    }
}

// ---- 0012: scenes --------------------------------------------------------------------------------------------------

// One scene: the wingman it shows and the scripts of the level that belong to it (`levelScripts[aiType]`, cited as
// `CO S59` in docs/coop/NOTES.md, entries #59 and #15). The first script is the wingman's own, the rest are its leader,
// chasers or the enemies that hurt it. Built from the #59 table and a scan of every script with EVOP_SET_TEAM_ID on a
// wingman (tools/coop/script_reader.py, US 1.0). SX S94 is left out: it brings a downed Slippy back, and its gate means
// "Slippy is not up", which a substitute would turn round.
typedef struct {
    u8 level;
    u8 team;
    u8 scripts[10]; // ends at COOP_TEAM_END
} CoopScene;

#define COOP_TEAM_END 0xFF

#define F TEAM_ID_FALCO
#define S TEAM_ID_SLIPPY
#define P TEAM_ID_PEPPY
#define SCENE(level, team, ...) \
    { level, team, { __VA_ARGS__ }, sizeof((u8[]) { __VA_ARGS__ }) / sizeof(u8) }

static const CoopScene sScenes[] = {
    { LEVEL_CORNERIA, TEAM_ID_FALCO, { 4, COOP_TEAM_END } },
    { LEVEL_CORNERIA, TEAM_ID_FALCO, { 9, COOP_TEAM_END } },
    { LEVEL_CORNERIA, TEAM_ID_FALCO, { 14, COOP_TEAM_END } },
    { LEVEL_CORNERIA, TEAM_ID_FALCO, { 59, 60, 61, COOP_TEAM_END } },
    { LEVEL_CORNERIA, TEAM_ID_FALCO, { 64, COOP_TEAM_END } },
    { LEVEL_CORNERIA, TEAM_ID_SLIPPY, { 17, 16, COOP_TEAM_END } },
    { LEVEL_CORNERIA, TEAM_ID_SLIPPY, { 51, COOP_TEAM_END } },
    { LEVEL_CORNERIA, TEAM_ID_PEPPY, { 1, 2, COOP_TEAM_END } },
    { LEVEL_CORNERIA, TEAM_ID_PEPPY, { 32, COOP_TEAM_END } },

    { LEVEL_METEO, TEAM_ID_FALCO, { 11, COOP_TEAM_END } },
    { LEVEL_METEO, TEAM_ID_FALCO, { 141, COOP_TEAM_END } },
    { LEVEL_METEO, TEAM_ID_FALCO, { 142, COOP_TEAM_END } },
    { LEVEL_METEO, TEAM_ID_FALCO, { 143, COOP_TEAM_END } },
    { LEVEL_METEO, TEAM_ID_SLIPPY, { 21, 46, 84, COOP_TEAM_END } },
    { LEVEL_METEO, TEAM_ID_SLIPPY, { 37, COOP_TEAM_END } },
    { LEVEL_METEO, TEAM_ID_SLIPPY, { 42, COOP_TEAM_END } },
    { LEVEL_METEO, TEAM_ID_PEPPY, { 12, COOP_TEAM_END } },
    { LEVEL_METEO, TEAM_ID_PEPPY, { 38, COOP_TEAM_END } },
    { LEVEL_METEO, TEAM_ID_PEPPY, { 80, COOP_TEAM_END } },
    { LEVEL_METEO, TEAM_ID_PEPPY, { 135, COOP_TEAM_END } },
    { LEVEL_METEO, TEAM_ID_PEPPY, { 136, COOP_TEAM_END } },
    { LEVEL_METEO, TEAM_ID_PEPPY, { 149, COOP_TEAM_END } },

    { LEVEL_SECTOR_X, TEAM_ID_FALCO, { 1, COOP_TEAM_END } },
    { LEVEL_SECTOR_X, TEAM_ID_FALCO, { 26, COOP_TEAM_END } },
    { LEVEL_SECTOR_X, TEAM_ID_PEPPY, { 21, COOP_TEAM_END } },
    { LEVEL_SECTOR_X, TEAM_ID_PEPPY, { 76, 93, COOP_TEAM_END } },
    { LEVEL_SECTOR_X, TEAM_ID_PEPPY, { 80, 78, COOP_TEAM_END } },

    { LEVEL_AREA_6, TEAM_ID_FALCO, { 12, 28, 31, COOP_TEAM_END } },
    { LEVEL_AREA_6, TEAM_ID_FALCO, { 36, 26, COOP_TEAM_END } },
    { LEVEL_AREA_6, TEAM_ID_FALCO, { 41, COOP_TEAM_END } },
    { LEVEL_AREA_6, TEAM_ID_FALCO, { 48, COOP_TEAM_END } },
    { LEVEL_AREA_6, TEAM_ID_FALCO, { 74, COOP_TEAM_END } },
    { LEVEL_AREA_6, TEAM_ID_SLIPPY, { 13, COOP_TEAM_END } },
    { LEVEL_AREA_6, TEAM_ID_SLIPPY, { 21, 23, COOP_TEAM_END } },
    { LEVEL_AREA_6, TEAM_ID_SLIPPY, { 42, COOP_TEAM_END } },
    { LEVEL_AREA_6, TEAM_ID_SLIPPY, { 76, COOP_TEAM_END } },
    { LEVEL_AREA_6, TEAM_ID_PEPPY, { 1, COOP_TEAM_END } },
    { LEVEL_AREA_6, TEAM_ID_PEPPY, { 11, 10, 65, 66, 67, 68, 69, 70, 71, COOP_TEAM_END } },
    { LEVEL_AREA_6, TEAM_ID_PEPPY, { 40, COOP_TEAM_END } },
    { LEVEL_AREA_6, TEAM_ID_PEPPY, { 75, COOP_TEAM_END } },

    { LEVEL_SECTOR_Y, TEAM_ID_FALCO, { 17, COOP_TEAM_END } },
    { LEVEL_SECTOR_Y, TEAM_ID_FALCO, { 58, COOP_TEAM_END } },
    { LEVEL_SECTOR_Y, TEAM_ID_FALCO, { 71, COOP_TEAM_END } },
    { LEVEL_SECTOR_Y, TEAM_ID_FALCO, { 90, 89, COOP_TEAM_END } },
    { LEVEL_SECTOR_Y, TEAM_ID_FALCO, { 97, COOP_TEAM_END } },
    { LEVEL_SECTOR_Y, TEAM_ID_SLIPPY, { 19, COOP_TEAM_END } },
    { LEVEL_SECTOR_Y, TEAM_ID_SLIPPY, { 48, 50, 63, 76, COOP_TEAM_END } },
    { LEVEL_SECTOR_Y, TEAM_ID_SLIPPY, { 60, COOP_TEAM_END } },
    { LEVEL_SECTOR_Y, TEAM_ID_SLIPPY, { 99, COOP_TEAM_END } },
    { LEVEL_SECTOR_Y, TEAM_ID_PEPPY, { 18, COOP_TEAM_END } },
    { LEVEL_SECTOR_Y, TEAM_ID_PEPPY, { 33, COOP_TEAM_END } },
    { LEVEL_SECTOR_Y, TEAM_ID_PEPPY, { 59, COOP_TEAM_END } },
    { LEVEL_SECTOR_Y, TEAM_ID_PEPPY, { 75, COOP_TEAM_END } },
    { LEVEL_SECTOR_Y, TEAM_ID_PEPPY, { 86, COOP_TEAM_END } },
    { LEVEL_SECTOR_Y, TEAM_ID_PEPPY, { 98, COOP_TEAM_END } },

    { LEVEL_SOLAR, TEAM_ID_FALCO, { 21, 22, COOP_TEAM_END } },
    { LEVEL_SOLAR, TEAM_ID_FALCO, { 23, COOP_TEAM_END } },
    { LEVEL_SOLAR, TEAM_ID_SLIPPY, { 4, COOP_TEAM_END } },
    { LEVEL_SOLAR, TEAM_ID_SLIPPY, { 18, COOP_TEAM_END } },
    { LEVEL_SOLAR, TEAM_ID_SLIPPY, { 24, COOP_TEAM_END } },
    { LEVEL_SOLAR, TEAM_ID_PEPPY, { 3, COOP_TEAM_END } },
    { LEVEL_SOLAR, TEAM_ID_PEPPY, { 25, COOP_TEAM_END } },
    { LEVEL_SOLAR, TEAM_ID_PEPPY, { 26, COOP_TEAM_END } },

    { LEVEL_ZONESS, TEAM_ID_FALCO, { 7, COOP_TEAM_END } },
    { LEVEL_ZONESS, TEAM_ID_FALCO, { 8, 24, COOP_TEAM_END } },
    { LEVEL_ZONESS, TEAM_ID_FALCO, { 22, COOP_TEAM_END } },
    { LEVEL_ZONESS, TEAM_ID_SLIPPY, { 9, COOP_TEAM_END } },
    { LEVEL_ZONESS, TEAM_ID_SLIPPY, { 16, COOP_TEAM_END } },
    { LEVEL_ZONESS, TEAM_ID_SLIPPY, { 46, 39, COOP_TEAM_END } },
    { LEVEL_ZONESS, TEAM_ID_PEPPY, { 5, COOP_TEAM_END } },
    { LEVEL_ZONESS, TEAM_ID_PEPPY, { 15, COOP_TEAM_END } },

    { LEVEL_MACBETH, TEAM_ID_FALCO, { 2, 29, COOP_TEAM_END } },
    { LEVEL_MACBETH, TEAM_ID_FALCO, { 3, COOP_TEAM_END } },
    { LEVEL_MACBETH, TEAM_ID_FALCO, { 9, COOP_TEAM_END } },
    { LEVEL_MACBETH, TEAM_ID_FALCO, { 26, 43, COOP_TEAM_END } },
    { LEVEL_MACBETH, TEAM_ID_SLIPPY, { 4, COOP_TEAM_END } },
    { LEVEL_MACBETH, TEAM_ID_SLIPPY, { 27, 44, COOP_TEAM_END } },
    { LEVEL_MACBETH, TEAM_ID_SLIPPY, { 50, 56, COOP_TEAM_END } },
    { LEVEL_MACBETH, TEAM_ID_PEPPY, { 28, 45, COOP_TEAM_END } },
    { LEVEL_MACBETH, TEAM_ID_PEPPY, { 37, COOP_TEAM_END } },

    { LEVEL_TITANIA, TEAM_ID_FALCO, { 2, COOP_TEAM_END } },
    { LEVEL_TITANIA, TEAM_ID_FALCO, { 4, COOP_TEAM_END } },
    { LEVEL_TITANIA, TEAM_ID_FALCO, { 6, 15, 18, COOP_TEAM_END } },
    { LEVEL_TITANIA, TEAM_ID_FALCO, { 7, 16, 19, COOP_TEAM_END } },
    { LEVEL_TITANIA, TEAM_ID_FALCO, { 8, COOP_TEAM_END } },
    { LEVEL_TITANIA, TEAM_ID_FALCO, { 27, COOP_TEAM_END } },
    { LEVEL_TITANIA, TEAM_ID_PEPPY, { 3, COOP_TEAM_END } },
    { LEVEL_TITANIA, TEAM_ID_PEPPY, { 5, COOP_TEAM_END } },
    { LEVEL_TITANIA, TEAM_ID_PEPPY, { 24, COOP_TEAM_END } },
    { LEVEL_TITANIA, TEAM_ID_PEPPY, { 28, COOP_TEAM_END } },

    { LEVEL_VENOM_1, TEAM_ID_FALCO, { 0, COOP_TEAM_END } },
    { LEVEL_VENOM_1, TEAM_ID_FALCO, { 20, COOP_TEAM_END } },
    { LEVEL_VENOM_1, TEAM_ID_FALCO, { 83, COOP_TEAM_END } },
    { LEVEL_VENOM_1, TEAM_ID_FALCO, { 85, COOP_TEAM_END } },
    { LEVEL_VENOM_1, TEAM_ID_FALCO, { 86, COOP_TEAM_END } },
    { LEVEL_VENOM_1, TEAM_ID_FALCO, { 98, COOP_TEAM_END } },
    { LEVEL_VENOM_1, TEAM_ID_SLIPPY, { 1, COOP_TEAM_END } },
    { LEVEL_VENOM_1, TEAM_ID_SLIPPY, { 22, COOP_TEAM_END } },
    { LEVEL_VENOM_1, TEAM_ID_SLIPPY, { 84, COOP_TEAM_END } },
    { LEVEL_VENOM_1, TEAM_ID_SLIPPY, { 88, COOP_TEAM_END } },
    { LEVEL_VENOM_1, TEAM_ID_SLIPPY, { 99, COOP_TEAM_END } },
    { LEVEL_VENOM_1, TEAM_ID_PEPPY, { 21, COOP_TEAM_END } },
    { LEVEL_VENOM_1, TEAM_ID_PEPPY, { 77, COOP_TEAM_END } },
    { LEVEL_VENOM_1, TEAM_ID_PEPPY, { 82, COOP_TEAM_END } },
    { LEVEL_VENOM_1, TEAM_ID_PEPPY, { 97, COOP_TEAM_END } },
};

// Lines the game recorded for all three wingmen, used at the same step of scripts of the same shape (0012, Context).
// Columns: Falco, Slippy, Peppy.
static const u16 sLineSets[][3] = {
    { 9100, 9110, 9120 },    { 9130, 9140, 9150 },    { 9160, 9170, 9180 }, { 20315, 20316, 20317 },
    { 20319, 20320, 20321 }, { 20306, 20305, 20304 }, { 20220, 20222, 20221 },
};

static const s32 sSpeaker[] = { 0, RCID_FALCO, RCID_SLIPPY, RCID_PEPPY, RCID_KATT, RCID_BILL };

#define COOP_TEAM_SCENES_MAX 16 // the most scenes one level has is 15 (Sector Y, Venom 1)

// The choice per scene of this level, by its place among the level's rows of sScenes: 0 not chosen yet, else the team
// id that flies it (the scene's own wingman for a stand-in). Simulation state: it joins the hash.
static u8 sChoice[COOP_TEAM_SCENES_MAX];

void CoopTeam_LevelInit(void) {
    s32 i;

    sCarryTaken = false;
    if (!CoopPlayers_IsCoop()) {
        CoopTeam_ForgetCarry();
    }

    for (i = 0; i < COOP_TEAM_SCENES_MAX; i++) {
        sChoice[i] = 0;
    }
}

const uint8_t* CoopTeam_Choices(int32_t* count) {
    *count = COOP_TEAM_SCENES_MAX;
    return sChoice;
}

// The scene `actor`'s script belongs to, and its place among this level's scenes; NULL if none.
static const CoopScene* CoopTeam_FindScene(const Actor* actor, s32* place) {
    s32 i;
    s32 j;
    s32 n = 0;

    if ((actor == NULL) || (actor->obj.id != OBJ_ACTOR_EVENT)) {
        return NULL;
    }
    for (i = 0; i < ARRAY_COUNT(sScenes); i++) {
        if (sScenes[i].level != gCurrentLevel) {
            continue;
        }
        for (j = 0; sScenes[i].scripts[j] != COOP_TEAM_END; j++) {
            if (sScenes[i].scripts[j] == actor->aiType) {
                *place = n;
                return &sScenes[i];
            }
        }
        n++;
    }
    return NULL;
}

// A computer wingman who is up and not on screen as an event actor of its own.
static s32 CoopTeam_IsFree(s32 id) {
    s32 index = gTeamEventActorIndex[id];
    const Actor* other;

    if (CoopTeam_IsHuman(id) || (gTeamShields[id] <= 0)) {
        return 0;
    }
    if ((index >= 0) && (index < ARRAY_COUNT(gActors))) {
        other = &gActors[index];
        if ((other->obj.status == OBJ_ACTIVE) && (other->obj.id == OBJ_ACTOR_EVENT) && (other->iwork[12] == id)) {
            return 0;
        }
    }
    return 1;
}

// Who flies a scene of a human-flown `team`: the next free computer wingman after it, Falco, Slippy, Peppy, round, else
// the stand-in (0006 C1) under its own id.
// The live event actor flying as `id` (Katt or Bill have no gTeamEventActorIndex slot), or -1.
static s32 CoopTeam_EventActorOf(s32 id) {
    s32 i;

    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        if ((gActors[i].obj.status == OBJ_ACTIVE) && (gActors[i].obj.id == OBJ_ACTOR_EVENT) &&
            (gActors[i].iwork[12] == id)) {
            return i;
        }
    }
    return -1;
}

// Decision 0012 B4: a guest is free when no live event actor carries its id.
static s32 CoopTeam_GuestFree(s32 id) {
    return CoopTeam_EventActorOf(id) < 0;
}

static s32 CoopTeam_Choose(s32 team) {
    s32 step;
    s32 id;

    for (step = 1; step < 3; step++) {
        id = TEAM_ID_FALCO + ((team - TEAM_ID_FALCO + step) % 3);
        if (CoopTeam_IsFree(id)) {
            return id;
        }
    }
    // No computer wingman is free (four players, or the others down): a guest, Katt, else Bill, else the stand-in.
    if (CoopTeam_GuestFree(TEAM_ID_KATT)) {
        return TEAM_ID_KATT;
    }
    if (CoopTeam_GuestFree(TEAM_ID_BILL)) {
        return TEAM_ID_BILL;
    }
    return team;
}

// The team that flies the scene of `actor`'s script, if that scene belongs to a human-flown wingman; else 0.
static s32 CoopTeam_SceneSubstitute(Actor* actor, const CoopScene** sceneOut) {
    const CoopScene* scene;
    s32 place = 0;

    if (!CoopPlayers_IsCoop()) {
        return 0;
    }
    scene = CoopTeam_FindScene(actor, &place);
    if ((scene == NULL) || (place >= COOP_TEAM_SCENES_MAX) || !CoopTeam_IsHuman(scene->team)) {
        return 0;
    }
    if (sChoice[place] == 0) {
        sChoice[place] = CoopTeam_Choose(scene->team);
    }
    *sceneOut = scene;
    return sChoice[place];
}

int32_t CoopTeam_SceneTeam(Actor* actor, int32_t teamId) {
    const CoopScene* scene = NULL;
    s32 sub = CoopTeam_SceneSubstitute(actor, &scene);

    if ((sub == 0) || (teamId != scene->team)) {
        return teamId;
    }
    return sub;
}

int32_t CoopTeam_SceneActive(Actor* actor, int32_t teamId) {
    s32 id = CoopTeam_SceneTeam(actor, teamId);

    if (id >= TEAM_ID_KATT) {
        return 1; // 0012 rule 3: a guest's gate counts as true while it flies
    }
    return gTeamShields[id] > 0;
}

int32_t CoopTeam_SceneTarget(Actor* actor, int32_t teamId) {
    s32 id = CoopTeam_SceneTeam(actor, teamId);

    if (id >= TEAM_ID_KATT) {
        s32 index = CoopTeam_EventActorOf(id);

        return (index >= 0) ? index : actor->index;
    }
    return gTeamEventActorIndex[id];
}

int32_t CoopTeam_SceneEventType(Actor* actor, int32_t eventType) {
    const CoopScene* scene = NULL;
    s32 sub;

    if (eventType != EVID_TEAMMATE) {
        return eventType;
    }
    sub = CoopTeam_SceneSubstitute(actor, &scene);
    if ((sub == 0) || (actor->aiType != scene->scripts[0])) {
        return eventType; // only the scene's own wingman script flies the guest's ship
    }
    return (sub == TEAM_ID_KATT) ? EVID_KATT : (sub == TEAM_ID_BILL) ? EVID_BILL : eventType;
}

int32_t CoopTeam_SceneGuest(Actor* actor) {
    const CoopScene* scene = NULL;
    s32 sub = CoopTeam_SceneSubstitute(actor, &scene);

    return sub >= TEAM_ID_KATT;
}

// ---- 0006 rule 3: the all-range body and the boss fights ---------------------------------------------------------

// ActorAllRangeState is local to src/engine/fox_360.c:13; these are two of its values.
#define COOP_STATE360_3 3
#define COOP_STATE360_6 6 // a wingman that went down, on its way out

// The player index that flies `teamId`, or -1.
static s32 CoopTeam_PlayerOf(s32 teamId) {
    s32 i;

    for (i = 1; i < CoopPlayers_Count(); i++) {
        if (CoopTeam_OfPlayer(i) == teamId) {
            return i;
        }
    }
    return -1;
}

// `actor` is the all-range wingman in the slot of a human-flown character: gActors[1] to [3], spawned by
// ActorAllRange_SpawnTeam (src/engine/fox_360.c:376) with aiType equal to the slot.
static s32 CoopTeam_IsBodySlot(const Actor* actor) {
    s32 slot;

    if ((actor == NULL) || !CoopPlayers_IsCoop() || (gLevelMode != LEVELMODE_ALL_RANGE) ||
        (actor->obj.id != OBJ_ACTOR_ALLRANGE)) {
        return 0;
    }
    slot = actor - gActors;
    if ((slot < TEAM_ID_FALCO) || (slot > TEAM_ID_PEPPY) || (actor->aiType != slot)) {
        return 0;
    }
    return CoopTeam_IsHuman(slot);
}

// The body holds the slot while the level is played. In the intro and the level-clear scenes the game's own wingman
// flies, as a scene keeps its stand-in on rails (0006 C1): those scenes move gActors[1] to [3] themselves.
// PLAYERSTATE_U_TURN is play too: player 0 turns back at the edge of the map. Leaving it out let the game's wingman
// fly for the turn, and if its human was down then, it retreated and its slot was gone for the level.
static s32 CoopTeam_BodyInPlay(void) {
    return (gPlayer[0].state == PLAYERSTATE_ACTIVE) || (gPlayer[0].state == PLAYERSTATE_DOWN) ||
           (gPlayer[0].state == PLAYERSTATE_U_TURN);
}

// The body stays a body in play, and out of play while its human is down: the game's wingman would come back for the
// scene only to read -1 and retreat in view, where a wingman of the game that went down is gone (src/engine/
// fox_360.c:1445-1480). Seen in Fortuna's mid-level scene and while player 0 goes down (#15). A level restart sets
// the value above 0 again, and the game's wingman is back for the intro.
static s32 CoopTeam_BodyHeld(const Actor* actor) {
    return CoopTeam_BodyInPlay() || (gTeamShields[actor->aiType] <= 0);
}

// The human flies the character of `actor`'s slot: joined, not parked, and up.
static Player* CoopTeam_BodyFlier(const Actor* actor) {
    s32 index = CoopTeam_PlayerOf(actor->aiType);

    if ((index < 0) || CoopPlayers_IsParked(index) || (gTeamShields[actor->aiType] <= 0)) {
        return NULL;
    }
    return &gPlayer[index];
}

// A frame of the body in place of the wingman's AI, collision and draw: on its human, so enemies aimed at the slot
// (src/engine/fox_360.c:453, :1874-1880) aim at the human with their own code. gNoHitbox, not timer_0C2, keeps shots
// and ships off it, as enemies read timer_0C2 (:1948). With its human down or not flying it waits in COOP_STATE360_6, the
// state of a wingman that went down, which enemies leave (:1876), with timer_0C2 as the game sets it there (:1475).
static void CoopTeam_HoldBody(Actor* actor) {
    Player* player = CoopTeam_BodyFlier(actor);

    if (actor->info.hitbox != gNoHitbox) { // the first frame as a body: the wingman's engine stops with its AI
        actor->info.hitbox = gNoHitbox;
        Audio_KillSfxBySource(actor->sfxSource);
    }
    if (player == NULL) {
        actor->state = COOP_STATE360_6;
        actor->timer_0C2 = 10000;
        actor->iwork[1] = 0;
        return;
    }
    actor->obj.pos = player->pos;
    actor->vel = player->vel;
    actor->timer_0C2 = 0;
    if (actor->state == COOP_STATE360_6) {
        actor->state = COOP_STATE360_3; // back from down: free to be chosen as a target again (:1947-1951)
    }
}

// Out of play the game's wingman flies again: its hitbox and engine back as ActorAllRange_SpawnTeam set them.
static void CoopTeam_ReleaseBody(Actor* actor) {
    if (actor->info.hitbox == gNoHitbox) {
        actor->info.hitbox = SEGMENTED_TO_VIRTUAL(gTeamHitbox);
        AUDIO_PLAY_SFX(NA_SE_ARWING_ENGINE_FG, actor->sfxSource, 4);
    }
}

static void CoopTeam_OnObjectUpdate(IEvent* event) {
    ObjectUpdateEvent* ev = (ObjectUpdateEvent*) event;
    Actor* actor;

    if (ev->type != OBJECT_TYPE_ACTOR) {
        return;
    }
    actor = (Actor*) ev->object;
    if (!CoopTeam_IsBodySlot(actor)) {
        return;
    }
    if (!CoopTeam_BodyHeld(actor)) {
        CoopTeam_ReleaseBody(actor);
        return;
    }
    CoopTeam_HoldBody(actor);
    ev->event.cancelled = true;
}

static void CoopTeam_OnObjectDrawPreSetup(IEvent* event) {
    ObjectDrawPreSetupEvent* ev = (ObjectDrawPreSetupEvent*) event;

    if ((ev->type == OBJECT_TYPE_ACTOR) && CoopTeam_IsBodySlot((Actor*) ev->object) &&
        CoopTeam_BodyHeld((Actor*) ev->object)) {
        ev->event.cancelled = true;
    }
}

int32_t CoopTeam_Body(const Actor* actor) {
    Player* player;

    if (!CoopTeam_IsBodySlot(actor) || (actor->obj.status != OBJ_ACTIVE) || (actor->info.hitbox != gNoHitbox)) {
        return COOP_TEAM_BODY_NONE;
    }
    if (actor->state == COOP_STATE360_6) {
        return COOP_TEAM_BODY_AWAY;
    }
    player = CoopTeam_BodyFlier(actor);
    if ((player != NULL) && (actor->obj.pos.x == player->pos.x) && (actor->obj.pos.y == player->pos.y) &&
        (actor->obj.pos.z == player->pos.z)) {
        return COOP_TEAM_BODY_ON;
    }
    return COOP_TEAM_BODY_OFF;
}

int32_t CoopTeam_LeavesBossFight(int32_t teamId) {
    // Titania's ActorTeamBoss is Slippy's escape from Goras, a story scene, not a fight wingman (src/engine/fox_hud.c,
    // ActorTeamBoss_Init): it stays as the game has it until the Slippy lines of 0006 D are listed.
    return CoopTeam_IsHuman(teamId) && (gCurrentLevel != LEVEL_TITANIA);
}

void CoopTeam_Init(void) {
    REGISTER_LISTENER(ObjectUpdateEvent, CoopTeam_OnObjectUpdate, EVENT_PRIORITY_NORMAL);
    REGISTER_LISTENER(ObjectDrawPreSetupEvent, CoopTeam_OnObjectDrawPreSetup, EVENT_PRIORITY_NORMAL);
}

// Decision 0013: a line in a borrowed scene, of the taken wingman or of another speaker naming him, that plays as a
// named line of the substitute, or of another speaker naming the substitute. Checked before the sets of three; a
// taken wingman's line in neither is dropped (0012 rule 5).
typedef struct {
    u8 level;
    u8 team; // the scene's own wingman, the one a human flies
    u8 sub;  // the substitute flying the scene
    u16 msgId;
    u16 newMsgId;
    s16 newSpeaker;
} CoopRescueLine;

static const CoopRescueLine sRescueLines[] = {
    // Corneria, Falco's rescue (S59, S60, S61) flown by Slippy: Slippy's own rescue lines (CO S17:8, :33) and Peppy's
    // warning to Slippy from his fly-in (CO S51:13).
    { LEVEL_CORNERIA, TEAM_ID_FALCO, TEAM_ID_SLIPPY, 2167, 2062, RCID_SLIPPY }, // CO S60:7, his ship fails: a call for help
    { LEVEL_CORNERIA, TEAM_ID_FALCO, TEAM_ID_SLIPPY, 2166, 2061, RCID_PEPPY },  // CO S60:25, asks for help: Peppy warns
    { LEVEL_CORNERIA, TEAM_ID_FALCO, TEAM_ID_SLIPPY, 2337, 2336, RCID_SLIPPY }, // CO S59:44, thanks
    // Corneria, Falco's lead to the waterfall (S14) flown by Slippy: Slippy sets off and reports something ahead, two
    // of his own lines from other levels, and Fox's question to Falco becomes Peppy calling Slippy back, Peppy's line
    // to Slippy from his fly-in (CO S51:3).
    { LEVEL_CORNERIA, TEAM_ID_FALCO, TEAM_ID_SLIPPY, 2181, 6028, RCID_SLIPPY },  // CO S14:10, Falco leads off
    { LEVEL_CORNERIA, TEAM_ID_FALCO, TEAM_ID_SLIPPY, 2185, 20267, RCID_PEPPY },  // CO S14:19, Fox asks Falco
    { LEVEL_CORNERIA, TEAM_ID_FALCO, TEAM_ID_SLIPPY, 2188, 14190, RCID_SLIPPY }, // CO S14:28, Falco found it
};

// Decision 0013, by kind: every line a wingman speaks in a scene of the table above, by what it does in the scene, and
// for each kind one line each of Falco, Slippy and Peppy recorded (0 where one has none). A taken wingman's line of a
// kind plays as the substitute's line of that kind. Lines tied to one level's story (Solar's heat, Titania's talk,
// tutorial tips) are in no kind and stay dropped. Sorted from the scene scripts (tools/coop/script_reader.py) and the
// message texts decoded from a US 1.0 archive; nobody has listened yet.
typedef enum {
    COOP_LINE_HELP,     // an enemy on the tail, asks Fox for help
    COOP_LINE_TROUBLE,  // the ship is in trouble
    COOP_LINE_HIT,      // just took a hit
    COOP_LINE_THANKS,   // thanks for the rescue
    COOP_LINE_RELIEF,   // that was close
    COOP_LINE_ATTACK,   // goes after the enemy
    COOP_LINE_KILL,     // got one
    COOP_LINE_BACK_OFF, // tells Fox to leave this one alone
    COOP_LINE_LEAD,     // flies ahead, Fox to follow
    COOP_LINE_FOUND,    // reports something ahead
    COOP_LINE_INCOMING, // enemies coming
    COOP_LINE_PERSIST,  // the enemy does not give up
    COOP_LINE_ALERT,    // the team should stay alert
    COOP_LINE_SIDE,     // takes one side
    COOP_LINE_PRAISE,   // praises Fox
    COOP_LINE_KINDS,
} CoopLineKind;

static const u16 sKindLines[COOP_LINE_KINDS][5] = {
    // Falco, Slippy, Peppy, and the guests Katt and Bill (0012 B4), who recorded few lines that fit
    { 2166, 2062, 7066, 0, 0 }, // HELP
    { 2167, 20013, 7066, 0, 0 }, // TROUBLE
    { 9151, 9152, 9153, 0, 0 }, // HIT
    { 2337, 2336, 5230, 0, 0 }, // THANKS
    { 9430, 20015, 20016, 0, 0 }, // RELIEF
    { 9190, 3110, 9210, 16135, 0 }, // ATTACK
    { 9220, 9230, 9240, 0, 0 }, // KILL
    { 2095, 20150, 20070, 0, 0 }, // BACK_OFF
    { 2181, 6028, 5460, 16135, 0 }, // LEAD
    { 2188, 14190, 0, 0, 0 }, // FOUND
    { 18035, 20272, 4075, 0, 0 }, // INCOMING
    { 6050, 11240, 5420, 0, 0 }, // PERSIST
    { 11060, 3005, 8130, 0, 0 }, // ALERT
    { 8060, 21081, 8070, 0, 0 }, // SIDE
    { 20262, 19410, 17160, 0, 18100 }, // PRAISE
};

typedef struct {
    u16 msgId;
    u8 kind;
} CoopLineKindRow;

static const CoopLineKindRow sLineKinds[] = {
    // Falco's scene lines
    { 2165, COOP_LINE_ATTACK }, { 9190, COOP_LINE_ATTACK }, { 2090, COOP_LINE_KILL }, { 2095, COOP_LINE_BACK_OFF },
    { 2181, COOP_LINE_LEAD }, { 2188, COOP_LINE_FOUND }, { 2337, COOP_LINE_THANKS }, { 2167, COOP_LINE_TROUBLE },
    { 2166, COOP_LINE_HELP }, { 5010, COOP_LINE_ATTACK }, { 7065, COOP_LINE_HELP }, { 20301, COOP_LINE_ATTACK },
    { 7020, COOP_LINE_ATTACK }, { 20262, COOP_LINE_PRAISE }, { 14030, COOP_LINE_ATTACK },
    { 6045, COOP_LINE_INCOMING }, { 6050, COOP_LINE_PERSIST }, { 6051, COOP_LINE_ATTACK }, { 11060, COOP_LINE_ALERT },
    { 9430, COOP_LINE_RELIEF }, { 18035, COOP_LINE_INCOMING }, { 4031, COOP_LINE_INCOMING }, { 8060, COOP_LINE_SIDE },
    // Slippy's scene lines
    { 3110, COOP_LINE_ATTACK }, { 3120, COOP_LINE_ATTACK }, { 9152, COOP_LINE_HIT }, { 14140, COOP_LINE_ATTACK },
    { 14150, COOP_LINE_ATTACK }, { 14160, COOP_LINE_HELP }, { 6055, COOP_LINE_INCOMING }, { 6028, COOP_LINE_LEAD },
    { 2062, COOP_LINE_HELP }, { 2336, COOP_LINE_THANKS }, { 8040, COOP_LINE_ATTACK }, { 21081, COOP_LINE_SIDE },
    // Peppy's scene lines
    { 7013, COOP_LINE_ALERT }, { 7066, COOP_LINE_HELP }, { 5230, COOP_LINE_THANKS }, { 8130, COOP_LINE_ALERT },
    { 5460, COOP_LINE_LEAD }, { 5220, COOP_LINE_HIT }, { 14040, COOP_LINE_ALERT }, { 10255, COOP_LINE_INCOMING },
    { 8070, COOP_LINE_SIDE },
};

// Other speakers' lines in these scenes that name a wingman: dropped when that wingman is the taken one, unless a row
// of sRescueLines replaces them.
typedef struct {
    u16 msgId;
    u8 team;
} CoopNamesWingman;

static const CoopNamesWingman sNamesWingman[] = {
    { 2185, TEAM_ID_FALCO },  // CO S14:19, Fox
    { 4022, TEAM_ID_FALCO },  // TI S2:24, Fox
    { 4024, TEAM_ID_FALCO },  // TI S2:60, Peppy
    { 20267, TEAM_ID_SLIPPY }, // CO S51:3, Peppy
    { 2061, TEAM_ID_SLIPPY },  // CO S51:13, Peppy
};

int32_t CoopTeam_SceneMessage(Actor* actor, int32_t* msgId, int32_t* speaker) {
    const CoopScene* scene = NULL;
    s32 sub = CoopTeam_SceneSubstitute(actor, &scene);
    s32 i;

    if ((sub == 0) || (sub == scene->team)) {
        return 1; // not a borrowed scene
    }
    // The table first, any speaker: it also holds other speakers' lines that name the taken wingman (0013).
    for (i = 0; i < ARRAY_COUNT(sRescueLines); i++) {
        const CoopRescueLine* line = &sRescueLines[i];

        if ((line->level == scene->level) && (line->team == scene->team) && (line->sub == sub) &&
            (line->msgId == *msgId)) {
            *msgId = line->newMsgId;
            *speaker = line->newSpeaker;
            return 1;
        }
    }
    if (*speaker != sSpeaker[scene->team]) {
        // Another speaker's line. One that names the taken wingman, with no row above, is dropped (0013).
        for (i = 0; i < ARRAY_COUNT(sNamesWingman); i++) {
            if ((sNamesWingman[i].msgId == *msgId) && (sNamesWingman[i].team == scene->team)) {
                return 0;
            }
        }
        return 1;
    }
    for (i = 0; (sub <= TEAM_ID_PEPPY) && (i < ARRAY_COUNT(sLineSets)); i++) {
        if (sLineSets[i][scene->team - TEAM_ID_FALCO] == *msgId) {
            *msgId = sLineSets[i][sub - TEAM_ID_FALCO];
            *speaker = sSpeaker[sub];
            return 1;
        }
    }
    // 0013 by kind: the substitute's own line of the same kind, where it has one.
    for (i = 0; i < ARRAY_COUNT(sLineKinds); i++) {
        if (sLineKinds[i].msgId == *msgId) {
            u16 line = sKindLines[sLineKinds[i].kind][sub - TEAM_ID_FALCO];

            if (line == 0) {
                return 0;
            }
            *msgId = line;
            *speaker = sSpeaker[sub];
            return 1;
        }
    }
    return 0;
}
