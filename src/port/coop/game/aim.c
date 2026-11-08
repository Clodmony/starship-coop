#include "aim.h"

#include "global.h"
#include "port/coop/game/players.h"

// Nonzero when an enemy may aim at player `i`. Player 0 always counts: it is upstream's target, and while it does not
// fly the others are parked (CoopPlayers_SkipUpdate).
static s32 CoopAim_CanAimAt(s32 i) {
    const Player* p = &gPlayer[i];

    if (i == 0) {
        return 1;
    }
    return !CoopPlayers_IsParked(i) && ((p->state == PLAYERSTATE_ACTIVE) || (p->state == PLAYERSTATE_U_TURN));
}

int32_t CoopAim_Nearest(float x, float y, float z) {
    s32 best = 0;
    f32 bestDist = 0.0f;
    s32 i;

    if (!CoopPlayers_IsCoop() || (gPlayer == NULL)) {
        return 0;
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        const Player* p = &gPlayer[i];
        f32 dx;
        f32 dy;
        f32 dz;
        f32 dist;

        if (!CoopAim_CanAimAt(i)) {
            continue;
        }
        dx = p->pos.x - x;
        dy = p->pos.y - y;
        dz = p->trueZpos - z;
        dist = (dx * dx) + (dy * dy) + (dz * dz);
        if ((i == 0) || (dist < bestDist)) { // strictly nearer: a tie stays with the lower index
            best = i;
            bestDist = dist;
        }
    }
    return best;
}
