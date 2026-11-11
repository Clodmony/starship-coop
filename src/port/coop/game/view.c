#include "view.h"

#include <string.h>

#include "global.h"
#include "gfx.h"
#include "names.h"
#include "players.h"
#include "sfx.h"
#include "port/Engine.h"
#include "port/interpolation/FrameInterpolation.h"

static f32 sCamYaw[COOP_PLAYERS_MAX];
static f32 sCamPitch[COOP_PLAYERS_MAX];
static bool sCamSet = false;

// The view of player 0 this frame, the same on every client.
static Matrix sReference;

static bool sHidden = false;
static Gfx* sHiddenDisp;
static Mtx* sHiddenMtx;
static Lightsn* sHiddenLight;

// What CoopView_DrawAnyway last heard: the object is in the camera this client draws.
static s32 sInDrawnView = 0;

#define HIT_FLASH_FIRST_TIMER 19 // the frame after dmgEffectTimer is set to 20
#define HIT_FLASH_LAST_TIMER 3
#define HIT_FLASH_STEP 8

// A shield bar, in units of Display_ActorMarks.
#define BAR_HALF_WIDTH 120.0f
#define BAR_ABOVE 170.0f

static Vtx sBarVtx[] = {
    VTX(0, 0, 0, 0, 0, 255, 255, 255, 255),
    VTX(0, 26, 0, 0, 0, 255, 255, 255, 255),
    VTX(240, 0, 0, 0, 0, 255, 255, 255, 255),
    VTX(240, 26, 0, 0, 0, 255, 255, 255, 255),
};

static Gfx sBarDL[] = {
    gsSPVertex(sBarVtx, ARRAY_COUNT(sBarVtx), 0),
    gsSP2Triangles(0, 1, 2, 0, 1, 2, 3, 0),
    gsSPEndDisplayList(),
};

static bool sBarOn[COOP_PLAYERS_MAX];
static bool sBarShown[COOP_PLAYERS_MAX];
static Vec3f sBarView[COOP_PLAYERS_MAX];
static f32 sBarValue[COOP_PLAYERS_MAX];

// The name over the bar (#257, #258): the player's name in the game's small font, white, centred above the bar. It
// stands NAME_ABOVE units of the bar above the bar's origin, and a texel of the font is NAME_TEXEL units of the bar at
// the size the bar has far away: the name keeps that size on screen, also where the bar grows as the other ship comes
// close (the bar's scale stops at 1, as Display_ActorMarks' does).
#define NAME_TEXEL 8.0f
#define NAME_ABOVE 226.0f // the bottom of the name: a little above the top of the bar (170 + 26)
#define NAME_SMALL_H 8

// The small font of src/engine/fox_std_lib.c, as Graphics_DisplaySmallText reads it.
extern char sSmallChars[];
extern u8* sSmallCharTex[];

static Vtx sSmallGlyphVtx[] = {
    VTX(0, 0, 0, 0, NAME_SMALL_H << 5, 255, 255, 255, 255),
    VTX(0, NAME_SMALL_H, 0, 0, 0, 255, 255, 255, 255),
    VTX(8, 0, 0, 8 << 5, NAME_SMALL_H << 5, 255, 255, 255, 255),
    VTX(8, NAME_SMALL_H, 0, 8 << 5, 0, 255, 255, 255, 255),
};
static Vtx sDigitGlyphVtx[] = {
    // the small font's digits are 16 texels wide, mostly empty
    VTX(0, 0, 0, 0, NAME_SMALL_H << 5, 255, 255, 255, 255),
    VTX(0, NAME_SMALL_H, 0, 0, 0, 255, 255, 255, 255),
    VTX(16, 0, 0, 16 << 5, NAME_SMALL_H << 5, 255, 255, 255, 255),
    VTX(16, NAME_SMALL_H, 0, 16 << 5, 0, 255, 255, 255, 255),
};

static bool sTagShown[COOP_PLAYERS_MAX];

// The eye and target Display_Update works out for `player`. It has one more case, for a level complete cutscene in
// all-range (Display_CsLevelCompleteHandleCamera), which keeps state of its own and is left out: there the plain camera
// counts.
static void CoopView_Camera(const Player* player, Vec3f* eye, Vec3f* at) {
    Vec3f rel;

    if (gLevelMode != LEVELMODE_ON_RAILS) {
        *eye = player->cam.eye;
        *at = player->cam.at;
        return;
    }

    Matrix_RotateY(gCalcMatrix, player->yRot_114 * M_DTOR, MTXF_NEW);
    Matrix_RotateX(gCalcMatrix, player->xRot_120 * M_DTOR, MTXF_APPLY);

    rel.x = player->cam.eye.x - player->pos.x;
    rel.y = player->cam.eye.y - player->pos.y;
    rel.z = player->cam.eye.z - (player->trueZpos + player->zPath);
    Matrix_MultVec3f(gCalcMatrix, &rel, eye);
    eye->x += player->pos.x;
    eye->y += player->pos.y;
    eye->z += player->trueZpos + player->zPath;

    rel.x = player->cam.at.x - player->pos.x;
    rel.y = player->cam.at.y - player->pos.y;
    rel.z = player->cam.at.z - (player->trueZpos + player->zPath);
    Matrix_MultVec3f(gCalcMatrix, &rel, at);
    at->x += player->pos.x;
    at->y += player->pos.y;
    at->z += player->trueZpos + player->zPath;

    if (player->alternateView && (player->boostSpeed > 5.0f)) {
        at->x += SIN_DEG(gGameFrameCount * 150.0f) * player->boostSpeed * 0.2f;
    }
}

static void CoopView_SetReference(const Player* player, const Vec3f* eye, const Vec3f* at) {
    Vec3f src = { 0.0f, 100.0f, 0.0f };
    Vec3f up;

    Matrix_RotateY(gCalcMatrix, -player->camYaw, MTXF_NEW);
    Matrix_RotateX(gCalcMatrix, player->camPitch, MTXF_APPLY);
    Matrix_RotateZ(gCalcMatrix, -player->camRoll * M_DTOR, MTXF_APPLY);
    Matrix_MultVec3f(gCalcMatrix, &src, &up);
    Matrix_LookAt(&sReference, eye->x, eye->y, eye->z, at->x, at->y, at->z, up.x, up.y, up.z, MTXF_NEW);
}

void CoopView_BeginDraw(void) {
    Vec3f eye;
    Vec3f at;
    s32 i;

    if (!CoopPlayers_IsCoop() || (gDrawMode != DRAW_PLAY)) {
        return;
    }

    for (i = 0; i < CoopPlayers_Count(); i++) {
        CoopView_Camera(&gPlayer[i], &eye, &at);
        sCamYaw[i] = -Math_Atan2F(eye.x - at.x, eye.z - at.z);
        sCamPitch[i] = -Math_Atan2F(eye.y - at.y, sqrtf(SQ(eye.z - at.z) + SQ(eye.x - at.x)));
        gPlayer[i].camYaw = sCamYaw[i];
        gPlayer[i].camPitch = sCamPitch[i];
        gPlayer[i].arwing.drawFace = true; // Display_Update does this for player 0 only; every pilot shows
        if (i == 0) {
            CoopView_SetReference(&gPlayer[0], &eye, &at);
        }
        // Background_DrawGround clears this for the player drawn, src/engine/fox_bg.c.
        if ((gLevelMode == LEVELMODE_ALL_RANGE) || (gGroundType == 7)) {
            gPlayer[i].xPath = 0.0f;
        }
    }
    sCamSet = true;
}

void CoopView_EndDraw(void) {
    s32 i;

    if (!CoopPlayers_IsCoop()) {
        return;
    }

    if (sCamSet) {
        for (i = 0; i < CoopPlayers_Count(); i++) {
            gPlayer[i].camYaw = sCamYaw[i];
            gPlayer[i].camPitch = sCamPitch[i];
        }
        sCamSet = false;
    }
    gPlayerNum = 0;
}

int32_t CoopView_DrawAnyway(int32_t inView) {
    if (!CoopPlayers_IsCoop()) {
        return 0;
    }
    sInDrawnView = inView;
    if (!inView && !sHidden) {
        sHidden = true;
        sHiddenDisp = gMasterDisp;
        sHiddenMtx = gGfxMtx;
        sHiddenLight = gLight;
    }
    return 1;
}

void CoopView_EndHidden(void) {
    if (sHidden) {
        sHidden = false;
        gMasterDisp = sHiddenDisp;
        gGfxMtx = sHiddenMtx;
        gLight = sHiddenLight;
    }
}

void CoopView_ToReference(const Vec3f* world, Vec3f* view) {
    Vec3f src = *world;

    Matrix_MultVec3f(&sReference, &src, view);
}

float CoopView_LockOnZ(const Vec3f* pos, float drawnZ) {
    Vec3f world;
    Vec3f view;

    if (!CoopPlayers_IsCoop()) {
        return drawnZ;
    }
    world.x = pos->x;
    world.y = pos->y;
    world.z = pos->z + gPathProgress;
    CoopView_ToReference(&world, &view);
    return view.z;
}

int32_t CoopView_InDrawnView(int32_t seen) {
    return CoopPlayers_IsCoop() ? sInDrawnView : seen;
}

void CoopView_ToDrawnCamera(const Vec3f* pos, float* sfxSrc) {
    static Matrix sEars;
    const Player* camera = &gPlayer[CoopPlayers_ViewIndex()];
    Vec3f src;
    Vec3f dest;

    Matrix_RotateY(&sEars, camera->camYaw, MTXF_NEW);
    Matrix_RotateX(&sEars, camera->camPitch, MTXF_APPLY);
    src.x = pos->x - gPlayCamEye.x;
    src.y = pos->y - gPlayCamEye.y;
    src.z = pos->z + gPathProgress - gPlayCamEye.z;
    Matrix_MultVec3f(&sEars, &src, &dest);
    sfxSrc[0] = dest.x;
    sfxSrc[1] = dest.y;
    sfxSrc[2] = dest.z;
}

int32_t CoopView_HitFlashAlpha(void) {
    s32 timer;

    if (!CoopPlayers_IsCoop() || (gGameState != GSTATE_PLAY) || (gDrawMode != DRAW_PLAY) || (gPlayer == NULL)) {
        return 0;
    }
    // As the hit did with the fill: 128 on the first frame, then 8 less on each (Player_Update, Play_UpdateFillScreen).
    timer = gPlayer[CoopPlayers_ViewIndex()].dmgEffectTimer;
    return ((timer > HIT_FLASH_LAST_TIMER) && (timer < HIT_FLASH_FIRST_TIMER + 1))
               ? (timer - HIT_FLASH_LAST_TIMER) * HIT_FLASH_STEP
               : 0;
}

void CoopView_DrawHitFlash(void) {
    Graphics_FillRectangle(&gMasterDisp, OTRGetRectDimensionFromLeftEdge(0), 0,
                           OTRGetRectDimensionFromRightEdge(SCREEN_WIDTH), SCREEN_HEIGHT, 255, 0, 0,
                           CoopView_HitFlashAlpha());
}

void CoopView_SeePlayer(const Player* player) {
    Vec3f world;
    s32 i = player->num;

    if (!CoopPlayers_IsCoop() || (i < 0) || (i >= COOP_PLAYERS_MAX)) {
        return;
    }
    sBarShown[i] = false;
    sTagShown[i] = false;
    sBarOn[i] = !CoopPlayers_IsViewed(i) && !CoopPlayers_IsParked(i) && player->draw;
    if (sBarOn[i]) {
        world.x = player->pos.x;
        world.y = player->pos.y;
        world.z = player->trueZpos + player->zPath;
        Matrix_MultVec3f(gGfxMatrix, &world, &sBarView[i]);
    }
}

// One bar, from its left end, `length` of the full width.
static void CoopView_DrawBar(f32 length) {
    Matrix_Push(&gGfxMatrix);
    Matrix_Scale(gGfxMatrix, length, 1.0f, 1.0f, MTXF_APPLY);
    Matrix_SetGfxMtx(&gMasterDisp);
    gSPDisplayList(gMasterDisp++, sBarDL);
    Matrix_Pop(&gGfxMatrix);
}

// One glyph of a font, its lower left corner at x, y in units of the bar.
static void CoopView_Glyph(u8* texture, s32 width, s32 height, Vtx* vtx, f32 x, f32 y) {
    Matrix_Push(&gGfxMatrix);
    Matrix_Translate(gGfxMatrix, x, y, 0.0f, MTXF_APPLY);
    Matrix_Scale(gGfxMatrix, NAME_TEXEL, NAME_TEXEL, 1.0f, MTXF_APPLY);
    Matrix_SetGfxMtx(&gMasterDisp);
    gDPLoadTextureBlock(gMasterDisp++, texture, G_IM_FMT_IA, G_IM_SIZ_8b, width, height, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPVertex(gMasterDisp++, vtx, 4, 0);
    gSP2Triangles(gMasterDisp++, 0, 1, 2, 0, 1, 2, 3, 0);
    Matrix_Pop(&gGfxMatrix);
}

// Where a character is in the small font's table, -1 if the font has none: as Graphics_DisplaySmallText looks it up.
static s32 CoopView_FontIndex(char c) {
    const char* found = strchr(sSmallChars, c);

    return ((c != '\0') && (found != NULL)) ? (s32) (found - sSmallChars) : -1;
}

// Text in the small font from x, y, as Graphics_DisplaySmallText steps it.
static void CoopView_SmallText(const char* text, f32 x, f32 y) {
    f32 at = 0.0f;
    s32 c;

    for (; *text != '\0'; text++) {
        c = CoopView_FontIndex(*text);
        if (c < 0) {
            continue;
        }
        if (sSmallCharTex[c] != NULL) {
            CoopView_Glyph(sSmallCharTex[c], (c > 30) ? 16 : 8, NAME_SMALL_H,
                           (c > 30) ? sDigitGlyphVtx : sSmallGlyphVtx, x + at * NAME_TEXEL, y);
        }
        switch (*text) {
            case '!':
            case ':':
            case 'I':
                at += 4.0f;
                break;
            case '-':
                at += 6.0f;
                break;
            default:
                at += (c >= 30) ? 9.0f : 8.0f;
                break;
        }
    }
}

// Player i's name, centred on x 0 from y 0 up, with a dark copy one texel down and right behind it so it reads on sky
// and ground alike. The owner chose the name alone as the mark, no letter (#257).
static void CoopView_DrawTag(s32 i) {
    const char* name = CoopNames_Label(i);
    f32 x = -Graphics_GetSmallTextWidth((char*) name) * NAME_TEXEL / 2.0f;

    RCP_SetupDL(&gMasterDisp, SETUPDL_62);
    gDPSetTextureFilter(gMasterDisp++, G_TF_POINT);
    gDPSetPrimColor(gMasterDisp++, 0x00, 0x00, 0, 0, 0, 200);
    CoopView_SmallText(name, x + NAME_TEXEL, -NAME_TEXEL);
    gDPSetPrimColor(gMasterDisp++, 0x00, 0x00, 255, 255, 255, 255);
    CoopView_SmallText(name, x, 0.0f);
    gDPSetTextureFilter(gMasterDisp++, G_TF_BILERP);
    sTagShown[i] = true;
}

void CoopView_DrawShieldBars(void) {
    f32 scale;
    f32 tagScale;
    f32 shields;
    s32 i;

    if (!CoopPlayers_IsCoop()) {
        return;
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        if (!sBarOn[i] || (sBarView[i].z >= 0.0f)) {
            continue;
        }
        FrameInterpolation_RecordOpenChild("CoopShieldBar", i);
        // As Display_ActorMarks: about the same size on screen at any distance.
        scale = VEC3F_MAG(&sBarView[i]) * 0.0015f;
        if (scale > 100.0f) {
            scale = 100.0f;
        } else if (scale < 1.0f) {
            scale = 1.0f;
        }
        shields = MIN(gPlayer[i].shields, 255) / 255.0f;

        Matrix_Push(&gGfxMatrix);
        Matrix_Translate(gGfxMatrix, sBarView[i].x, sBarView[i].y, sBarView[i].z, MTXF_APPLY);
        Matrix_Scale(gGfxMatrix, scale * 0.25f, scale * 0.25f, 1.0f, MTXF_APPLY);
        RCP_SetupDL(&gMasterDisp, SETUPDL_12);
        gSPClearGeometryMode(gMasterDisp++, G_CULL_BACK);
        Matrix_Push(&gGfxMatrix);
        Matrix_Translate(gGfxMatrix, -BAR_HALF_WIDTH, BAR_ABOVE, 0.0f, MTXF_APPLY);
        gDPSetPrimColor(gMasterDisp++, 0x00, 0x00, 0, 0, 0, 160);
        CoopView_DrawBar(1.0f);
        if (shields < 0.25f) {
            gDPSetPrimColor(gMasterDisp++, 0x00, 0x00, 255, 40, 0, 255);
        } else {
            gDPSetPrimColor(gMasterDisp++, 0x00, 0x00, 80, 220, 80, 255);
        }
        CoopView_DrawBar(shields);
        Matrix_Pop(&gGfxMatrix);
        // #257, #258: the name above the bar, the size it has far away
        Matrix_Push(&gGfxMatrix);
        Matrix_Translate(gGfxMatrix, 0.0f, NAME_ABOVE, 0.0f, MTXF_APPLY);
        tagScale = MIN(VEC3F_MAG(&sBarView[i]) * 0.0015f, 100.0f) / scale;
        Matrix_Scale(gGfxMatrix, tagScale, tagScale, 1.0f, MTXF_APPLY);
        CoopView_DrawTag(i);
        Matrix_Pop(&gGfxMatrix);
        Matrix_Pop(&gGfxMatrix);
        FrameInterpolation_RecordCloseChild();

        sBarShown[i] = true;
        sBarValue[i] = shields;
    }
    gSPSetGeometryMode(gMasterDisp++, G_CULL_BACK);
}

int32_t CoopView_TagShown(int32_t index) {
    return (index >= 0) && (index < COOP_PLAYERS_MAX) && sTagShown[index];
}

float CoopView_ShieldBar(int32_t index) {
    if ((index < 0) || (index >= COOP_PLAYERS_MAX) || !sBarShown[index]) {
        return -1.0f;
    }
    return sBarValue[index];
}

// ---- What still followed player 0 on every client (#140) ----

static s32 sHeatShields = -1;
static s32 sHeatHeight = -1;

void CoopView_HeatAlarm(uint8_t shields, uint8_t heightParam) {
    const Player* player;
    f32 y;

    if (CoopPlayers_IsCoop() && (CoopPlayers_ViewIndex() != 0)) {
        player = &gPlayer[CoopPlayers_ViewIndex()];
        shields = (u8) CLAMP(player->shields, 0, 255);
        // As Play_UpdateLevel works it out for player 0: 0 up to 200 high, then one more for each 100, at most 5.
        y = player->pos.y;
        heightParam = (y > 600.0f) ? 5 : (y > 500.0f) ? 4 : (y > 400.0f) ? 3 : (y > 300.0f) ? 2 : (y > 200.0f) ? 1 : 0;
    }
    sHeatShields = shields;
    sHeatHeight = heightParam;
    Audio_SetHeatAlarmParams(shields, heightParam);
}

int32_t CoopView_HeatAlarmSet(int32_t* shields, int32_t* heightParam) {
    int32_t set = sHeatShields >= 0;

    *shields = sHeatShields;
    *heightParam = sHeatHeight;
    sHeatShields = sHeatHeight = -1;
    return set;
}

int32_t CoopView_PauseReticles(void) {
    Player* player;
    s32 i;

    if (!CoopPlayers_IsCoop()) {
        return 0;
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        player = &gPlayer[i];
        // As upstream for player 0, from each player's own pad slot, which every client holds the same.
        if ((gControllerPress[CoopPlayers_PadOf(i)].button & R_TRIG) && (player->form != FORM_BLUE_MARINE) &&
            (player->state != PLAYERSTATE_STANDBY)) {
            gShowReticles[i] = !gShowReticles[i];
            if (i == CoopPlayers_LocalIndex()) {
                AUDIO_PLAY_SFX(gShowReticles[i] ? NA_SE_MAP_WINDOW_OPEN : NA_SE_MAP_WINDOW_CLOSE, gDefaultSfxSource, 4);
            }
        }
    }
    return 1;
}

// The drawn rotation of every facade, by gScenery slot, to know when it flips: -1 for none yet.
static f32 sFacadeDrawn[ARRAY_COUNT(gScenery)];
static s32 sFacadeInit = 0;
static s32 sTurned = 0;
static s32 sBillboards = 0;

static s32 CoopView_IsDoodad(const Object* obj) {
    return (obj->id == OBJ_SPRITE_CO_POLE) || (obj->id == OBJ_SPRITE_CO_TREE) || (obj->id == OBJ_SPRITE_FO_POLE);
}

f32 CoopView_DrawnRotY(const Object* obj, int32_t facade, int32_t* flipped) {
    const Player* camera;
    f32 rotY;
    s32 slot;

    if (!CoopPlayers_IsCoop()) {
        return obj->rot.y;
    }
    if (facade || CoopView_IsDoodad(obj)) {
        sBillboards++;
    }
    if (CoopPlayers_ViewIndex() == 0) {
        return obj->rot.y;
    }
    camera = &gPlayer[CoopPlayers_ViewIndex()];
    if (facade) {
        // Object_SetMatrix's choice for player 0's eye, made for the eye drawn.
        rotY = (camera->cam.eye.x < obj->pos.x) ? 271.0f : 0.0f;
        slot = (s32) ((const Scenery*) obj - gScenery);
        if (!sFacadeInit) {
            for (s32 i = 0; i < ARRAY_COUNT(sFacadeDrawn); i++) {
                sFacadeDrawn[i] = -1.0f;
            }
            sFacadeInit = 1;
        }
        if ((slot >= 0) && (slot < ARRAY_COUNT(sFacadeDrawn))) {
            *flipped = (sFacadeDrawn[slot] >= 0.0f) && (sFacadeDrawn[slot] != rotY);
            sFacadeDrawn[slot] = rotY;
        }
    } else if (CoopView_IsDoodad(obj)) {
        // Sprite_UpdateDoodad's turn to player 0's eye, made for the eye drawn.
        rotY =
            (Math_Atan2F(camera->cam.eye.x - obj->pos.x, camera->cam.eye.z - (obj->pos.z + gPathProgress)) * 180.0f) /
            M_PI;
    } else {
        return obj->rot.y;
    }
    if (rotY != obj->rot.y) {
        sTurned++;
    }
    return rotY;
}

int32_t CoopView_TakeTurned(int32_t* billboards) {
    s32 turned = sTurned;

    *billboards = sBillboards;
    sTurned = sBillboards = 0;
    return turned;
}
