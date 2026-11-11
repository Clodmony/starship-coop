// The co-op log in the game, see log_game.h.

#include "log_game.h"

#ifdef USE_NETWORKING

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "global.h"
#include "port/coop/game/app_folder.h"
#include "port/coop/game/players.h"
#include "port/coop/sync/desync_report.h"
#include "port/coop/sync/lockstep_game.h"
#include "port/hooks/Events.h"
#include "public/bridge/consolevariablebridge.h"
#include "coop_log.h"
#include "log_spdlog.h"

#define STATE_LINE_MS 1000

static int32_t sGameState = -1;
static int32_t sPlayState = -1;
static int32_t sLevel = -1;
static int32_t sMode = -1;
static int32_t sPlayerState[COOP_PLAYERS_MAX];
static int32_t sParked[COOP_PLAYERS_MAX];
static int32_t sFoxRefills = 0; // CoopPlayers_FoxRefillCount as last seen
static int32_t sLeadSwaps = 0;  // CoopPlayers_LeadSwapCount as last seen
static uint32_t sStateLineMs = 0;
static bool sTracking = false;

static void Folder(char* out, size_t cap) {
    CoopAppFolder_Path(out, cap);
}

static int32_t Frame(void) {
    return CoopSyncGame_Frame();
}

static void Level(char* out, size_t cap) {
    char buffer[32];
    const char* name;
    size_t i;

    if ((gGameState != GSTATE_PLAY) && (gGameState != GSTATE_MAP)) {
        out[0] = '\0';
        return;
    }
    if (gGameState == GSTATE_MAP) {
        snprintf(out, cap, "map");
        return;
    }
    name = CoopDesyncReport_LevelName(gCurrentLevel, buffer, sizeof(buffer));
    snprintf(out, cap, "%s", name);
    for (i = 0; out[i] != '\0'; i++) {
        if (out[i] == ' ') {
            out[i] = '_';
        } else if ((out[i] == '(') || (out[i] == ')')) {
            out[i] = '-';
        }
    }
}

static const CoopLogContext sContext = { Folder, Frame, Level };

static const char* GameState_Name(int32_t state) {
    switch (state) {
        case GSTATE_NONE:
            return "none";
        case GSTATE_INIT:
            return "init";
        case GSTATE_TITLE:
            return "title";
        case GSTATE_MENU:
            return "menu";
        case GSTATE_MAP:
            return "map";
        case GSTATE_GAME_OVER:
            return "game_over";
        case GSTATE_VS_INIT:
            return "vs_init";
        case GSTATE_PLAY:
            return "play";
        case GSTATE_ENDING:
            return "ending";
        default:
            return "boot";
    }
}

static const char* PlayState_Name(int32_t state) {
    switch (state) {
        case PLAY_STANDBY:
            return "standby";
        case PLAY_INIT:
            return "init";
        case PLAY_UPDATE:
            return "update";
        case PLAY_PAUSE:
            return "pause";
        default:
            return "?";
    }
}

static const char* PlayerState_Name(int32_t state) {
    static const char* names[] = { "standby",     "init",          "intro",    "active",    "down",
                                   "u_turn",      "next",          "complete", "warp_zone", "start_360",
                                   "gfox_repair", "andross_mouth", "state_12", "vs_standby" };

    return ((state >= 0) && (state < (int32_t) (sizeof(names) / sizeof(names[0])))) ? names[state] : "?";
}

static void Track_Reset(void) {
    int32_t i;

    sGameState = gGameState;
    sPlayState = gPlayState;
    sLevel = gCurrentLevel;
    sMode = gLevelMode;
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        sPlayerState[i] = -1;
        sParked[i] = -1;
    }
    sFoxRefills = CoopPlayers_FoxRefillCount();
    sLeadSwaps = CoopPlayers_LeadSwapCount();
    sStateLineMs = SDL_GetTicks();
    sTracking = true;
}

static void Game_Changes(void) {
    char name[32];

    if ((int32_t) gGameState != sGameState) {
        CoopLog_Write(COOP_LOG_GAME, false, "game state %s -> %s", GameState_Name(sGameState),
                      GameState_Name(gGameState));
        sGameState = gGameState;
    }
    if (gPlayState != sPlayState) {
        if (gPlayState == PLAY_PAUSE) {
            CoopLog_Write(COOP_LOG_GAME, false, "paused");
        } else if (sPlayState == PLAY_PAUSE) {
            CoopLog_Write(COOP_LOG_GAME, false, "resumed");
        } else {
            CoopLog_Write(COOP_LOG_GAME, true, "play state %s -> %s", PlayState_Name(sPlayState),
                          PlayState_Name(gPlayState));
        }
        sPlayState = gPlayState;
    }
    if (gCurrentLevel != sLevel) {
        CoopLog_Write(COOP_LOG_GAME, false, "level %s, phase %d, %s",
                      CoopDesyncReport_LevelName(gCurrentLevel, name, sizeof(name)), (int) gLevelPhase,
                      CoopDesyncReport_ModeName(gLevelMode));
        sLevel = gCurrentLevel;
        sMode = gLevelMode;
    } else if ((int32_t) gLevelMode != sMode) {
        CoopLog_Write(COOP_LOG_GAME, false, "level mode %s -> %s", CoopDesyncReport_ModeName(sMode),
                      CoopDesyncReport_ModeName(gLevelMode));
        sMode = gLevelMode;
    }
}

// Players exist in a level only, and gPlayer is this level's array.
static bool Players_Readable(void) {
    return (gGameState == GSTATE_PLAY) && (gPlayState >= PLAY_UPDATE) && (gPlayer != NULL);
}

static void Player_Changes(void) {
    int32_t count = CoopPlayers_Count();
    int32_t i;

    if (!Players_Readable()) {
        return;
    }
    // Player 0 refilled in place for a life (decision 0007 rule 3).
    if (CoopPlayers_FoxRefillCount() > sFoxRefills) {
        CoopLog_Write(COOP_LOG_PLAYER, false, "player 1 refilled in place, pool %d", (int) gLifeCount[0]);
    }
    sFoxRefills = CoopPlayers_FoxRefillCount();
    if (CoopPlayers_LeadSwapCount() > sLeadSwaps) { // spike #183, test switch only
        CoopLog_Write(COOP_LOG_PLAYER, false, "lead swap: this game now flies slot %d",
                      (int) CoopPlayers_LocalIndex() + 1);
    }
    sLeadSwaps = CoopPlayers_LeadSwapCount();
    for (i = 0; (i < count) && (i < COOP_PLAYERS_MAX); i++) {
        int32_t parked = CoopPlayers_IsParked(i) ? 1 : 0;
        int32_t state = gPlayer[i].state;

        // Down (#14) or parked for another reason; the pool shows what a return cost.
        if ((parked != sParked[i]) && (sParked[i] >= 0)) {
            if (parked && CoopPlayers_IsDown(i)) {
                CoopLog_Write(COOP_LOG_PLAYER, false, "player %d down, pool %d", (int) i + 1, (int) gLifeCount[0]);
            } else if (parked) {
                CoopLog_Write(COOP_LOG_PLAYER, false, "player %d parked", (int) i + 1);
            } else {
                CoopLog_Write(COOP_LOG_PLAYER, false, "player %d flies, pool %d", (int) i + 1, (int) gLifeCount[0]);
            }
        }
        sParked[i] = parked;
        if (state != sPlayerState[i]) {
            if (sPlayerState[i] >= 0) {
                CoopLog_Write(COOP_LOG_PLAYER,
                              (state != PLAYERSTATE_DOWN) && (sPlayerState[i] != PLAYERSTATE_DOWN) &&
                                  (state != PLAYERSTATE_ACTIVE) && (state != PLAYERSTATE_LEVEL_COMPLETE),
                              "player %d %s -> %s", (int) i + 1, PlayerState_Name(sPlayerState[i]),
                              PlayerState_Name(state));
            }
            sPlayerState[i] = state;
        }
    }
}

static void State_Lines(uint32_t nowMs) {
    int32_t count;
    int32_t i;

    if (nowMs - sStateLineMs < STATE_LINE_MS) {
        return;
    }
    sStateLineMs = nowMs;
    if (!Players_Readable()) {
        return;
    }
    count = CoopPlayers_Count();
    for (i = 0; (i < count) && (i < COOP_PLAYERS_MAX); i++) {
        const Player* p = &gPlayer[i];

        CoopLog_Write(COOP_LOG_STATE, false, "player %d %s%s shields %d pos %.0f %.0f %.0f path %.0f speed %.1f",
                      (int) i + 1, PlayerState_Name(p->state), CoopPlayers_IsParked(i) ? " parked" : "",
                      (int) p->shields, p->pos.x, p->pos.y, p->trueZpos, p->pos.z, p->baseSpeed);
    }
}

static void Game_Log(void) {
    char line[COOP_LOG_LINE];
    int level;

    while (CoopLogSpdlog_Take(line, sizeof(line), &level)) {
        CoopLog_Write(level >= 4 ? COOP_LOG_ERROR : COOP_LOG_WARN, false, "%s", line);
    }
}

static void CoopLogGame_OnGamePreUpdate(IEvent* event) {
    uint32_t nowMs;

    (void) event;
    if (!CoopLog_IsOpen()) {
        if (sTracking) {
            sTracking = false;
            CoopLogSpdlog_Drop();
        }
        return;
    }
    if (!sTracking) {
        Track_Reset();
    }
    nowMs = SDL_GetTicks();
    Game_Log();
    Game_Changes();
    Player_Changes();
    State_Lines(nowMs);
}

// The game is quitting with a session open: the closing line still gets written, with nothing sent. Every line before
// it is on disk already, as the log flushes each one.
static void CoopLogGame_AtExit(void) {
    CoopLog_End(SDL_GetTicks(), NULL, NULL);
}

void CoopLogGame_Init(void) {
    const char* mode = getenv("SF64_COOP_LOG");
    CoopLogLevel level = COOP_LOG_QUIET;

    if ((mode != NULL) && (strcmp(mode, "off") == 0)) {
        level = COOP_LOG_OFF;
    } else if (((mode != NULL) && (strcmp(mode, "verbose") == 0)) || (CVarGetInteger("gCoopLogVerbose", 0) != 0)) {
        level = COOP_LOG_VERBOSE;
    }
    CoopLog_SetLevel(level);
    CoopLog_SetContext(&sContext);
    atexit(CoopLogGame_AtExit);
    // LOW: after lockstep (HIGH) has settled whether this frame runs and what frame it is, and after the session.
    REGISTER_LISTENER(GamePreUpdateEvent, CoopLogGame_OnGamePreUpdate, EVENT_PRIORITY_LOW);
}

#else

void CoopLogGame_Init(void) {
}

#endif
