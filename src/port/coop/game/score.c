#include "score.h"

#include "global.h"
#include "port/coop/game/players.h"
#include "port/coop/session/session.h"
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

#define COOP_SCORE_ROW 17.0f

void CoopScore_DrawStatus(void) {
    s32 count = CoopPlayers_Count();
    f32 x = 212.0f; // right of the "enemies down" window (x 124 to 194 in HUD_LevelClearStatusScreen_Draw)
    f32 y = 28.0f;
    s32 i;

    if (!CoopPlayers_IsCoop() || gVersusMode || (count < 2)) {
        return;
    }
    HUD_MsgWindowBg_Draw2(x - 4.0f, y - 4.0f, 2.7f, (count * COOP_SCORE_ROW + 6.0f) / 17.0f);
    for (i = 0; i < count; i++) {
        f32 rowY = y + (i * COOP_SCORE_ROW);

        RCP_SetupDL(&gMasterDisp, SETUPDL_76_OPTIONAL);
        if (CoopPlayers_IsParked(i)) {
            gDPSetPrimColor(gMasterDisp++, 0, 0, 48, 48, 48, 255); // down or out, as a downed wingman's face
        } else {
            gDPSetPrimColor(gMasterDisp++, 0, 0, 255, 255, 255, 255);
        }
        HUD_VsModePortraitTex_Draw(x, rowY, 0.34f, CoopScore_Face(i));

        RCP_SetupDL(&gMasterDisp, SETUPDL_76_OPTIONAL);
        gDPSetPrimColor(gMasterDisp++, 0, 0, 255, 255, 255, 255);
        HUD_Number_Draw(x + 20.0f, rowY + 3.0f, gPlayerScores[i], 1.0f, true, 9999);
    }
}

void CoopScore_LevelInit(void) {
    s32 i;

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
