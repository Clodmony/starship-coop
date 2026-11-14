#include "view_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "sf64audio_provisional.h"
#include "port/hooks/Events.h"
#include "port/coop/game/names.h"
#include "port/coop/game/players.h"
#include "port/coop/game/view.h"

// What the HUD and the draw code keep for themselves.
extern f32 D_801617AC;
extern s32 sHudBombCount;
extern bool sDrawCockpit;
extern u8 sPlayersVisible[];
extern SfxRequest sSfxRequests[];
extern PlayerNoiseModulation sPlayerNoise[];
extern u8 sSfxRequestWriteIndex;
extern f32 gDefaultSfxSource[];

#define VIEW_POKES_MAX 32

typedef enum {
    VIEW_POKE_SHIELDS,
    VIEW_POKE_BOMBS,
    VIEW_POKE_LIVES,
    VIEW_POKE_BOOST,
    VIEW_POKE_HIT,
    VIEW_POKE_DOWN,
    VIEW_POKE_GUNS,
} ViewPokeKind;

typedef struct {
    s32 frame;
    s32 player;
    ViewPokeKind kind;
    f32 value; // a whole number, but for guns, which can take a half step
    bool done;
} ViewPoke;

static const char* sPokeNames[] = { "shields", "bombs", "lives", "boost", "hit", "down", "guns" };

static ViewPoke sPokes[VIEW_POKES_MAX];
static s32 sPokeCount = 0;
static FILE* sFile = NULL;
static u8 sSfxSeen = 0;

static void CoopViewTrace_Poke(ViewPoke* poke) {
    Player* player = &gPlayer[poke->player];

    switch (poke->kind) {
        case VIEW_POKE_SHIELDS:
            player->shields = (s32) poke->value;
            break;
        case VIEW_POKE_BOMBS:
            gBombCount[poke->player] = (s32) poke->value;
            break;
        case VIEW_POKE_LIVES:
            gLifeCount[poke->player] = (s32) poke->value;
            break;
        case VIEW_POKE_BOOST:
            player->boostMeter = (s32) poke->value;
            break;
        case VIEW_POKE_HIT:
            Player_ApplyDamage(player, 0, (s32) poke->value);
            break;
        case VIEW_POKE_DOWN:
            // What Player_Update wants before it calls Player_Down; gInvincible keeps the timer from being set by a
            // hit.
            player->shields = 0;
            player->radioDamageTimer = (s32) poke->value;
            break;
        case VIEW_POKE_GUNS:
            // Where the laser guns of the Arwing are (-10 fully out, in steps of 0.5 as Player_Shoot moves them);
            // laserGunsXpos follows from it (#192).
            player->arwing.laserGunsYpos = poke->value;
            break;
    }
}

static void CoopViewTrace_WriteSfx(void) {
    s32 count = 0;
    s32 i;

    fputs(" sfx=", sFile);
    for (; sSfxSeen != sSfxRequestWriteIndex; sSfxSeen++) {
        const SfxRequest* request = &sSfxRequests[sSfxSeen];

        fprintf(sFile, "%s%08x@", (count++ == 0) ? "" : ",", (u32) request->sfxId);
        if (request->source == gDefaultSfxSource) {
            fputc('d', sFile);
            continue;
        }
        for (i = 0; i < CoopPlayers_Count(); i++) {
            if (request->source == gPlayer[i].sfxSource) {
                fprintf(sFile, "p%d", i);
                break;
            }
        }
        if (i == CoopPlayers_Count()) {
            fputc('o', sFile);
        }
    }
    if (count == 0) {
        fputc('-', sFile);
    }
}

// The names drawn over other players this frame (#257, #258). A name's spaces are written as _, so the line stays split
// by spaces.
static void CoopViewTrace_WriteTags(void) {
    s32 tags = 0;
    s32 i;
    const char* c;

    fputs(" tags=", sFile);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        if (!CoopView_TagShown(i)) {
            continue;
        }
        fprintf(sFile, "%s%d:", (tags++ == 0) ? "" : ",", i);
        for (c = CoopNames_Label(i); *c != '\0'; c++) {
            fputc((*c == ' ') ? '_' : *c, sFile);
        }
    }
    if (tags == 0) {
        fputc('-', sFile);
    }
    // #332: where each of those names and its bar were drawn, on a 320x240 screen: left/top/right/bottom of the name,
    // then of the bar, then how far the name was lifted clear of a nearer one
    tags = 0;
    fputs(" tagbox=", sFile);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        float box[4];
        float bar[4];

        if (CoopView_TagBox(i, box, bar)) {
            fprintf(sFile, "%s%d:%.1f/%.1f/%.1f/%.1f/%.1f/%.1f/%.1f/%.1f/%.1f", (tags++ == 0) ? "" : ",", i, box[0],
                    box[1], box[2], box[3], bar[0], bar[1], bar[2], bar[3], CoopView_TagLift(i));
        }
    }
    if (tags == 0) {
        fputc('-', sFile);
    }
}

static void CoopViewTrace_OnGamePostUpdate(IEvent* event) {
    int32_t drawn[4];
    s32 bars = 0;
    s32 i;

    (void) event;
    if ((gGameState != GSTATE_PLAY) || (gPlayer == NULL)) {
        sSfxSeen = sSfxRequestWriteIndex;
        return;
    }

    fprintf(sFile, "frame=%d level=%d mode=%d local=%d view=%d eye=%.1f,%.1f,%.1f at=%.1f,%.1f,%.1f", gGameFrameCount,
            gCurrentLevel, gLevelMode, CoopPlayers_LocalIndex(), CoopPlayers_ViewIndex(), gPlayCamEye.x, gPlayCamEye.y,
            gPlayCamEye.z, gPlayCamAt.x, gPlayCamAt.y, gPlayCamAt.z);
    fprintf(sFile, " shield=%.4f bombs=%d cockpit=%d fill=%d,%d,%d,%d flash=%d", D_801617AC, sHudBombCount,
            sDrawCockpit, gFillScreenRed, gFillScreenGreen, gFillScreenBlue, gFillScreenAlpha,
            CoopView_HitFlashAlpha());
    CoopView_DrawnFill(drawn);
    fprintf(sFile, " drawn=%d,%d,%d,%d", (int) drawn[0], (int) drawn[1], (int) drawn[2], (int) drawn[3]);
    fputs(" bars=", sFile);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        if (CoopView_ShieldBar(i) >= 0.0f) {
            fprintf(sFile, "%s%d:%.3f", (bars++ == 0) ? "" : ",", i, CoopView_ShieldBar(i));
        }
    }
    if (bars == 0) {
        fputc('-', sFile);
    }
    CoopViewTrace_WriteTags();
    for (i = 0; i < CoopPlayers_Count(); i++) {
        const Player* p = &gPlayer[i];

        fprintf(sFile, " p%d=%d,%d,%d,%d,%d,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f", i, p->state, CoopPlayers_IsParked(i),
                p->shields, gBombCount[i], gLifeCount[i], p->alternateView, p->sfxSource[0], p->sfxSource[1],
                p->sfxSource[2], p->pos.x, p->pos.y, p->trueZpos);
    }
    fputs(" seen=", sFile);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        fprintf(sFile, "%s%d", (i == 0) ? "" : ",", sPlayersVisible[i]);
    }
    fputs(" guns=", sFile);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        fprintf(sFile, "%s%g/%g", (i == 0) ? "" : ",", gPlayer[i].arwing.laserGunsYpos,
                gPlayer[i].arwing.laserGunsXpos);
    }
    fputs(" noise=", sFile);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        fprintf(sFile, "%s%d", (i == 0) ? "" : ",", sPlayerNoise[i].form);
    }
    fprintf(sFile, " pause=%d", gPlayState == PLAY_PAUSE);
    fputs(" reticles=", sFile);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        fprintf(sFile, "%s%d", (i == 0) ? "" : ",", gShowReticles[i]);
    }
    {
        int32_t heatShields;
        int32_t heatHeight;

        if (CoopView_HeatAlarmSet(&heatShields, &heatHeight)) {
            fprintf(sFile, " heat=%d,%d", heatShields, heatHeight);
        } else {
            fputs(" heat=-", sFile);
        }
    }
    {
        int32_t billboards;
        int32_t turned = CoopView_TakeTurned(&billboards);

        fprintf(sFile, " turned=%d/%d", turned, billboards);
    }
    CoopViewTrace_WriteSfx();
    fputc('\n', sFile);
    if ((gGameFrameCount % 60) == 0) {
        fflush(sFile); // tests may kill the game on a timeout
    }

    for (i = 0; i < sPokeCount; i++) {
        if (!sPokes[i].done && (gGameFrameCount >= sPokes[i].frame)) {
            sPokes[i].done = true;
            CoopViewTrace_Poke(&sPokes[i]);
        }
    }
}

static void CoopViewTrace_ReadPokes(const char* text) {
    while ((text != NULL) && (text[0] != '\0') && (sPokeCount < VIEW_POKES_MAX)) {
        ViewPoke* poke = &sPokes[sPokeCount];
        char what[16];
        u32 kind;

        if (sscanf(text, "%d:%d:%15[a-z]:%f", &poke->frame, &poke->player, what, &poke->value) == 4) {
            for (kind = 0; kind < ARRAY_COUNT(sPokeNames); kind++) {
                if (strcmp(what, sPokeNames[kind]) == 0) {
                    poke->kind = (ViewPokeKind) kind;
                    sPokeCount++;
                    break;
                }
            }
        }
        text = strchr(text, ',');
        if (text != NULL) {
            text++;
        }
    }
}

// SF64_COOP_TEST_NAMES: the players' names without a session, as the session would hand them in (#258).
static void CoopViewTrace_ReadNames(const char* text) {
    char name[COOP_NAME_MAX + 1];
    s32 i;
    s32 n;

    for (i = 0; (text != NULL) && (i < COOP_PLAYERS_MAX); i++) {
        for (n = 0; (text[n] != '\0') && (text[n] != ','); n++) {}
        snprintf(name, sizeof(name), "%.*s", n, text);
        CoopNames_Set(i, name);
        text = (text[n] == ',') ? text + n + 1 : NULL;
    }
}

void CoopViewTrace_Init(void) {
    const char* path = getenv("SF64_COOP_VIEW_TRACE");

    CoopViewTrace_ReadNames(getenv("SF64_COOP_TEST_NAMES"));

    if ((path == NULL) || (path[0] == '\0')) {
        return;
    }
    sFile = fopen(path, "w");
    if (sFile == NULL) {
        return;
    }
    setvbuf(sFile, NULL, _IOFBF, 1 << 16);
    CoopViewTrace_ReadPokes(getenv("SF64_COOP_VIEW_POKE"));
    REGISTER_LISTENER(GamePostUpdateEvent, CoopViewTrace_OnGamePostUpdate, EVENT_PRIORITY_NORMAL);
}
