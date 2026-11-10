// State pokes for the tests of #210. See score_probe.h.

#include "score_probe.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "port/coop/game/players.h"
#include "port/coop/sync/lockstep_game.h"
#include "port/hooks/Events.h"

#define PROBE_KA_SAUCERER 1     // KA_BOSS_SAUCERER, src/overlays/ovl_i4/fox_ka.c
#define PROBE_KA_FIGHT 6        // SAUCERER_OPEN_HATCHES, the state of the fight
#define PROBE_KA_HATCH_HP 10    // BOSS_HATCH_1_HP .. BOSS_HATCH_4_HP, then BOSS_CORE_HP
#define PROBE_KA_CORE_TIMER 16  // BOSS_CORE_TIMER
#define PROBE_CO_WING_SCRIPT 39 // CO S39: EVC_WING_BROKEN makes a wing repair item
#define PROBE_DROP_LASERS_AT 14 // gDropHitCountItem: the next kill in all-range drops lasers (Actor_Despawn)
#define PROBE_STEP 20           // frames between two pokes of one kind
#define PROBE_SECOND 60         // frame of the second poke of `lives` and `wing`

typedef enum ProbeKind { PROBE_NONE, PROBE_KILL, PROBE_LIVES, PROBE_WING, PROBE_SAUCERER } ProbeKind;

static FILE* sReport = NULL;
static ProbeKind sKind = PROBE_NONE;
static s32 sFrame = -1;
static s32 sPlayer = 1;
static s32 sLastFrame = -1;

static s32 sKillSlot = -1; // the enemy of the last kill poke, and what it saw before
static s32 sKillSource = 0;
static s32 sKillHits = 0;
static s32 sKillDrop = 0;
static s32 sKillLasers = 0;

static s32 sLivesSeen[3] = { -1, -1, -1 };

static s32 sEventSlot[2] = { -1, -1 };
static s32 sEventSeen[2][2] = { { -1, -1 }, { -1, -1 } };

static s32 sHatchSeen[6] = { -1, -1, -1, -1, -1, -1 };
static s32 sFightFrame = -1; // the frame the Saucerer was put into its fight

static void Report(const char* format, ...) {
    va_list args;

    va_start(args, format);
    vfprintf(sReport, format, args);
    va_end(args);
    fputc('\n', sReport);
    fflush(sReport); // tests stop the game from outside
}

static void Report_Players(s32 frame) {
    Report("players frame=%d states=%d,%d parked=%d,%d", (int) frame, (int) gPlayer[0].state,
           (int) ((CoopPlayers_Count() > 1) ? gPlayer[1].state : -1), (int) CoopPlayers_IsParked(0),
           (int) CoopPlayers_IsParked(1));
}

// Player 0 and <player> fly: ACTIVE or in a U-turn, and <player> is not parked.
static s32 Probe_BothFly(void) {
    s32 i;

    for (i = 0; i < 2; i++) {
        const Player* p = &gPlayer[(i == 0) ? 0 : sPlayer];

        if (((p->state != PLAYERSTATE_ACTIVE) && (p->state != PLAYERSTATE_U_TURN)) ||
            CoopPlayers_IsParked((i == 0) ? 0 : sPlayer)) {
            return 0;
        }
    }
    return 1;
}

static Actor* Probe_FreeActor(s32 lowest) {
    s32 i;

    for (i = ARRAY_COUNT(gActors) - 1; i >= lowest; i--) {
        if (gActors[i].obj.status == OBJ_FREE) {
            return &gActors[i];
        }
    }
    return NULL;
}

static Item* Probe_FreeItem(void) {
    s32 i;

    for (i = 0; i < ARRAY_COUNT(gItems); i++) {
        if (gItems[i].obj.status == OBJ_FREE) {
            return &gItems[i];
        }
    }
    return NULL;
}

static s32 Probe_LaserItems(void) {
    s32 count = 0;
    s32 i;

    for (i = 0; i < ARRAY_COUNT(gItems); i++) {
        if ((gItems[i].obj.status != OBJ_FREE) && (gItems[i].obj.id == OBJ_ITEM_LASERS)) {
            count++;
        }
    }
    return count;
}

// kill: an all-range enemy on its last dying frame, credited to `dmgSource`, well away from everyone.
static void Kill_Place(s32 frame, s32 dmgSource) {
    Actor* enemy = Probe_FreeActor(AI360_ENEMY + 10);

    if (enemy == NULL) {
        Report("error no free actor");
        return;
    }
    Actor_Initialize(enemy);
    enemy->index = enemy - gActors;
    enemy->obj.status = OBJ_DYING;
    enemy->obj.id = OBJ_ACTOR_ALLRANGE;
    Object_SetInfo(&enemy->info, enemy->obj.id);
    enemy->aiType = AI360_ENEMY;
    enemy->obj.pos.x = gPlayer[0].pos.x;
    enemy->obj.pos.y = gPlayer[0].pos.y + 1500.0f;
    enemy->obj.pos.z = gPlayer[0].trueZpos - 3000.0f;
    enemy->timer_0BE = 2; // Actor_Update counts it to 1, and func_enmy2_800763A4 ends it there
    enemy->dmgSource = dmgSource;
    if ((dmgSource == CoopPlayers_DmgSource(sPlayer)) && (gLevelMode == LEVELMODE_ALL_RANGE)) {
        gDropHitCountItem = PROBE_DROP_LASERS_AT;
    }
    sKillSlot = enemy->index;
    sKillSource = dmgSource;
    sKillHits = gHitCount;
    sKillDrop = gDropHitCountItem;
    sKillLasers = Probe_LaserItems();
    Report_Players(frame);
}

static void Kill_Update(s32 frame) {
    s32 step;

    if ((sKillSlot >= 0) && (frame > sLastFrame)) {
        Report("kill frame=%d source=%d hits=%d,%d drop=%d,%d lasers=%d,%d gone=%d", (int) sLastFrame,
               (int) sKillSource, (int) sKillHits, (int) gHitCount, (int) sKillDrop, (int) gDropHitCountItem,
               (int) sKillLasers, (int) Probe_LaserItems(), (int) (gActors[sKillSlot].obj.status == OBJ_FREE));
        sKillSlot = -1;
    }
    for (step = 0; step < 4; step++) { // the player, DMG_SRC_2, Falco, player 0
        if (frame != sFrame + (step * PROBE_STEP)) {
            continue;
        }
        switch (step) {
            case 0:
                Kill_Place(frame, CoopPlayers_DmgSource(sPlayer));
                break;
            case 1:
                Kill_Place(frame, DMG_SRC_2);
                break;
            case 2:
                Kill_Place(frame, CoopPlayers_DmgSource(NPC_SHOT_ID + AI360_FALCO));
                break;
            case 3:
                Kill_Place(frame, CoopPlayers_DmgSource(0));
                break;
        }
    }
}

// lives: a gold ring or a 1-up where the player will be on the next frame (players move before objects).
static void Lives_Place(s32 frame, ObjectId id) {
    Player* p = &gPlayer[sPlayer];
    Item* item = Probe_FreeItem();

    if (item == NULL) {
        Report("error no free item");
        return;
    }
    Item_Initialize(item);
    item->obj.status = OBJ_ACTIVE;
    item->obj.id = id;
    Object_SetInfo(&item->info, item->obj.id);
    item->obj.pos.x = p->pos.x + p->vel.x;
    item->obj.pos.y = p->pos.y + p->vel.y;
    item->obj.pos.z = p->trueZpos + p->vel.z;
    item->unk_50 = 90.0f; // as Actor_Despawn's drops
    item->timer_48 = 1000;
    item->width = 1.0f;
    Report_Players(frame);
}

static void Lives_Update(s32 frame) {
    s32 now[3];

    if (frame == sFrame) {
        gGoldRingCount[0] = 5;
        Lives_Place(frame, OBJ_ITEM_GOLD_RING);
    } else if (frame == sFrame + PROBE_SECOND) {
        Lives_Place(frame, OBJ_ITEM_1UP);
    }
    now[0] = gGoldRingCount[0];
    now[1] = gLifeCount[0];
    now[2] = gLifeCount[sPlayer];
    if ((frame >= sFrame) && (memcmp(now, sLivesSeen, sizeof(now)) != 0)) {
        memcpy(sLivesSeen, now, sizeof(now));
        Report("lives frame=%d rings=%d pool=%d slot=%d", (int) frame, (int) now[0], (int) now[1], (int) now[2]);
    }
}

// wing: an event actor that runs CO S39, as the level's object list loads one (Object_Load, ActorEvent_Load), above
// and ahead of player 0. gPrevEventActorIndex is put back, so the next event actor of the level links as before.
static void Wing_Place(s32 frame, s32 n) {
    Actor* actor = Probe_FreeActor(0);
    ObjectInit init;
    s32 prev = gPrevEventActorIndex;

    if (actor == NULL) {
        Report("error no free actor");
        return;
    }
    memset(&init, 0, sizeof(init));
    init.id = ACTOR_EVENT_ID + PROBE_CO_WING_SCRIPT;
    init.xPos = (s16) gPlayer[0].pos.x;
    init.yPos = (s16) (gPlayer[0].pos.y + 600.0f);
    init.zPos1 = -(gPlayer[0].trueZpos - 2000.0f) - 3000.0f; // pos.z = -zPos1 - 3000 + zPos2
    sEventSlot[n] = actor - gActors;
    ActorEvent_Load(actor, &init, sEventSlot[n]);
    gPrevEventActorIndex = prev;
    Report_Players(frame);
}

static void Wing_Update(s32 frame) {
    s32 n;

    if (frame == sFrame) {
        gPlayer[sPlayer].arwing.rightWingState = gPlayer[sPlayer].arwing.leftWingState = WINGSTATE_BROKEN;
        Wing_Place(frame, 0);
    } else if (frame == sFrame + PROBE_SECOND) {
        for (n = 0; n < CoopPlayers_Count(); n++) {
            gPlayer[n].arwing.rightWingState = gPlayer[n].arwing.leftWingState = WINGSTATE_INTACT;
        }
        Wing_Place(frame, 1);
    }
    for (n = 0; n < 2; n++) {
        const Actor* actor;

        if (sEventSlot[n] < 0) {
            continue;
        }
        actor = &gActors[sEventSlot[n]];
        if ((actor->eventType != sEventSeen[n][0]) || (actor->obj.status != sEventSeen[n][1])) {
            sEventSeen[n][0] = actor->eventType;
            sEventSeen[n][1] = actor->obj.status;
            Report("event frame=%d n=%d type=%d status=%d", (int) frame, (int) n + 1, (int) actor->eventType,
                   (int) actor->obj.status);
        }
        if (actor->obj.status == OBJ_FREE) {
            sEventSlot[n] = -1;
        }
    }
}

// saucerer: a single laser of `player` inside hitbox `box` of the Saucerer, standing still. Where the box is follows
// PlayerShot_CheckObjectHitbox backwards: that turns the shot into the boss's frame, so the box's centre is turned out.
static void Saucerer_Shoot(s32 frame, Boss* boss, s32 player, s32 box) {
    PlayerShot* shot = NULL;
    f32* data = boss->info.hitbox;
    f32 rot[3] = { 0.0f, 0.0f, 0.0f };
    Vec3f local;
    Vec3f world;
    s32 i;

    for (i = 0; i < ARRAY_COUNT(gPlayerShots) - 4; i++) { // the last ones are lock-on and bomb slots
        if (gPlayerShots[i].obj.status == SHOT_FREE) {
            shot = &gPlayerShots[i];
            break;
        }
    }
    if ((shot == NULL) || (data == NULL) || (box >= (s32) data[0])) {
        Report("error no shot slot or no hitbox %d", (int) box);
        return;
    }
    data++;
    for (i = 0;; i++) {
        if (data[0] == HITBOX_ROTATED) {
            rot[0] = data[1];
            rot[1] = data[2];
            rot[2] = data[3];
            data += 4;
        } else {
            rot[0] = rot[1] = rot[2] = 0.0f;
        }
        if (i == box) {
            break;
        }
        data += 6;
    }
    local.z = data[0]; // Hitbox: z, y, x offsets with their sizes between
    local.y = data[2];
    local.x = data[4];
    Matrix_RotateY(gCalcMatrix, boss->obj.rot.y * M_DTOR, MTXF_NEW);
    Matrix_RotateX(gCalcMatrix, boss->obj.rot.x * M_DTOR, MTXF_APPLY);
    Matrix_RotateZ(gCalcMatrix, boss->obj.rot.z * M_DTOR, MTXF_APPLY);
    Matrix_RotateY(gCalcMatrix, rot[1] * M_DTOR, MTXF_APPLY);
    Matrix_RotateX(gCalcMatrix, rot[0] * M_DTOR, MTXF_APPLY);
    Matrix_RotateZ(gCalcMatrix, rot[2] * M_DTOR, MTXF_APPLY);
    Matrix_MultVec3fNoTranslate(gCalcMatrix, &local, &world);

    PlayerShot_Initialize(shot);
    shot->obj.status = SHOT_ACTIVE;
    shot->obj.id = PLAYERSHOT_SINGLE_LASER;
    shot->obj.pos.x = boss->obj.pos.x + world.x;
    shot->obj.pos.y = boss->obj.pos.y + world.y;
    shot->obj.pos.z = boss->obj.pos.z + world.z;
    shot->timer = 35;
    shot->sourceId = player;
    Report("shot frame=%d player=%d box=%d slot=%d at=%.1f,%.1f,%.1f", (int) frame, (int) player, (int) box,
           (int) (shot - gPlayerShots), shot->obj.pos.x, shot->obj.pos.y, shot->obj.pos.z);
}

static void Saucerer_Update(s32 frame) {
    Boss* boss = &gBosses[PROBE_KA_SAUCERER];
    s32 now[6];

    if ((gCurrentLevel != LEVEL_KATINA) || (boss->obj.id != OBJ_BOSS_KA_SAUCERER)) {
        return;
    }
    if ((sFightFrame < 0) && (frame >= sFrame) && Probe_BothFly()) {
        // As Katina_UpdateEvents does after a restart from the checkpoint (src/overlays/ovl_i4/fox_ka.c).
        sFightFrame = frame;
        boss->state = PROBE_KA_FIGHT;
        boss->obj.pos.x = 0.0f;
        boss->obj.pos.z = 0.0f;
        boss->obj.pos.y = 2000.0f;
        boss->swork[PROBE_KA_CORE_TIMER] = 5760;
        gAllRangeEventTimer = 20000;
        Report_Players(frame);
    } else if ((sFightFrame >= 0) && (frame == sFightFrame + 2)) {
        Saucerer_Shoot(frame, boss, sPlayer, 0);
    } else if ((sFightFrame >= 0) && (frame == sFightFrame + 2 + PROBE_STEP)) {
        // Not on the same frame: the boss keeps one dmgPart, the last shot's (PlayerShot_CollisionCheck).
        Saucerer_Shoot(frame, boss, 0, 1);
    }
    now[0] = boss->state;
    now[1] = boss->swork[PROBE_KA_HATCH_HP];
    now[2] = boss->swork[PROBE_KA_HATCH_HP + 1];
    now[3] = boss->swork[PROBE_KA_HATCH_HP + 2];
    now[4] = boss->swork[PROBE_KA_HATCH_HP + 3];
    now[5] = boss->swork[PROBE_KA_HATCH_HP + 4];
    if ((sFightFrame >= 0) && (memcmp(now, sHatchSeen, sizeof(now)) != 0)) {
        memcpy(sHatchSeen, now, sizeof(now));
        Report("hatch frame=%d state=%d hp=%d,%d,%d,%d core=%d", (int) frame, (int) now[0], (int) now[1], (int) now[2],
               (int) now[3], (int) now[4], (int) now[5]);
    }
}

static void CoopScoreProbe_OnGamePreUpdate(IEvent* event) {
    s32 frame = CoopSyncGame_Frame();

    (void) event;
    if (frame < 0) {
        frame = (s32) gGameFrameCount; // a game outside lockstep
    }
    if ((gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE) || (gPlayer == NULL) ||
        (sPlayer >= CoopPlayers_Count())) {
        sLastFrame = frame;
        return;
    }
    switch (sKind) {
        case PROBE_KILL:
            Kill_Update(frame);
            break;
        case PROBE_LIVES:
            Lives_Update(frame);
            break;
        case PROBE_WING:
            Wing_Update(frame);
            break;
        case PROBE_SAUCERER:
            Saucerer_Update(frame);
            break;
        case PROBE_NONE:
            break;
    }
    sLastFrame = frame;
}

void CoopScoreProbe_Init(void) {
    static const char* names[] = { "", "kill", "lives", "wing", "saucerer" };
    const char* probe = getenv("SF64_COOP_SCORE_PROBE");
    const char* report = getenv("SF64_COOP_SCORE_REPORT");
    const char* colon;
    s32 i;

    if ((probe == NULL) || (probe[0] == '\0') || (report == NULL) || (report[0] == '\0')) {
        return;
    }
    colon = strchr(probe, ':');
    for (i = 1; i < ARRAY_COUNT(names); i++) {
        if ((colon != NULL) && (strlen(names[i]) == (size_t) (colon - probe)) &&
            (strncmp(probe, names[i], colon - probe) == 0)) {
            sKind = (ProbeKind) i;
        }
    }
    if ((sKind == PROBE_NONE) || ((sReport = fopen(report, "w")) == NULL)) {
        return;
    }
    sFrame = atoi(colon + 1);
    if (strchr(colon + 1, ':') != NULL) {
        sPlayer = atoi(strchr(colon + 1, ':') + 1);
    }
    if ((sPlayer < 0) || (sPlayer >= COOP_PLAYERS_MAX)) {
        sPlayer = 1;
    }
    Report("probe kind=%s frame=%d player=%d", names[sKind], (int) sFrame, (int) sPlayer);
    // LOW, as the lockstep driver: its frame number is the one the feed and the item pokes count in.
    REGISTER_LISTENER(GamePreUpdateEvent, CoopScoreProbe_OnGamePreUpdate, EVENT_PRIORITY_LOW);
}
