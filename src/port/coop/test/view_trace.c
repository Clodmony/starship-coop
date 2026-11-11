#include "view_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "sf64audio_provisional.h"
#include "port/hooks/Events.h"
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
            // What Player_Update wants before it calls Player_Down; gInvincible keeps the timer from being set by a hit.
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

static void CoopViewTrace_OnGamePostUpdate(IEvent* event) {
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
    fputs(" bars=", sFile);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        if (CoopView_ShieldBar(i) >= 0.0f) {
            fprintf(sFile, "%s%d:%.3f", (bars++ == 0) ? "" : ",", i, CoopView_ShieldBar(i));
        }
    }
    if (bars == 0) {
        fputc('-', sFile);
    }
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

void CoopViewTrace_Init(void) {
    const char* path = getenv("SF64_COOP_VIEW_TRACE");

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
