#include "view.h"

#include "global.h"
#include "gfx.h"
#include "players.h"
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

void CoopView_DrawShieldBars(void) {
    f32 scale;
    f32 shields;
    s32 i;

    if (!CoopPlayers_IsCoop()) {
        return;
    }
    RCP_SetupDL(&gMasterDisp, SETUPDL_12);
    gSPClearGeometryMode(gMasterDisp++, G_CULL_BACK);
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
        FrameInterpolation_RecordCloseChild();

        sBarShown[i] = true;
        sBarValue[i] = shields;
    }
    gSPSetGeometryMode(gMasterDisp++, G_CULL_BACK);
}

float CoopView_ShieldBar(int32_t index) {
    if ((index < 0) || (index >= COOP_PLAYERS_MAX) || !sBarShown[index]) {
        return -1.0f;
    }
    return sBarValue[index];
}
