// Input for a game in a session, for tests (#20). See lockstep_driver.h.

#include "lockstep_driver.h"

#ifdef USE_NETWORKING

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "global.h"
#include "pilot.h"
#include "port/coop/game/input.h"
#include "port/coop/game/players.h"
#include "port/coop/sync/lockstep_game.h"
#include "port/coop/sync/state_hash.h"
#include "port/hooks/Events.h"

// src/sys/sys_math.c
extern s32 sRandSeed1;
extern s32 sRandSeed2;
extern s32 sRandSeed3;

#define DRIVER_RULE_START 60 // as hash_check.py: input by rule presses A every 30 frames from here on
#define DRIVER_RULE_PERIOD 30
#define DRIVER_ALIVE_PERIOD 120
#define DRIVER_LINE_SIZE 256
#define ITEM_SETUP_BREAK_0 (1 << 0)      // SF64_COOP_LOCKSTEP_ITEM setup: player 0's wings are broken
#define ITEM_SETUP_BREAK_1 (1 << 1)      // player 1's wings are broken
#define ITEM_SETUP_TWIN_0 (1 << 2)       // player 0 has twin lasers
#define ITEM_SETUP_NO_WING_HP_1 (1 << 3) // player 1's wing health is 0

typedef struct {
    s16 hold[3];
    s16 press[3];
} FeedPad;

typedef struct {
    FeedPad pad[COOP_PLAYERS_MAX];
} FeedRow;

static FILE* sReport = NULL;
static FILE* sLog = NULL;
static FeedRow* sFeed = NULL;
static s32 sFeedRows = 0;
static bool sPilot = false;
static s32 sEndFrame = -1;
static s32 sMuteFrame = -1;
static s32 sPokeFrame = -1;
static s32 sZeroFrame = -1;
static s32 sZeroSeed = 0;
static s32 sSlowEvery = 0;
static s32 sSlowMs = 0;
static s32 sLastFrame = -1;
static s32 sOwnFrame = -1;
static s32 sFramesAfter = 0;
static bool sOverSaid = false;
static bool sStartSaid = false;
static s32 sItemFrame = -1; // SF64_COOP_LOCKSTEP_ITEM=<frame>:<player>
static s32 sItemPlayer = 0;
static bool sItemPlaced = false;
static s32 sItemSeen[COOP_PLAYERS_MAX * 4] = { -1 };
static s32 sItemKind = 0;
static s32 sItemSetup = 0;
static bool sItemDropSaid = false;

static void Report(const char* format, ...) {
    va_list args;

    if (sReport == NULL) {
        return;
    }
    va_start(args, format);
    vfprintf(sReport, format, args);
    va_end(args);
    fputc('\n', sReport);
    fflush(sReport);
}

typedef enum ItemKind { ITEM_KIND_LASERS, ITEM_KIND_REPAIR, ITEM_KIND_CRATE } ItemKind;

static void Item_BreakWings(s32 player) {
    gPlayer[player].arwing.rightWingState = gPlayer[player].arwing.leftWingState = WINGSTATE_BROKEN;
}

// SF64_COOP_LOCKSTEP_ITEM, on its frame: the setup (which wings are broken, who has twin lasers), then a laser item, a
// wing repair or a supply crate about to burst, where the player will be on the next frame (players move before
// objects). From then on a line for every change of the lasers, the wings or their state of players 0 and 1, and, for
// a crate, one for the item it drops.
static void Item_Place(s32 frame) {
    Player* p = &gPlayer[sItemPlayer];
    s32 i;

    if (sItemSetup & ITEM_SETUP_BREAK_0) {
        Item_BreakWings(0);
    }
    if (sItemSetup & ITEM_SETUP_BREAK_1) {
        Item_BreakWings(1);
    }
    if (sItemSetup & ITEM_SETUP_TWIN_0) {
        gLaserStrength[0] = LASERS_TWIN;
    }
    if (sItemSetup & ITEM_SETUP_NO_WING_HP_1) {
        gLeftWingHealth[1] = gRightWingHealth[1] = 0;
    }
    if (sItemKind == ITEM_KIND_CRATE) {
        Actor* crate = NULL;

        for (i = ARRAY_COUNT(gActors) - 1; i >= 50; i--) {
            if (gActors[i].obj.status == OBJ_FREE) {
                crate = &gActors[i];
                break;
            }
        }
        if (crate == NULL) {
            return;
        }
        Actor_Initialize(crate);
        crate->obj.status = OBJ_ACTIVE;
        crate->obj.id = OBJ_ACTOR_SUPPLIES;
        Object_SetInfo(&crate->info, OBJ_ACTOR_SUPPLIES);
        crate->obj.pos.x = p->pos.x + p->vel.x;
        crate->obj.pos.y = p->pos.y + p->vel.y;
        crate->obj.pos.z = p->trueZpos + p->vel.z;
        crate->health = 1;
        crate->damage = 10;
        crate->dmgType = DMG_BEAM;
    } else {
        Item* item = NULL;

        for (i = 0; i < ARRAY_COUNT(gItems); i++) {
            if (gItems[i].obj.status == OBJ_FREE) {
                item = &gItems[i];
                break;
            }
        }
        if (item == NULL) {
            return;
        }
        Item_Initialize(item);
        item->obj.status = OBJ_ACTIVE;
        item->obj.id = (sItemKind == ITEM_KIND_REPAIR) ? OBJ_ITEM_WING_REPAIR : OBJ_ITEM_LASERS;
        Object_SetInfo(&item->info, item->obj.id);
        item->obj.pos.x = p->pos.x + p->vel.x;
        item->obj.pos.y = p->pos.y + p->vel.y;
        item->obj.pos.z = p->trueZpos + p->vel.z;
        item->unk_50 = 90.0f;
        if (sItemKind == ITEM_KIND_REPAIR) {
            item->timer_48 = 1000;
        }
    }
    sItemPlaced = true;
    Report("item placed frame=%d player=%d kind=%d", (int) frame, (int) sItemPlayer, (int) sItemKind);
}

static void Driver_Item(s32 frame) {
    s32 now[COOP_PLAYERS_MAX * 4];
    s32 i;

    if ((sItemFrame < 0) || (gGameState != GSTATE_PLAY)) {
        return;
    }
    if (!sItemPlaced && (frame == sItemFrame)) {
        Item_Place(frame);
    }
    if (!sItemPlaced) {
        return;
    }
    if (!sItemDropSaid && (sItemKind == ITEM_KIND_CRATE) && (frame > sItemFrame)) {
        for (i = 0; i < ARRAY_COUNT(gItems); i++) {
            if (gItems[i].obj.status != OBJ_FREE) {
                sItemDropSaid = true;
                Report("drop frame=%d id=%d lasers=%d", (int) frame, (int) gItems[i].obj.id,
                       (int) (gItems[i].obj.id == OBJ_ITEM_LASERS));
                break;
            }
        }
    }
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        now[i * 4] = gLaserStrength[i];
        now[i * 4 + 1] = gLeftWingHealth[i];
        now[i * 4 + 2] = gRightWingHealth[i];
        now[i * 4 + 3] = (gPlayer[i].arwing.leftWingState * 10) + gPlayer[i].arwing.rightWingState;
    }
    if (memcmp(now, sItemSeen, sizeof(now)) != 0) {
        memcpy(sItemSeen, now, sizeof(now));
        Report("lasers frame=%d strength=%d,%d wings=%d,%d states=%d,%d", (int) frame, (int) now[0], (int) now[4],
               (int) now[1], (int) now[5], (int) now[3], (int) now[7]);
    }
}

static void Feed_Load(const char* path) {
    FILE* f = fopen(path, "r");
    char line[DRIVER_LINE_SIZE];
    s32 capacity = 0;

    if (f == NULL) {
        Report("error cannot read the feed");
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        FeedRow row;
        char* at = line;
        long frame = strtol(at, &at, 10);
        s32 i;
        s32 j;

        (void) strtol(at, &at, 10); // game state
        (void) strtol(at, &at, 10); // play state
        memset(&row, 0, sizeof(row));
        for (i = 0; i < COOP_PLAYERS_MAX; i++) {
            for (j = 0; j < 3; j++) {
                row.pad[i].hold[j] = (s16) strtol(at, &at, 10);
            }
            for (j = 0; j < 3; j++) {
                row.pad[i].press[j] = (s16) strtol(at, &at, 10);
            }
        }
        if (frame >= capacity) {
            s32 grown = (capacity != 0) ? capacity * 2 : 4096;

            while (frame >= grown) {
                grown *= 2;
            }
            sFeed = realloc(sFeed, sizeof(FeedRow) * grown);
            memset(&sFeed[capacity], 0, sizeof(FeedRow) * (grown - capacity));
            capacity = grown;
        }
        sFeed[frame] = row;
        if (frame >= sFeedRows) {
            sFeedRows = (s32) frame + 1;
        }
    }
    fclose(f);
}

// What this client's person does now. With a delay of d, a line of the feed has to go in d frames before its own frame.
static void Driver_Pad(s32 frame) {
    s32 self = CoopPlayers_SeatIndex(); // the feed's column is the session player's (#183)

    memset(&gControllerHold[0], 0, sizeof(gControllerHold[0]));
    memset(&gControllerPress[0], 0, sizeof(gControllerPress[0]));
    if (sPilot) {
        OSContPad hold[COOP_PLAYERS_MAX];
        OSContPad press[COOP_PLAYERS_MAX];

        memset(hold, 0, sizeof(hold));
        memset(press, 0, sizeof(press));
        if ((frame >= DRIVER_RULE_START) && ((frame % DRIVER_RULE_PERIOD) == 0)) {
            hold[0].button = press[0].button = A_BUTTON;
        }
        CoopPilot_Fly(hold, press);
        gControllerHold[0] = hold[self];
        gControllerPress[0] = press[self];
    } else if (sFeed != NULL) {
        s32 row = frame + (s32) CoopSyncGame_Delay();

        if (row < sFeedRows) {
            const FeedPad* pad = &sFeed[row].pad[self];

            gControllerHold[0].button = (u16) pad->hold[0];
            gControllerHold[0].stick_x = (s8) pad->hold[1];
            gControllerHold[0].stick_y = (s8) pad->hold[2];
            gControllerPress[0].button = (u16) pad->press[0];
            gControllerPress[0].stick_x = (s8) pad->press[1];
            gControllerPress[0].stick_y = (s8) pad->press[2];
        }
    }
}

static void Driver_Log(s32 frame) {
    CoopInputFrame input;
    s32 i;

    if (sLog == NULL) {
        return;
    }
    fprintf(sLog, "%d %d %d", (int) frame, (int) gGameState, (int) gPlayState);
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        CoopSyncGame_Read(i, &input);
        fprintf(sLog, " %u %d %d %u %d %d", input.hold.button, input.hold.stick_x, input.hold.stick_y,
                input.press.button, input.press.stick_x, input.press.stick_y);
    }
    fputc('\n', sLog);
    fflush(sLog);
}

// With no lockstep to count frames, the control run of the test counts them from the same place.
static s32 Driver_Frame(void) {
    s32 frame = CoopSyncGame_Frame();

    if (frame >= 0) {
        return frame;
    }
    if ((sOwnFrame < 0) && !CoopSyncGame_IsOn() && CoopPlayers_IsCoop() && (gGameState == GSTATE_PLAY) &&
        (gPlayState == PLAY_UPDATE) && (sLastFrame < 0)) {
        sOwnFrame = 0;
    }
    return (sOwnFrame >= 0) ? sOwnFrame++ : -1;
}

static void Driver_After(void) {
    if (sLastFrame < 0) {
        return;
    }
    if (!sOverSaid) {
        sOverSaid = true;
        Report("over frame=%d", (int) sLastFrame);
    }
    sFramesAfter++;
    if ((sFramesAfter % DRIVER_ALIVE_PERIOD) == 0) {
        Report("alive frames=%d", (int) sFramesAfter);
    }
}

static void CoopLockstepDriver_OnGamePreUpdate(IEvent* event) {
    s32 frame;

    (void) event;
    frame = Driver_Frame();
    if (frame < 0) {
        Driver_After();
        return;
    }
    if (!sStartSaid) {
        sStartSaid = true;
        Report("start");
    }
    if (CoopSyncGame_Frame() >= 0) {
        sLastFrame = frame;
        Driver_Log(frame);
    }
    Driver_Pad(frame);
    Driver_Item(frame);
    if (frame == sMuteFrame) {
        CoopSyncGame_Mute();
    }
    if (frame == sPokeFrame) {
        CoopStateHash_PokeNow();
    }
    if (frame == sZeroFrame) {
        *((sZeroSeed == 1) ? &sRandSeed1 : (sZeroSeed == 2) ? &sRandSeed2 : &sRandSeed3) = 0;
    }
    if ((sSlowEvery > 0) && ((frame % sSlowEvery) == 0)) {
        SDL_Delay((u32) sSlowMs);
    }
    if (frame == sEndFrame) {
        Report("done frame=%d held=%u held_ms=%u", (int) frame, (unsigned) CoopSyncGame_HeldFrames(),
               (unsigned) CoopSyncGame_HeldMs());
    }
}

static const char* Env_Text(const char* name) {
    const char* value = getenv(name);

    return ((value != NULL) && (value[0] != '\0')) ? value : NULL;
}

static s32 Env_Number(const char* name) {
    const char* value = Env_Text(name);

    return (value != NULL) ? atoi(value) : -1;
}

void CoopLockstepDriver_Init(void) {
    const char* report = Env_Text("SF64_COOP_LOCKSTEP_REPORT");
    const char* log = Env_Text("SF64_COOP_LOCKSTEP_LOG");
    const char* feed = Env_Text("SF64_COOP_LOCKSTEP_FEED");
    const char* slow = Env_Text("SF64_COOP_LOCKSTEP_SLOW");
    const char* zero = Env_Text("SF64_COOP_LOCKSTEP_ZERO_SEED");
    const char* item = Env_Text("SF64_COOP_LOCKSTEP_ITEM");

    if (report != NULL) {
        sReport = fopen(report, "w");
    }
    if (log != NULL) {
        sLog = fopen(log, "w");
    }
    if (feed != NULL) {
        Feed_Load(feed);
    }
    sPilot = Env_Text("SF64_COOP_LOCKSTEP_PILOT") != NULL;
    sEndFrame = Env_Number("SF64_COOP_LOCKSTEP_END");
    sMuteFrame = Env_Number("SF64_COOP_LOCKSTEP_MUTE");
    sPokeFrame = Env_Number("SF64_COOP_LOCKSTEP_POKE");
    if (slow != NULL) {
        sSlowEvery = atoi(slow);
        sSlowMs = (strchr(slow, ':') != NULL) ? atoi(strchr(slow, ':') + 1) : 0;
    }
    if (zero != NULL) {
        sZeroFrame = atoi(zero);
        sZeroSeed = (strchr(zero, ':') != NULL) ? atoi(strchr(zero, ':') + 1) : 1;
    }
    if (item != NULL) {
        sItemFrame = atoi(item);
        sItemPlayer = (strchr(item, ':') != NULL) ? atoi(strchr(item, ':') + 1) : 1;
        // <frame>:<player>:<kind>:<setup>, kind 0 lasers, 1 wing repair, 2 supply crate; setup a sum of the
        // ITEM_SETUP_*
        if ((strchr(item, ':') != NULL) && (strchr(strchr(item, ':') + 1, ':') != NULL)) {
            const char* rest = strchr(strchr(item, ':') + 1, ':') + 1;

            sItemKind = atoi(rest);
            sItemSetup = (strchr(rest, ':') != NULL) ? atoi(strchr(rest, ':') + 1) : 0;
        }
        memset(sItemSeen, 0xff, sizeof(sItemSeen));
    }
    if ((report == NULL) && (log == NULL) && (feed == NULL) && !sPilot) {
        return;
    }
    if (sPilot) {
        CoopPilot_Init();
    }
    // LOW: before the input layer reads port 0. Lockstep's frame is ready by then, as a frame that lacks input never
    // gets this far (CoopSyncGame_HoldFrame).
    REGISTER_LISTENER(GamePreUpdateEvent, CoopLockstepDriver_OnGamePreUpdate, EVENT_PRIORITY_LOW);
}

#else

void CoopLockstepDriver_Init(void) {
}

#endif
