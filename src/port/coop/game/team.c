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

void CoopTeam_Update(void) {
    s32 i;
    s32 id;
    s32 shields;

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

static const s32 sSpeaker[] = { 0, RCID_FALCO, RCID_SLIPPY, RCID_PEPPY };

#define COOP_TEAM_SCENES_MAX 16 // the most scenes one level has is 15 (Sector Y, Venom 1)

// The choice per scene of this level, by its place among the level's rows of sScenes: 0 not chosen yet, else the team
// id that flies it (the scene's own wingman for a stand-in). Simulation state: it joins the hash.
static u8 sChoice[COOP_TEAM_SCENES_MAX];

void CoopTeam_LevelInit(void) {
    s32 i;

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
static s32 CoopTeam_Choose(s32 team) {
    s32 step;
    s32 id;

    for (step = 1; step < 3; step++) {
        id = TEAM_ID_FALCO + ((team - TEAM_ID_FALCO + step) % 3);
        if (CoopTeam_IsFree(id)) {
            return id;
        }
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

int32_t CoopTeam_SceneMessage(Actor* actor, int32_t* msgId, int32_t* speaker) {
    const CoopScene* scene = NULL;
    s32 sub = CoopTeam_SceneSubstitute(actor, &scene);
    s32 i;

    if ((sub == 0) || (sub == scene->team) || (*speaker != sSpeaker[scene->team])) {
        return 1; // not a borrowed scene, or not the taken wingman's line
    }
    for (i = 0; i < ARRAY_COUNT(sLineSets); i++) {
        if (sLineSets[i][scene->team - TEAM_ID_FALCO] == *msgId) {
            *msgId = sLineSets[i][sub - TEAM_ID_FALCO];
            *speaker = sSpeaker[sub];
            return 1;
        }
    }
    return 0;
}
