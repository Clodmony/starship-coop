#include "fixes.h"

#include "global.h"
#include "port/coop/game/players.h"

int32_t CoopFixes_IworkNegative(uintptr_t word) {
    return CoopPlayers_IsCoop() && ((s32) word < 0);
}

float CoopFixes_IworkFactor(uintptr_t word) {
    return CoopPlayers_IsCoop() ? (f32) (s32) word : (f32) word;
}

int32_t CoopFixes_HoldsBeam(const Effect* effect, int32_t state) {
    if (!CoopPlayers_IsCoop()) {
        return 1;
    }
    return (effect->obj.status == OBJ_ACTIVE) && (effect->obj.id == OBJ_EFFECT_395) && (effect->state == state);
}

Effect* CoopFixes_GorgonBeam(int32_t slot) {
    // Not part of the game's state: nothing reads it after the call that got it, and each call starts it from zeros.
    static Effect sStandIn;

    if (!CoopPlayers_IsCoop()) {
        return &gEffects[slot];
    }
    if ((slot >= 0) && (slot < ARRAY_COUNT(gEffects)) && CoopFixes_HoldsBeam(&gEffects[slot], 10)) {
        return &gEffects[slot];
    }
    memset(&sStandIn, 0, sizeof(sStandIn));
    return &sStandIn;
}

void CoopFixes_TakeLine(int32_t index) {
    if (!CoopPlayers_IsCoop() || (index < 0) || (index >= ARRAY_COUNT(gTexturedLines))) {
        return;
    }
    if (gTexturedLines[index].mode == 0) {
        memset(&gTexturedLines[index], 0, sizeof(gTexturedLines[index]));
    }
}
