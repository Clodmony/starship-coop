// Starts a test game straight in a level (#148). See start_level.h.

#include "start_level.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libultraship/luslog.h>

#include "global.h"
#include "port/coop/game/level_start.h"
#include "port/hooks/Events.h"

// src/overlays/ovl_menu/fox_map.c
void Map_Setup_Menu(void);
// src/engine/fox_play.c
extern u8 sSaveSlotFromLevel[20];

typedef enum {
    START_WAIT_TITLE,
    START_WAIT_LEVEL,
    START_PRESSED,
    START_FLYING,
    START_DONE,
} StartStep;

static StartStep sStep = START_DONE;
static s32 sLevel = -1;
static bool sSkipIntro = false;
static s32 sTitleFrames = 0;
static s32 sResetFrames = -1;
static s32 sFrames = 0;
static u8 sNormalClear = 0;
static char sName[32] = "menus";

// GSTATE_INIT has run by then: lives, button maps, the seeds of a replay. Map_Setup_Menu is what the map does for a new
// game, and CoopLevelStart_Enter the way into a level that does not care where it comes from.
static void CoopTestStart_Enter(void) {
    CoopLevelStart start;

    Map_Setup_Menu();
    CoopLevelStart_FromGame(&start, (uint8_t) sLevel, 0);
    CoopLevelStart_Enter(&start);
}

// The first frame of a level's intro. An intro only ends early by Start, on a planet the save has cleared
// (src/engine/fox_play.c, Play_Main), and that path has its own idea of what to reset, so it is pressed, not imitated.
static void CoopTestStart_PressStart(void) {
    u8 slot = sSaveSlotFromLevel[gCurrentLevel];

    sNormalClear = gSaveFile.save.data.planet[slot].normalClear;
    gSaveFile.save.data.planet[slot].normalClear = 1;
    gControllerPress[gMainController].button |= START_BUTTON;
}

// In the level, with or without its intro: on to the reset if one was asked for.
static StartStep CoopTestStart_AfterStart(void) {
    sFrames = 0;
    return (sResetFrames >= 0) ? START_FLYING : START_DONE;
}

// HIGH runs last: the replay has restored the seeds and the pads are in place.
static void CoopTestStart_OnGamePreUpdate(IEvent* event) {
    (void) event;
    switch (sStep) {
        case START_WAIT_TITLE:
            if ((gGameState == GSTATE_TITLE) && (sFrames++ >= sTitleFrames)) {
                CoopTestStart_Enter();
                sStep = sSkipIntro ? START_WAIT_LEVEL : CoopTestStart_AfterStart();
            }
            break;
        case START_WAIT_LEVEL:
            // Play_Init leaves player 0 in PLAYERSTATE_INIT, and its first update goes on from there.
            if ((gGameState == GSTATE_PLAY) && (gPlayState == PLAY_UPDATE) && (gPlayer[0].state != PLAYERSTATE_INIT)) {
                if (gPlayer[0].state == PLAYERSTATE_LEVEL_INTRO) {
                    CoopTestStart_PressStart();
                    sStep = START_PRESSED;
                } else {
                    sStep = START_DONE;
                }
            }
            break;
        case START_PRESSED:
            gSaveFile.save.data.planet[sSaveSlotFromLevel[gCurrentLevel]].normalClear = sNormalClear;
            sStep = CoopTestStart_AfterStart();
            break;
        case START_FLYING:
            if ((gGameState == GSTATE_PLAY) && (gPlayState == PLAY_UPDATE) && (sFrames++ >= sResetFrames)) {
                // What a hit leaves (src/engine/fox_play.c, Player_ApplyDamage) on a game that plays on afterwards.
                gRightWingHealth[0] = 40;
                gNextGameState = GSTATE_BOOT; // F4 (src/port/Engine.cpp, GameEngine::StartFrame)
                LUSLOG_INFO("coop: test switch: reset after %d frames in the level", sResetFrames);
                sStep = START_DONE;
            }
            break;
        default:
            break;
    }
}

const char* CoopTestStart_Name(void) {
    return sName;
}

void CoopTestStart_Init(void) {
    const char* level = getenv("SF64_COOP_TEST_START_LEVEL");
    const char* skipIntro = getenv("SF64_COOP_TEST_START_SKIP_INTRO");
    const char* titleFrames = getenv("SF64_COOP_TEST_START_TITLE_FRAMES");
    const char* reset = getenv("SF64_COOP_TEST_START_RESET");

    if ((level == NULL) || (level[0] == '\0')) {
        return;
    }
    sLevel = atoi(level);
    if ((sLevel < 0) || (sLevel >= COOP_LEVEL_START_LEVELS)) {
        LUSLOG_INFO("coop: test switch: no level %d to start in", sLevel);
        return;
    }
    sSkipIntro = (skipIntro != NULL) && (skipIntro[0] != '\0') && (strcmp(skipIntro, "0") != 0);
    sTitleFrames = ((titleFrames != NULL) && (titleFrames[0] != '\0')) ? atoi(titleFrames) : 0;
    sResetFrames = ((reset != NULL) && (reset[0] != '\0')) ? atoi(reset) : -1;
    snprintf(sName, sizeof(sName), "level:%d%s%s", sLevel, sSkipIntro ? ":skip-intro" : "",
             (sResetFrames >= 0) ? ":reset" : "");
    LUSLOG_INFO("coop: test switch: start in %s", sName);
    sStep = START_WAIT_TITLE;
    REGISTER_LISTENER(GamePreUpdateEvent, CoopTestStart_OnGamePreUpdate, EVENT_PRIORITY_HIGH);
}
