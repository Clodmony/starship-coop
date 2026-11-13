// What each player gets in Andross' escape (#282). See escape_probe.h.

#include "escape_probe.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "port/hooks/Events.h"
#include "port/coop/game/players.h"
#include "public/bridge/consolevariablebridge.h"

#define ESCAPE_START_DELAY 30 // frames from every player flying in an escape to its first case
#define ESCAPE_PERIOD 30      // frames from the end of one case to the next
#define ESCAPE_WINDOW 60      // updates of the case's object
#define ESCAPE_REACH 500.0f   // both objects act under this in x and in z (Andross_AndExplosion_Update, _Trigger_)
#define ESCAPE_PIN 450.0f     // the target's distance from the object, on one axis
#define ESCAPE_MISS 550.0f    // the same for a miss case: the target just outside the object's reach
#define ESCAPE_KEEP 510.0f    // every other player at least this far from the object, in the objects' own measure
#define ESCAPE_AWAY 3000.0f   // where the object waits while no place keeps the others out
#define ESCAPE_CASES_MAX 8
#define ESCAPE_STATES_TEXT 48

typedef enum EscapeKind {
    ESCAPE_KIND_BLAST,
    ESCAPE_KIND_EXIT,
    ESCAPE_KIND_MISS, // an explosion, as ESCAPE_KIND_BLAST, placed ESCAPE_MISS from its target
} EscapeKind;

typedef struct EscapeCase {
    char name[12];
    EscapeKind kind;
    s32 target;
} EscapeCase;

typedef enum EscapeStage {
    ESCAPE_STAGE_WAIT,
    ESCAPE_STAGE_RUNNING,
    ESCAPE_STAGE_DONE,
} EscapeStage;

static FILE* sEscFile = NULL;
static EscapeCase sEscCases[ESCAPE_CASES_MAX];
static s32 sEscCaseCount = 0;
static s32 sEscCase = 0;
static EscapeStage sEscStage = ESCAPE_STAGE_WAIT;
static s32 sEscNext = -1; // the frame the next case may begin; -1 until every player flies in this escape
static s32 sEscLastFrame = -1;

// The running case.
static Actor* sEscObject = NULL;
static s32 sEscUpdated = 0; // the object had an update on this frame
static s32 sEscUpdates = 0;
static s32 sEscOff = 0; // updates it waited away
static s32 sEscCaseFrame = 0;
static s32 sEscCaseHit = 0;
static s32 sEscCaseDown = 0;
static s32 sEscCaseExit = 0;
static s32 sEscCaseLives = 0;
static f32 sEscReachMin[COOP_PLAYERS_MAX];
static f32 sEscReachMax[COOP_PLAYERS_MAX];
static s32 sEscShieldsBefore[COOP_PLAYERS_MAX];
static s32 sEscShieldsMin[COOP_PLAYERS_MAX];
static s32 sEscStateSeen[COOP_PLAYERS_MAX];
static char sEscStates[COOP_PLAYERS_MAX][ESCAPE_STATES_TEXT];

// The escape, run by run.
static bool sEscInEscape = false;
static bool sEscEver = false;
static s32 sEscRun = 0;
static f32 sEscNearest[COOP_PLAYERS_MAX];
static s32 sEscTouch[COOP_PLAYERS_MAX];

// What the player lines last said, per player: state, csState, parked, down; and gGameState.
static s32 sEscSaid[COOP_PLAYERS_MAX][4];
static bool sEscSaidAny[COOP_PLAYERS_MAX];
static s32 sEscGameState = -1;

// As src/port/coop/test/pilot_andross.c: the brain in its state 21 after the frame its maze is loaded (gCsFrameCount
// 600, Andross_AndBrain_Update; a restart at the escape's checkpoint begins at 599, Andross_801961AC).
static bool CoopEscapeProbe_InEscape(void) {
    const Boss* brain = &gBosses[0];

    return (gGameState == GSTATE_PLAY) && (gCurrentLevel == LEVEL_VENOM_ANDROSS) &&
           (gLevelMode == LEVELMODE_ALL_RANGE) && (gScenery360 != NULL) && (brain->obj.status == OBJ_ACTIVE) &&
           (brain->obj.id == OBJ_BOSS_AND_BRAIN) && (brain->state == 21) && (gCsFrameCount > 600);
}

// The measure both objects test with: the larger of the x and z distances (player z is trueZpos).
static f32 CoopEscapeProbe_Reach(const Vec3f* at, const Player* p) {
    return fmaxf(fabsf(at->x - p->pos.x), fabsf(at->z - p->trueZpos));
}

static bool CoopEscapeProbe_Flies(s32 i) {
    return !CoopPlayers_IsParked(i) &&
           ((gPlayer[i].state == PLAYERSTATE_ACTIVE) || (gPlayer[i].state == PLAYERSTATE_U_TURN));
}

static bool CoopEscapeProbe_IsDownState(s32 i) {
    return (gPlayer[i].state == PLAYERSTATE_DOWN) || (gPlayer[i].state == PLAYERSTATE_NEXT) || CoopPlayers_IsDown(i);
}

static bool CoopEscapeProbe_ObjectAlive(void) {
    EscapeKind kind = sEscCases[sEscCase].kind;

    return (sEscObject != NULL) && (sEscObject->obj.status != OBJ_FREE) &&
           (sEscObject->obj.id == ((kind == ESCAPE_KIND_EXIT) ? OBJ_ACTOR_AND_JAMES_TRIGGER : OBJ_ACTOR_AND_EXPLOSION));
}

// The object's place for this update: `pin` from the target on the side that keeps the other players farthest out, or
// ESCAPE_AWAY from it when no side keeps them ESCAPE_KEEP away. Returns whether it is in place.
static bool CoopEscapeProbe_Pin(Actor* object, s32 target, f32 pin) {
    static const f32 sides[4][2] = { { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f } };
    const Player* t = &gPlayer[target];
    f32 bestGap = -1.0f;
    s32 best = 0;
    s32 side;
    s32 i;

    for (side = 0; side < 4; side++) {
        Vec3f at = { t->pos.x + sides[side][0] * pin, t->pos.y, t->trueZpos + sides[side][1] * pin };
        f32 gap = 1e30f;

        for (i = 0; i < CoopPlayers_Count(); i++) {
            if (i != target) {
                gap = fminf(gap, CoopEscapeProbe_Reach(&at, &gPlayer[i]));
            }
        }
        if (gap > bestGap) {
            bestGap = gap;
            best = side;
        }
    }
    if (bestGap >= ESCAPE_KEEP) {
        object->obj.pos.x = t->pos.x + sides[best][0] * pin;
        object->obj.pos.z = t->trueZpos + sides[best][1] * pin;
    } else {
        object->obj.pos.x = t->pos.x + sides[best][0] * ESCAPE_AWAY;
        object->obj.pos.z = t->trueZpos + sides[best][1] * ESCAPE_AWAY;
    }
    object->obj.pos.y = t->pos.y;
    // Actor_Move runs between this event and the update: nothing may move it off its place.
    object->vel.x = object->vel.y = object->vel.z = 0.0f;
    object->gravity = 0.0f;
    return bestGap >= ESCAPE_KEEP;
}

// Right before the case's object updates: its place for this update.
static void CoopEscapeProbe_OnObjectUpdate(ObjectUpdateEvent* event) {
    if ((sEscStage != ESCAPE_STAGE_RUNNING) || (event->type != OBJECT_TYPE_ACTOR) || (event->object == NULL) ||
        (event->object != sEscObject) || !CoopEscapeProbe_ObjectAlive()) {
        return;
    }
    if (!CoopEscapeProbe_Pin(sEscObject, sEscCases[sEscCase].target,
                             (sEscCases[sEscCase].kind == ESCAPE_KIND_MISS) ? ESCAPE_MISS : ESCAPE_PIN)) {
        sEscOff++;
    }
    sEscUpdated = 1;
}

static Actor* CoopEscapeProbe_Spawn(ObjectId id, s32 target) {
    Actor* object;
    s32 i;

    // The last free slot, as the bomb probe takes it; the maze fills them from gActors[1] (Andross_80187C5C).
    for (i = ARRAY_COUNT(gActors) - 1; (i >= 0) && (gActors[i].obj.status != OBJ_FREE); i--) {}
    if (i < 0) {
        return NULL;
    }
    object = &gActors[i];
    Actor_Initialize(object);
    object->obj.status = OBJ_ACTIVE;
    object->obj.id = id;
    object->obj.pos.x = gPlayer[target].pos.x + ESCAPE_AWAY;
    object->obj.pos.y = gPlayer[target].pos.y;
    object->obj.pos.z = gPlayer[target].trueZpos;
    Object_SetInfo(&object->info, object->obj.id);
    return object;
}

static void CoopEscapeProbe_NoteState(s32 i) {
    size_t len = strlen(sEscStates[i]);

    if ((s32) gPlayer[i].state == sEscStateSeen[i]) {
        return;
    }
    sEscStateSeen[i] = gPlayer[i].state;
    if (len + 4 < ESCAPE_STATES_TEXT) {
        snprintf(sEscStates[i] + len, ESCAPE_STATES_TEXT - len, "%s%d", (len > 0) ? "-" : "", (int) gPlayer[i].state);
    }
}

static void CoopEscapeProbe_Begin(void) {
    const EscapeCase* c = &sEscCases[sEscCase];
    s32 i;

    sEscObject = CoopEscapeProbe_Spawn(
        (c->kind == ESCAPE_KIND_EXIT) ? OBJ_ACTOR_AND_JAMES_TRIGGER : OBJ_ACTOR_AND_EXPLOSION, c->target);
    if (sEscObject == NULL) {
        return;
    }
    sEscUpdated = sEscUpdates = sEscOff = 0;
    sEscCaseFrame = gGameFrameCount;
    sEscCaseHit = sEscCaseDown = sEscCaseExit = 0;
    sEscCaseLives = gLifeCount[0];
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        sEscReachMin[i] = 1e30f;
        sEscReachMax[i] = 0.0f;
        sEscShieldsBefore[i] = sEscShieldsMin[i] = gPlayer[i].shields;
        sEscStateSeen[i] = -1;
        sEscStates[i][0] = '\0';
        if (i < CoopPlayers_Count()) {
            CoopEscapeProbe_NoteState(i);
        }
    }
    sEscStage = ESCAPE_STAGE_RUNNING;
}

static void CoopEscapeProbe_End(void) {
    const EscapeCase* c = &sEscCases[sEscCase];
    s32 i;

    fprintf(sEscFile,
            "escape case=%s target=%d run=%d frame=%d end=%d updates=%d off=%d lost=%d exit=%d hit=%d down=%d "
            "lives=%d/%d backdrop=%d",
            c->name, (int) c->target, (int) sEscRun, (int) sEscCaseFrame, (int) gGameFrameCount, (int) sEscUpdates,
            (int) sEscOff, (int) (sEscUpdates < ESCAPE_WINDOW), (int) sEscCaseExit, (int) sEscCaseHit,
            (int) sEscCaseDown, (int) sEscCaseLives, (int) gLifeCount[0], (int) gDrawBackdrop);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        fprintf(sEscFile, " p%d=%.0f/%.0f,%d/%d/%d,%s", (int) i, (sEscReachMin[i] < 1e29f) ? sEscReachMin[i] : -1.0f,
                sEscReachMax[i], (int) sEscShieldsBefore[i], (int) sEscShieldsMin[i], (int) gPlayer[i].shields,
                sEscStates[i]);
    }
    fputc('\n', sEscFile);
    if (CoopEscapeProbe_ObjectAlive()) {
        Object_Kill(&sEscObject->obj, sEscObject->sfxSource);
    }
    sEscObject = NULL;
    sEscNext = gGameFrameCount + ESCAPE_PERIOD;
    sEscCase++;
    if (sEscCase >= sEscCaseCount) {
        fprintf(sEscFile, "escape cases done frame=%d cases=%d\n", (int) gGameFrameCount, (int) sEscCaseCount);
        sEscStage = ESCAPE_STAGE_DONE;
    } else {
        sEscStage = ESCAPE_STAGE_WAIT;
    }
    fflush(sEscFile);
}

// One frame of the running case, after the frame: what each player got.
static void CoopEscapeProbe_Watch(bool escapeOver) {
    const EscapeCase* c = &sEscCases[sEscCase];
    bool alive = CoopEscapeProbe_ObjectAlive();
    s32 i;

    if (sEscUpdated) {
        sEscUpdates++;
        sEscUpdated = 0;
    }
    fprintf(sEscFile, "escape t=%d frame=%d case=%s", (int) sEscUpdates, (int) gGameFrameCount, c->name);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        const Player* p = &gPlayer[i];
        f32 reach = alive ? CoopEscapeProbe_Reach(&sEscObject->obj.pos, p) : -1.0f;

        if (alive) {
            sEscReachMin[i] = fminf(sEscReachMin[i], reach);
            sEscReachMax[i] = fmaxf(sEscReachMax[i], reach);
        }
        sEscShieldsMin[i] = MIN(sEscShieldsMin[i], p->shields);
        if (p->hitTimer == 20) {
            sEscCaseHit |= 1 << i;
        }
        if (CoopEscapeProbe_IsDownState(i)) {
            sEscCaseDown |= 1 << i;
        }
        CoopEscapeProbe_NoteState(i);
        fprintf(sEscFile, " p%d=%d,%d,%d,%.0f,%d,%d", (int) i, (int) p->state, (int) CoopPlayers_IsParked(i),
                (int) p->shields, reach, (int) p->hitTimer, (int) p->dmgType);
    }
    fprintf(sEscFile, " lives=%d level=%d\n", (int) gLifeCount[0], (int) gCurrentLevel);
    if ((sEscUpdates >= ESCAPE_WINDOW) || !alive || escapeOver) {
        CoopEscapeProbe_End();
    }
}

static void CoopEscapeProbe_RunStart(void) {
    const Actor* trigger = NULL;
    s32 explosions = 0;
    s32 i;

    sEscRun++;
    sEscEver = true;
    sEscNext = -1;
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        sEscNearest[i] = 1e30f;
        sEscTouch[i] = 0;
    }
    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        const Actor* a = &gActors[i];

        if (a->obj.status != OBJ_ACTIVE) {
            continue;
        }
        if ((a->obj.id == OBJ_ACTOR_AND_JAMES_TRIGGER) && (trigger == NULL)) {
            trigger = a;
        } else if (a->obj.id == OBJ_ACTOR_AND_EXPLOSION) {
            explosions++;
        }
    }
    fprintf(sEscFile, "escape start run=%d frame=%d trigger=%.0f,%.0f,%.0f explosions=%d lives=%d inv=%d\n",
            (int) sEscRun, (int) gGameFrameCount, (trigger != NULL) ? trigger->obj.pos.x : 0.0f,
            (trigger != NULL) ? trigger->obj.pos.y : 0.0f, (trigger != NULL) ? trigger->obj.pos.z : 0.0f,
            (int) explosions, (int) gLifeCount[0], (int) CVarGetInteger("gInvincible", 0));
    fflush(sEscFile);
}

static void CoopEscapeProbe_RunEnd(bool exited) {
    s32 i;

    if (exited) {
        fprintf(sEscFile, "escape exit run=%d frame=%d case=%s\n", (int) sEscRun, (int) gGameFrameCount,
                (sEscStage == ESCAPE_STAGE_RUNNING) ? sEscCases[sEscCase].name : "-");
        if (sEscStage == ESCAPE_STAGE_RUNNING) {
            sEscCaseExit = 1;
        }
    }
    fprintf(sEscFile, "escape run_end run=%d frame=%d why=%s nearest=", (int) sEscRun, (int) gGameFrameCount,
            exited ? "exit" : "left");
    for (i = 0; i < CoopPlayers_Count(); i++) {
        fprintf(sEscFile, "%s%.0f", (i > 0) ? "," : "", (sEscNearest[i] < 1e29f) ? sEscNearest[i] : -1.0f);
    }
    fprintf(sEscFile, " touch=");
    for (i = 0; i < CoopPlayers_Count(); i++) {
        fprintf(sEscFile, "%s%d", (i > 0) ? "," : "", (int) sEscTouch[i]);
    }
    fputc('\n', sEscFile);
    fflush(sEscFile);
}

// The level's own explosions (not the case's): how near each flying player came to one.
static void CoopEscapeProbe_Nearest(void) {
    s32 i;
    s32 k;

    for (i = 0; i < CoopPlayers_Count(); i++) {
        f32 best = 1e30f;

        if (!CoopEscapeProbe_Flies(i)) {
            continue;
        }
        for (k = 0; k < ARRAY_COUNT(gActors); k++) {
            const Actor* a = &gActors[k];

            if ((a->obj.status == OBJ_ACTIVE) && (a->obj.id == OBJ_ACTOR_AND_EXPLOSION) && (a != sEscObject)) {
                best = fminf(best, CoopEscapeProbe_Reach(&a->obj.pos, &gPlayer[i]));
            }
        }
        sEscNearest[i] = fminf(sEscNearest[i], best);
        if (best < ESCAPE_REACH) {
            sEscTouch[i]++;
        }
    }
}

// A line for each change of a player's state, csState, parked or down, from the first escape on.
static void CoopEscapeProbe_SayPlayers(void) {
    s32 i;

    if ((gCurrentLevel != LEVEL_VENOM_ANDROSS) && (gCurrentLevel != LEVEL_VENOM_2)) {
        return;
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        const Player* p = &gPlayer[i];
        s32 now[4] = { p->state, p->csState, CoopPlayers_IsParked(i), CoopPlayers_IsDown(i) };

        if (sEscSaidAny[i] && (memcmp(now, sEscSaid[i], sizeof(now)) == 0)) {
            continue;
        }
        memcpy(sEscSaid[i], now, sizeof(now));
        sEscSaidAny[i] = true;
        fprintf(sEscFile,
                "escape player=%d frame=%d level=%d state=%d cs=%d parked=%d down=%d draw=%d shields=%d lives=%d\n",
                (int) i, (int) gGameFrameCount, (int) gCurrentLevel, (int) p->state, (int) p->csState, (int) now[2],
                (int) now[3], (int) p->draw, (int) p->shields, (int) gLifeCount[0]);
    }
}

static bool CoopEscapeProbe_EveryoneFlies(void) {
    s32 i;

    for (i = 0; i < CoopPlayers_Count(); i++) {
        if (!CoopEscapeProbe_Flies(i)) {
            return false;
        }
    }
    return true;
}

static void CoopEscapeProbe_OnGamePostUpdate(IEvent* event) {
    bool inEscape;
    bool over;

    if ((gPlayer == NULL) || !CoopPlayers_IsCoop()) {
        return;
    }
    if (sEscEver && ((s32) gGameState != sEscGameState)) {
        fprintf(sEscFile, "escape gamestate frame=%d from=%d to=%d\n", (int) gGameFrameCount, (int) sEscGameState,
                (int) gGameState);
        fflush(sEscFile);
    }
    sEscGameState = gGameState;
    if ((gGameState != GSTATE_PLAY) || ((s32) gGameFrameCount == sEscLastFrame)) {
        return; // not in a level, or no game frame ran since the last call
    }
    sEscLastFrame = gGameFrameCount;
    inEscape = CoopEscapeProbe_InEscape();
    if (inEscape && !sEscInEscape) {
        CoopEscapeProbe_RunStart();
    }
    over = sEscInEscape && !inEscape;
    if (sEscEver) {
        CoopEscapeProbe_SayPlayers();
    }
    if (over) {
        // The trigger moves the level to Venom 2's id on the frame it fires (fox_andross.c:683-691).
        CoopEscapeProbe_RunEnd(gCurrentLevel == LEVEL_VENOM_2);
    }
    if (inEscape) {
        CoopEscapeProbe_Nearest();
    }
    if (sEscStage == ESCAPE_STAGE_RUNNING) {
        CoopEscapeProbe_Watch(over);
    } else if ((sEscStage == ESCAPE_STAGE_WAIT) && (sEscCase < sEscCaseCount) && inEscape) {
        if (sEscCases[sEscCase].target >= CoopPlayers_Count()) {
            fprintf(sEscFile, "escape case=%s skipped: no player %d\n", sEscCases[sEscCase].name,
                    (int) sEscCases[sEscCase].target);
            sEscCase++;
        } else if (CoopEscapeProbe_EveryoneFlies()) {
            if (sEscNext < 0) {
                sEscNext = gGameFrameCount + ESCAPE_START_DELAY;
            } else if ((s32) gGameFrameCount >= sEscNext) {
                CoopEscapeProbe_Begin();
            }
        }
    }
    sEscInEscape = inEscape;
}

static void CoopEscapeProbe_ReadCases(const char* text) {
    const char* at = text;

    while ((at != NULL) && (*at != '\0') && (sEscCaseCount < ESCAPE_CASES_MAX)) {
        EscapeCase* c = &sEscCases[sEscCaseCount];
        size_t len = strcspn(at, ",");
        EscapeKind kind = (strncmp(at, "blast", 5) == 0)  ? ESCAPE_KIND_BLAST
                          : (strncmp(at, "miss", 4) == 0) ? ESCAPE_KIND_MISS
                                                          : ESCAPE_KIND_EXIT;
        size_t word = (kind == ESCAPE_KIND_BLAST)                                     ? 5
                      : ((kind == ESCAPE_KIND_MISS) || (strncmp(at, "exit", 4) == 0)) ? 4
                                                                                      : 0;

        if ((word > 0) && (len > word) && (len < sizeof(c->name))) {
            memcpy(c->name, at, len);
            c->name[len] = '\0';
            c->kind = kind;
            c->target = atoi(at + word);
            if ((c->target >= 0) && (c->target < COOP_PLAYERS_MAX)) {
                sEscCaseCount++;
            }
        }
        at += len;
        if (*at == ',') {
            at++;
        }
    }
    if (sEscCaseCount == 0) {
        sEscStage = ESCAPE_STAGE_DONE;
    }
}

void CoopEscapeProbe_Init(void) {
    const char* path = getenv("SF64_COOP_ESCAPE_PROBE");

    if ((path == NULL) || (path[0] == '\0')) {
        return;
    }
    sEscFile = fopen(path, "w");
    if (sEscFile == NULL) {
        return;
    }
    CoopEscapeProbe_ReadCases(getenv("SF64_COOP_ESCAPE_CASES"));
    REGISTER_LISTENER(GamePostUpdateEvent, CoopEscapeProbe_OnGamePostUpdate, EVENT_PRIORITY_NORMAL);
    REGISTER_LISTENER(ObjectUpdateEvent, CoopEscapeProbe_OnObjectUpdate, EVENT_PRIORITY_NORMAL);
}
