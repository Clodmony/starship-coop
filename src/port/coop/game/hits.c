#include "hits.h"

#include "global.h"
#include "port/coop/game/players.h"

// The player at `i` (1 and up) if a hit can reach it, else NULL.
static Player* CoopHits_Target(s32 i) {
    return CoopPlayers_IsParked(i) ? NULL : &gPlayer[i];
}

// Modes in which TexturedLine_Update tests the player (src/engine/fox_enmy.c).
static s32 CoopHits_LineHurts(const TexturedLine* line) {
    return (line->mode == 1) || (line->mode == 101) || (line->mode == 50);
}

void CoopHits_TexturedLine(int32_t index) {
    TexturedLine* line = &gTexturedLines[index];
    s32 i;

    if (!CoopPlayers_IsCoop()) {
        return;
    }
    // A hit can end the line's mode, as it does for player 0: then the line hurts nobody after that.
    for (i = 1; (i < CoopPlayers_Count()) && CoopHits_LineHurts(line); i++) {
        Player* p = CoopHits_Target(i);
        Vec3f src;
        Vec3f dest;

        if ((p == NULL) || (p->state != PLAYERSTATE_ACTIVE) || (p->hitTimer != 0)) {
            continue;
        }
        // Player_ApplyDamage writes gCalcMatrix, so the rotation is set again for each player.
        Matrix_RotateX(gCalcMatrix, -line->xRot, MTXF_NEW);
        Matrix_RotateY(gCalcMatrix, -line->yRot, MTXF_APPLY);
        src.x = p->pos.x - line->posAA.x;
        src.y = p->pos.y - line->posAA.y;
        src.z = p->trueZpos - line->posAA.z;
        Matrix_MultVec3fNoTranslate(gCalcMatrix, &src, &dest);
        dest.x += line->posAA.x;
        dest.y += line->posAA.y;
        dest.z += line->posAA.z;
        if ((fabsf(dest.x - line->posAA.x) < 30.0f) && (fabsf(dest.y - line->posAA.y) < 30.0f) &&
            (dest.z < line->posAA.z) && ((line->posAA.z - line->zScale) < dest.z)) {
            Player_ApplyDamage(p, 0, (gCurrentLevel == LEVEL_AQUAS) ? 30 : 20);
            if (line->mode < 100) {
                line->mode = 0;
            }
        }
    }
}

void CoopHits_BolseBeam(Boss* base, float xRot, float yRot, float xOffset, float zOffset, float length) {
    s32 i;
    s32 j;

    if (!CoopPlayers_IsCoop()) {
        return;
    }
    for (i = 1; i < CoopPlayers_Count(); i++) {
        Player* p = CoopHits_Target(i);
        Vec3f src;
        Vec3f dest;

        if (p == NULL) {
            continue;
        }
        Matrix_RotateX(gCalcMatrix, -xRot * M_DTOR, MTXF_NEW);
        Matrix_RotateY(gCalcMatrix, -yRot * M_DTOR, MTXF_APPLY);
        src.x = p->pos.x - (base->obj.pos.x + xOffset);
        src.y = p->pos.y - (base->obj.pos.y + 580.0f);
        src.z = p->trueZpos - (base->obj.pos.z + zOffset);
        Matrix_MultVec3fNoTranslate(gCalcMatrix, &src, &dest);
        if (!((fabsf(dest.x) < 100.0f) && (fabsf(dest.y) < 200.0f) && (dest.z >= 0.0f) && (dest.z < length) &&
              (p->hitTimer == 0))) {
            continue;
        }
        Player_ApplyDamage(p, 0, 40);
        p->knockback.y = (dest.y > 0.0f) ? 20.0f : -20.0f;
        for (j = 0; j < 5; j++) {
            Effect_Effect389_Spawn(p->pos.x + RAND_FLOAT_CENTERED(30.0f), p->pos.y + RAND_FLOAT(10.0f),
                                   p->trueZpos + RAND_FLOAT_CENTERED(30.0f), p->vel.x, p->vel.y + p->knockback.y,
                                   p->vel.z, RAND_FLOAT(0.1f) + 0.1f, p->num + 11);
        }
        Effect386_Spawn1(p->pos.x + RAND_FLOAT_CENTERED(10.0f), p->pos.y + RAND_FLOAT(10.0f),
                         p->trueZpos + RAND_FLOAT_CENTERED(10.0f), 0.0f, 15.0f, 0.0f, 2.0f, 5);
    }
}

void CoopHits_AndrossReach(float x, float y) {
    s32 i;

    if (!CoopPlayers_IsCoop()) {
        return;
    }
    for (i = 1; i < CoopPlayers_Count(); i++) {
        Player* p = CoopHits_Target(i);

        if ((p != NULL) && (p->mercyTimer == 0) && (p->state == PLAYERSTATE_ACTIVE) && (fabsf(x - p->pos.x) < 700.0f) &&
            (fabsf(y - p->pos.y) < 700.0f)) {
            Player_ApplyDamage(p, 0, 40);
        }
    }
}
