#include "score.h"

#include "global.h"
#include "sfx.h"
#include "port/coop/game/names.h"
#include "port/coop/game/players.h"
#include "port/coop/session/session.h"
#include "port/hooks/Events.h"
#include "port/mods/PortEnhancements.h" // SETUPDL_76_OPTIONAL, as src/engine/fox_hud.c draws

int32_t CoopScore_HumanKill(int32_t dmgSource) {
    if (dmgSource == AI360_FOX + 1) {
        return 1;
    }
    return CoopPlayers_IsCoop() && (dmgSource > COOP_DMG_SRC_PLAYER) &&
           (dmgSource < COOP_DMG_SRC_PLAYER + CoopPlayers_Count());
}

int32_t CoopScore_HumanShot(int32_t sourceId) {
    if (sourceId == TEAM_ID_FOX) {
        return 1;
    }
    return CoopPlayers_IsCoop() && (sourceId > 0) && (sourceId < CoopPlayers_Count());
}

int32_t CoopScore_LifeSlot(int32_t slot) {
    return CoopPlayers_IsCoop() ? 0 : slot;
}

static u16 sSavedScores[COOP_PLAYERS_MAX];

static void CoopScore_Credit(s32 index, s32 hits) {
    if (!CoopPlayers_IsCoop() || gVersusMode || (index < 0) || (index >= CoopPlayers_Count()) || (hits <= 0)) {
        return;
    }
    gPlayerScores[index] = MIN(gPlayerScores[index] + hits, 9999);
}

void CoopScore_CreditKill(int32_t dmgSource, int32_t hits) {
    if (dmgSource == AI360_FOX + 1) {
        CoopScore_Credit(0, hits);
    } else if (CoopScore_HumanKill(dmgSource)) {
        CoopScore_Credit(dmgSource - COOP_DMG_SRC_PLAYER, hits);
    }
}

void CoopScore_CreditShot(int32_t sourceId, int32_t hits) {
    if (CoopScore_HumanShot(sourceId)) {
        CoopScore_Credit(sourceId, hits); // TEAM_ID_FOX is 0, players 1 to 3 shoot with their index
    }
}

void CoopScore_Checkpoint(void) {
    s32 i;

    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        sSavedScores[i] = gPlayerScores[i];
    }
}

void HUD_MsgWindowBg_Draw2(f32 xPos, f32 yPos, f32 xScale, f32 yScale); // src/engine/fox_hud.c, not in a header

// The results hold (#252, the owner's wish after the two-PC run of 2026-11-11). Every level shows the level clear
// status screen for 200 frames (gShowLevelClearStatusScreen on and off by gCsFrameCount, e.g. 981 to 1181 in
// src/overlays/ovl_i1/fox_co.c; the shortest window of all levels is 200 frames). In a co-op session the scene holds
// once it has been shown COOP_RESULTS_HOLD_AT frames, until every human has pressed A or B, or for COOP_RESULTS_TIMEOUT
// frames at most. The tally of the screen runs in its draw (HUD_LevelClearStatusScreen_Draw, HUD_DrawStatusScreens),
// so it goes on while the scene holds. Counted from synced inputs in the update only, never from what was drawn.
#define COOP_RESULTS_READY_FROM 60 // presses count from 2 s on: a fire button still hammered from the fight does not
#define COOP_RESULTS_HOLD_AT 150   // the scene holds 5 s in, so 50 of the 200 frames are left after it goes on
#define COOP_RESULTS_TIMEOUT 900   // 30 s at 30 game frames a second (gVIsPerFrame 2), then it goes on alone

static s32 sResultsShown; // update frames the screen has been on, 0 while it is off
static s32 sResultsHeld;  // frames the scene has held
static s32 sResultsDone;  // the hold is over for this showing
static u8 sResultsReady[COOP_PLAYERS_MAX];

static void CoopScore_ResultsReset(void) {
    s32 i;

    sResultsShown = sResultsHeld = sResultsDone = 0;
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        sResultsReady[i] = 0;
    }
}

static s32 CoopScore_ResultsOn(void) {
    return CoopPlayers_IsCoop() && !gVersusMode && (CoopPlayers_Count() >= 2);
}

int32_t CoopScore_ResultsHolding(void) {
    return (sResultsShown >= COOP_RESULTS_HOLD_AT) && !sResultsDone;
}

static void CoopScore_OnPlayUpdate(IEvent* event) {
    s32 count = CoopPlayers_Count();
    s32 all = 1;
    s32 i;

    if (!CoopScore_ResultsOn() || !gShowLevelClearStatusScreen) {
        CoopScore_ResultsReset();
        return;
    }
    if (sResultsShown < COOP_RESULTS_HOLD_AT) {
        sResultsShown++;
    }
    for (i = 0; i < count; i++) {
        if ((sResultsShown >= COOP_RESULTS_READY_FROM) && !sResultsReady[i] &&
            (gControllerPress[CoopPlayers_PadOf(i)].button & (A_BUTTON | B_BUTTON))) {
            sResultsReady[i] = 1;
            AUDIO_PLAY_SFX(NA_SE_DECIDE, gDefaultSfxSource, 4);
        }
        all = all && (sResultsReady[i] || CoopPlayers_IsOut(i)); // a seat without a human holds nobody
    }
    if (!CoopScore_ResultsHolding()) {
        return;
    }
    if (all || (sResultsHeld >= COOP_RESULTS_TIMEOUT)) {
        sResultsDone = 1;
        return;
    }
    sResultsHeld++;
    event->cancelled = true; // the scene stands still; the screen above it draws on
}

void CoopScore_Init(void) {
    // HIGH runs last (EventSystem sorts LOW first), after the port's debug pause, which sets `cancelled` outright.
    REGISTER_LISTENER(PlayUpdateEvent, CoopScore_OnPlayUpdate, EVENT_PRIORITY_HIGH);
}

int32_t CoopScore_ResultsReady(int32_t index) {
    return (index >= 0) && (index < COOP_PLAYERS_MAX) && sResultsReady[index];
}

// The face HUD_VsModePortraitTex_Draw shows for player `index`: 0 Fox, 1 Peppy, 2 Slippy, 3 Falco. Without a roster
// (the debug player count) players 1 to 3 are Falco, Slippy and Peppy, the order a session hands them out.
static s32 CoopScore_Face(s32 index) {
    switch (CoopPlayers_Character(index)) {
        case COOP_CHARACTER_FOX:
            return 0;
        case COOP_CHARACTER_PEPPY:
            return 1;
        case COOP_CHARACTER_SLIPPY:
            return 2;
        case COOP_CHARACTER_FALCO:
            return 3;
        default:
            return (index == 0) ? 0 : (index == 1) ? 3 : (index == 2) ? 2 : 1;
    }
}

static void CoopScore_Color(s32 r, s32 g, s32 b) {
    RCP_SetupDL(&gMasterDisp, SETUPDL_76_OPTIONAL);
    gDPSetPrimColor(gMasterDisp++, 0, 0, r, g, b, 255);
}

static s32 CoopScore_Digits(s32 n) {
    s32 digits = 1;

    while (n >= 10) {
        n /= 10;
        digits++;
    }
    return digits;
}

// A window of its own at the top right, right of the team's "enemies down" (x 124 to 194) and above the lives (y 90):
// a title, then per human the face, the name and that player's hits. On the level clear screen also who has pressed
// A, and at the bottom what the others wait for.
void CoopScore_DrawStatus(void) {
    s32 count = CoopPlayers_Count();
    s32 results = (gPlayState != PLAY_PAUSE) && gShowLevelClearStatusScreen;
    f32 x = 206.0f;
    f32 right = 310.0f; // numbers end here
    f32 y = 22.0f;
    f32 row = (count <= 2) ? 26.0f : (count == 3) ? 19.0f : 15.0f;
    f32 face = (row - 4.0f) / 44.0f;
    s32 i;

    if (!CoopScore_ResultsOn()) {
        return;
    }
    HUD_MsgWindowBg_Draw2(x - 4.0f, y - 4.0f, (right - x + 10.0f) / 24.0f, (14.0f + count * row + 4.0f) / 17.0f);
    CoopScore_Color(90, 160, 200); // the blue of the team's own count, left on the same screen
    Graphics_DisplaySmallText(x, y, 1.0f, 1.0f, "HITS");

    for (i = 0; i < count; i++) {
        f32 rowY = y + 14.0f + (i * row);
        s32 hits = gPlayerScores[i];
        s32 down = CoopPlayers_IsParked(i);

        if (down) {
            CoopScore_Color(48, 48, 48); // down or out, as a downed wingman's face
        } else {
            CoopScore_Color(255, 255, 255);
        }
        HUD_VsModePortraitTex_Draw(x, rowY, face, CoopScore_Face(i));

        if (row >= 19.0f) {
            char* name = (char*) CoopNames_Label(i); // #258: the typed name, else the character's
            s32 width = Graphics_GetSmallTextWidth(name);
            f32 room = right - CoopScore_Digits(hits) * 13.0f - (x + row) - 2.0f; // up to the first digit of the hits

            CoopScore_Color(255, 255, 255);
            Graphics_DisplaySmallText(x + row, rowY + 1.0f, (width > room) ? room / width : 1.0f, 1.0f, name);
        }
        if (results && CoopScore_ResultsReady(i) && !CoopPlayers_IsOut(i)) {
            CoopScore_Color(80, 255, 80);
            Graphics_DisplaySmallText(x + row, rowY + ((row >= 19.0f) ? 11.0f : 1.0f), 1.0f, 1.0f, "OK");
        }
        CoopScore_Color(255, 255, 255);
        HUD_Number_Draw(right - CoopScore_Digits(hits) * 13.0f, rowY + ((row - 15.0f) / 2.0f), hits, 1.0f, true,
                        9999);
    }

    if (results && CoopScore_ResultsHolding() && ((gGameFrameCount & 16) == 0)) {
        CoopScore_Color(255, 255, 255);
        Graphics_DisplaySmallText(116, 222, 1.0f, 1.0f, "PRESS A");
    }
}

int32_t CoopScore_SavedHits(int32_t player) {
    return ((player >= 0) && (player < COOP_PLAYERS_MAX)) ? sSavedScores[player] : 0;
}

void CoopScore_SetSavedHits(int32_t player, int32_t hits) {
    if ((player >= 0) && (player < COOP_PLAYERS_MAX)) {
        sSavedScores[player] = (u16) CLAMP(hits, 0, 9999);
    }
}

void CoopScore_LevelInit(void) {
    s32 i;

    CoopScore_ResultsReset();
    if (!CoopPlayers_IsCoop() || gVersusMode) {
        return;
    }
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        if (gSavedHitCount == 0) {
            sSavedScores[i] = 0;
        }
        gPlayerScores[i] = sSavedScores[i];
    }
}
