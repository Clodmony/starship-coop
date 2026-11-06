// Input recording and replay for tests (WP-03). See replay.h for how to switch it on.
//
// The port reads the controllers once per game frame in Graphics_ThreadUpdate (src/sys/sys_main.c):
// Controller_UpdateInput fills gControllerHold and gControllerPress, then Game_Update runs and fires
// GamePreUpdateEvent before any game logic. This module listens to that event: when recording it stores
// both arrays, when playing it overwrites them with the stored values. The RNG seeds are stored on the
// first frame and restored on playback, because Rand_Init seeds them from the clock at boot.
//
// Each frame also stores a small checkpoint (game state, level, player 0 state and position, RNG seeds).
// Playback compares it and reports the first frame that differs. The file holds raw structs, so it is
// only valid for the same build on the same platform.

#include "replay.h"
#include "switches.h"

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

#define REPLAY_MAGIC "SFCR"
#define REPLAY_VERSION 2
#define REPLAY_PADS 4

typedef enum {
    REPLAY_OFF,
    REPLAY_RECORD,
    REPLAY_PLAY,
    REPLAY_DONE,
} ReplayMode;

typedef struct {
    char magic[4];
    u32 version;
    u32 padSize;
    u32 frameSize;
    s32 seeds[6];
} ReplayHeader;

typedef struct {
    s32 gameState;
    s32 level;
    s32 playState;
    s32 playerState;
    f32 pos[3];
    s32 seeds[3];
} ReplayCheckpoint;

typedef struct {
    u32 frame;
    OSContPad hold[REPLAY_PADS];
    OSContPad press[REPLAY_PADS];
    ReplayCheckpoint cp;
} ReplayFrame;

static ReplayMode sMode = REPLAY_OFF;
static FILE* sFile = NULL;
static const char* sReportPath = "replay-report.txt";
static bool sStarted = false;
static u32 sFrame = 0;
static u32 sRecordedFrames = 0;
static s64 sFirstMismatch = -1;
static ReplayCheckpoint sMismatchWant;
static ReplayCheckpoint sMismatchGot;
static s64 sLevelCompleteFrame = -1;
static s32 sLevelCompleteLevel = -1;

static void CoopReplay_TakeCheckpoint(ReplayCheckpoint* cp) {
    memset(cp, 0, sizeof(*cp));
    cp->gameState = gGameState;
    cp->level = gCurrentLevel;
    cp->playState = gPlayState;
    cp->playerState = -1;
    if ((gGameState == GSTATE_PLAY) && (gPlayer != NULL)) {
        cp->playerState = gPlayer[0].state;
        cp->pos[0] = gPlayer[0].pos.x;
        cp->pos[1] = gPlayer[0].pos.y;
        cp->pos[2] = gPlayer[0].pos.z;
    }
    cp->seeds[0] = sRandSeed1;
    cp->seeds[1] = sRandSeed2;
    cp->seeds[2] = sRandSeed3;
}

static void CoopReplay_NoteLevelComplete(const ReplayCheckpoint* cp) {
    if ((sLevelCompleteFrame < 0) && (cp->gameState == GSTATE_PLAY) &&
        (cp->playerState == PLAYERSTATE_LEVEL_COMPLETE)) {
        sLevelCompleteFrame = sFrame;
        sLevelCompleteLevel = cp->level;
    }
}

static void CoopReplay_FormatCheckpoint(char* buf, size_t size, const ReplayCheckpoint* cp) {
    snprintf(buf, size, "gamestate=%d level=%d playstate=%d playerstate=%d pos=(%.3f,%.3f,%.3f) seeds=(%d,%d,%d)",
             cp->gameState, cp->level, cp->playState, cp->playerState, cp->pos[0], cp->pos[1], cp->pos[2], cp->seeds[0],
             cp->seeds[1], cp->seeds[2]);
}

static void CoopReplay_WriteReport(const char* mode, const char* result) {
    FILE* f = fopen(sReportPath, "w");
    char want[256];
    char got[256];
    ReplayCheckpoint now;

    if (f == NULL) {
        return;
    }
    CoopReplay_TakeCheckpoint(&now);
    CoopReplay_FormatCheckpoint(got, sizeof(got), &now);
    fprintf(f, "mode=%s\n", mode);
    fprintf(f, "frames=%u\n", sFrame);
    fprintf(f, "recorded_frames=%u\n", sRecordedFrames);
    fprintf(f, "first_mismatch=%lld\n", (long long) sFirstMismatch);
    if (sFirstMismatch >= 0) {
        CoopReplay_FormatCheckpoint(want, sizeof(want), &sMismatchWant);
        fprintf(f, "mismatch_want=%s\n", want);
        CoopReplay_FormatCheckpoint(want, sizeof(want), &sMismatchGot);
        fprintf(f, "mismatch_got=%s\n", want);
    }
    fprintf(f, "level_complete_frame=%lld\n", (long long) sLevelCompleteFrame);
    fprintf(f, "level_complete_level=%d\n", sLevelCompleteLevel);
    fprintf(f, "final=%s\n", got);
    // Written last: tools/coop/replay.py waits for this line.
    fprintf(f, "result=%s\n", result);
    fclose(f);
}

// failure is the result of a playback that did not reach a clean end of the file, NULL for one that did. It wins over a
// mismatch: player_trace.py accepts FAIL_MISMATCH with two or more players, and that must not hide a damaged file.
static void CoopReplay_FinishPlayback(const char* failure) {
    const char* result;

    if (sFile != NULL) {
        fclose(sFile);
        sFile = NULL;
    }
    if (failure != NULL) {
        result = failure;
    } else if (sFirstMismatch >= 0) {
        result = "FAIL_MISMATCH";
    } else {
        result = "PASS";
    }
    CoopReplay_WriteReport("play", result);
    sMode = REPLAY_DONE;
}

static void CoopReplay_OnExit(void) {
    if (sMode == REPLAY_RECORD) {
        if (sFile != NULL) {
            fclose(sFile);
            sFile = NULL;
        }
        sRecordedFrames = sFrame;
        CoopReplay_WriteReport("record", "RECORDED");
    } else if (sMode == REPLAY_PLAY) {
        // Window closed before the replay ended.
        CoopReplay_FinishPlayback("FAIL_INCOMPLETE");
    }
}

static bool CoopReplay_StartRecord(void) {
    ReplayHeader header;

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, REPLAY_MAGIC, 4);
    header.version = REPLAY_VERSION;
    header.padSize = sizeof(OSContPad);
    header.frameSize = sizeof(ReplayFrame);
    header.seeds[0] = sRandSeed1;
    header.seeds[1] = sRandSeed2;
    header.seeds[2] = sRandSeed3;
    header.seeds[3] = sSeededRandSeed1;
    header.seeds[4] = sSeededRandSeed2;
    header.seeds[5] = sSeededRandSeed3;
    return fwrite(&header, sizeof(header), 1, sFile) == 1;
}

static bool CoopReplay_StartPlay(void) {
    ReplayHeader header;

    if ((fread(&header, sizeof(header), 1, sFile) != 1) || (memcmp(header.magic, REPLAY_MAGIC, 4) != 0) ||
        (header.version != REPLAY_VERSION) || (header.padSize != sizeof(OSContPad)) ||
        (header.frameSize != sizeof(ReplayFrame))) {
        return false;
    }
    sRandSeed1 = header.seeds[0];
    sRandSeed2 = header.seeds[1];
    sRandSeed3 = header.seeds[2];
    sSeededRandSeed1 = header.seeds[3];
    sSeededRandSeed2 = header.seeds[4];
    sSeededRandSeed3 = header.seeds[5];
    return true;
}

// The read that found no whole frame got lastRead bytes. A file that ends inside a frame (a recording killed mid-write
// leaves one) or holds no frames at all proves nothing, however well the frames before it matched.
static const char* CoopReplay_FileEndFailure(size_t lastRead) {
    if ((lastRead != 0) || ferror(sFile)) {
        return "FAIL_TRUNCATED";
    }
    return (sFrame == 0) ? "FAIL_NO_FRAMES" : NULL;
}

static void CoopReplay_OnGamePreUpdate(IEvent* event) {
    ReplayFrame frame;
    ReplayCheckpoint cp;
    size_t lastRead;

    (void) event;
    if ((sMode != REPLAY_RECORD) && (sMode != REPLAY_PLAY)) {
        return;
    }

    // The Nintendo logo at boot waits 1000 ms of wall-clock time (Timer_CreateTask in fox_game.c, GSTATE_CHECK_SAVE),
    // so the number of boot frames differs from run to run. Start counting at GSTATE_START, right after the logo.
    if (!sStarted) {
        if (gGameState != GSTATE_START) {
            if (sMode == REPLAY_PLAY) {
                memset(gControllerHold, 0, sizeof(OSContPad) * REPLAY_PADS);
                memset(gControllerPress, 0, sizeof(OSContPad) * REPLAY_PADS);
            }
            return;
        }
        sStarted = true;
    }

    if (sFrame == 0) {
        bool ok = (sMode == REPLAY_RECORD) ? CoopReplay_StartRecord() : CoopReplay_StartPlay();
        if (!ok) {
            CoopReplay_WriteReport(sMode == REPLAY_RECORD ? "record" : "play", "ERROR_BAD_FILE");
            fclose(sFile);
            sFile = NULL;
            sMode = REPLAY_DONE;
            return;
        }
    }

    CoopReplay_TakeCheckpoint(&cp);
    CoopReplay_NoteLevelComplete(&cp);

    if (sMode == REPLAY_RECORD) {
        memset(&frame, 0, sizeof(frame));
        frame.frame = sFrame;
        memcpy(frame.hold, gControllerHold, sizeof(frame.hold));
        memcpy(frame.press, gControllerPress, sizeof(frame.press));
        frame.cp = cp;
        fwrite(&frame, sizeof(frame), 1, sFile);
        if ((sFrame % 60) == 0) {
            fflush(sFile);
        }
        sFrame++;
        return;
    }

    lastRead = fread(&frame, 1, sizeof(frame), sFile);
    if (lastRead != sizeof(frame)) {
        CoopReplay_FinishPlayback(CoopReplay_FileEndFailure(lastRead));
        return;
    }
    sRecordedFrames = frame.frame + 1;
    if ((sFirstMismatch < 0) && ((frame.frame != sFrame) || (memcmp(&frame.cp, &cp, sizeof(cp)) != 0))) {
        sFirstMismatch = sFrame;
        sMismatchWant = frame.cp;
        sMismatchGot = cp;
    }
    memcpy(gControllerHold, frame.hold, sizeof(frame.hold));
    memcpy(gControllerPress, frame.press, sizeof(frame.press));
    sFrame++;
}

void CoopReplay_Init(void) {
    const char* recordPath = getenv("SF64_COOP_REPLAY_RECORD");
    const char* playPath = getenv("SF64_COOP_REPLAY_PLAY");
    const char* reportPath = getenv("SF64_COOP_REPLAY_REPORT");

    // Test switches (WP-02) apply before the first game frame, with or without a replay.
    CoopTestSwitches_Init();

    if ((reportPath != NULL) && (reportPath[0] != '\0')) {
        sReportPath = reportPath;
    }
    if ((recordPath != NULL) && (recordPath[0] != '\0')) {
        sFile = fopen(recordPath, "wb");
        sMode = REPLAY_RECORD;
    } else if ((playPath != NULL) && (playPath[0] != '\0')) {
        sFile = fopen(playPath, "rb");
        sMode = REPLAY_PLAY;
    } else {
        return;
    }

    if (sFile == NULL) {
        CoopReplay_WriteReport(sMode == REPLAY_RECORD ? "record" : "play", "ERROR_OPEN_FILE");
        sMode = REPLAY_OFF;
        return;
    }
    // Listeners run in ascending priority (src/port/hooks/impl/EventSystem.cpp:27-29), so LOW runs first: the input
    // and the restored seeds are in place before any other listener, such as the state hash (WP-04), looks at them.
    REGISTER_LISTENER(GamePreUpdateEvent, CoopReplay_OnGamePreUpdate, EVENT_PRIORITY_LOW);
    atexit(CoopReplay_OnExit);
}
