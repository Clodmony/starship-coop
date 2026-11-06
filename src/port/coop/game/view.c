#include "view.h"

#include "global.h"
#include "players.h"

static f32 sCamYaw[COOP_PLAYERS_MAX];
static f32 sCamPitch[COOP_PLAYERS_MAX];
static bool sCamSet = false;

// The view of player 0 this frame, the same on every client.
static Matrix sReference;

static bool sHidden = false;
static Gfx* sHiddenDisp;
static Mtx* sHiddenMtx;
static Lightsn* sHiddenLight;

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
