#include "effects.h"

#include "global.h"
#include "port/coop/game/players.h"

// Nonzero when an effect can hit player `i`, who is not the one the upstream test covers.
static s32 CoopEffects_CanHit(s32 i, s32 named) {
    return (i != named) && !CoopPlayers_IsParked(i);
}

void CoopEffects_BoxHit(int32_t named, Effect* effect, float size) {
    s32 i;

    if (!CoopPlayers_IsCoop()) {
        return;
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        Player* p = &gPlayer[i];

        if (!CoopEffects_CanHit(i, named) || ((effect->obj.id == OBJ_EFFECT_397) && (p->barrelRollAlpha != 0))) {
            continue;
        }
        if ((fabsf(p->trueZpos - effect->obj.pos.z) < size) && (fabsf(p->pos.x - effect->obj.pos.x) < size) &&
            (fabsf(p->pos.y - effect->obj.pos.y) < size) && (p->mercyTimer == 0)) {
            Player_ApplyDamage(p, 0, effect->info.damage);
        }
    }
}

// The shot flies off in a random direction, as it does from player 0.
static void CoopEffects_Deflect(Player* p, Effect* effect) {
    Vec3f src = { 0.0f, 0.0f, 100.0f };
    Vec3f dest;

    effect->obj.rot.y = 90.0f;
    effect->obj.rot.x = RAND_FLOAT(360.0f);
    Matrix_RotateY(gCalcMatrix, effect->obj.rot.y * M_DTOR, MTXF_NEW);
    Matrix_RotateX(gCalcMatrix, effect->obj.rot.x * M_DTOR, MTXF_APPLY);
    Matrix_MultVec3f(gCalcMatrix, &src, &dest);
    effect->vel.x = dest.x;
    effect->vel.y = dest.y;
    effect->vel.z = dest.z;
    p->deflectCount++;
    AUDIO_PLAY_SFX(NA_SE_ROLLING_REFLECT, effect->sfxSource, 0);
}

void CoopEffects_ShotHit(Effect* effect) {
    s32 i;

    if (!CoopPlayers_IsCoop()) {
        return;
    }
    for (i = 0; (i < CoopPlayers_Count()) && (effect->obj.status != OBJ_FREE); i++) {
        Player* p = &gPlayer[i];
        f32 reach = (p->barrelRollAlpha != 0) ? 100.0f : 0.0f;

        if (!CoopEffects_CanHit(i, 0) || !(fabsf(p->trueZpos - effect->obj.pos.z) < (50.0f + reach)) ||
            !(fabsf(p->pos.x - effect->obj.pos.x) < (30.0f + reach)) ||
            !(fabsf(p->pos.y - effect->obj.pos.y) < (30.0f + reach))) {
            continue;
        }
        if ((p->barrelRollAlpha != 0) || (p->meteoWarpTimer != 0)) {
            CoopEffects_Deflect(p, effect);
        }
        if ((p->barrelRollAlpha == 0) && (p->mercyTimer == 0)) {
            Player_ApplyDamage(p, 0, effect->info.damage);
            p->knockback.x = (effect->vel.x < 0.0f) ? -20.0f : 20.0f;
            if (gCurrentLevel != LEVEL_MACBETH) {
                p->knockback.y = (effect->vel.y < 0.0f) ? -20.0f : 20.0f;
            }
            Object_Kill(&effect->obj, effect->sfxSource);
        }
    }
}

void CoopEffects_ColumnHit(Effect* effect) {
    s32 i;

    if (!CoopPlayers_IsCoop()) {
        return;
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        Player* p = &gPlayer[i];

        if (CoopEffects_CanHit(i, 0) && (fabsf(p->trueZpos - effect->obj.pos.z) < 40.0f) &&
            (fabsf(p->pos.x - effect->obj.pos.x) < 80.0f) && (effect->obj.pos.y < p->pos.y) &&
            ((p->pos.y - effect->obj.pos.y) < (effect->scale2 * 35.0f)) && (p->mercyTimer == 0)) {
            Player_ApplyDamage(p, 0, effect->info.damage);
        }
    }
}

void CoopEffects_BeamHit(Effect* effect) {
    s32 i;

    if (!CoopPlayers_IsCoop() || (effect->timer_50 != 0)) {
        return;
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        Player* p = &gPlayer[i];
        Vec3f src = { 0.0f, 0.0f, 0.0f };
        Vec3f dest;

        src.z = fabsf(p->trueZpos - effect->obj.pos.z);
        Matrix_MultVec3fNoTranslate(gCalcMatrix, &src, &dest);
        if (CoopEffects_CanHit(i, 0) &&
            (fabsf(p->pos.x - (effect->obj.pos.x + dest.x)) <= (effect->unk_60.x * 50.0f)) &&
            (fabsf(p->pos.y - (effect->obj.pos.y + dest.y)) <= (effect->unk_60.y * 50.0f)) && (p->mercyTimer == 0)) {
            Matrix_Push(&gCalcMatrix); // Player_ApplyDamage writes gCalcMatrix, and the test of player 0 comes next
            Player_ApplyDamage(p, 0, 40);
            Matrix_Pop(&gCalcMatrix);
        }
    }
}
