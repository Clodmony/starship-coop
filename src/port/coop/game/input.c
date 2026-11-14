#include "input.h"

#include <string.h>

static const CoopInputSource* sSources[COOP_PLAYERS_MAX];
static const CoopInputSource* sLocalSource = NULL;
static CoopInputFrame sController;
static int32_t sControllerRead = 0;

void CoopInput_SetSource(int32_t player, const CoopInputSource* source) {
    if ((player >= 0) && (player < COOP_PLAYERS_MAX)) {
        sSources[player] = source;
    }
}

void CoopInput_SetLocalSource(const CoopInputSource* source) {
    sLocalSource = source;
}

void CoopInput_ClearSources(void) {
    memset(sSources, 0, sizeof(sSources));
    sLocalSource = NULL;
}

void CoopInput_ClearSource(const CoopInputSource* source) {
    int32_t i;

    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        if (sSources[i] == source) {
            sSources[i] = NULL;
        }
    }
    if (sLocalSource == source) {
        sLocalSource = NULL;
    }
}

// hold and press are port 0, fresh from Controller_UpdateInput. Hold and the sticks of press can be trusted, as both
// come from the hardware read. The buttons of press cannot: Controller_UpdateInput compares with what the slot held at
// the end of the last frame, and that was a stream if player 0 is not local (NOTES.md, 2026-11-06, #68).
static void CoopInput_ReadController(const OSContPad* hold, const OSContPad* press) {
    uint16_t last = sControllerRead ? sController.hold.button : hold->button;

    sController.hold = *hold;
    sController.press = *press;
    sController.press.button = hold->button & ~last;
    sControllerRead = 1;
}

static void CoopInput_ReadPlayer(int32_t player, int32_t count, int32_t local, CoopInputFrame* out) {
    const CoopInputSource* source = (player == local) ? sLocalSource : sSources[player];

    memset(out, 0, sizeof(*out));
    if (player >= count) {
        return;
    }
    if (source != NULL) {
        if (!source->read(source->user, player, out)) {
            memset(out, 0, sizeof(*out));
        }
    } else if (player == local) {
        *out = sController;
    }
}

void CoopInput_Route(int32_t count, int32_t local, OSContPad* hold, OSContPad* press) {
    CoopInputFrame frame;
    int32_t i;

    // First, because slot 0 is about to get player 0's input, which is another player's on a client.
    CoopInput_ReadController(&hold[0], &press[0]);
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        CoopInput_ReadPlayer(i, count, local, &frame);
        hold[i] = frame.hold;
        press[i] = frame.press;
    }
}

void CoopInput_Rest(void) {
    sControllerRead = 0;
}

const CoopInputFrame* CoopInput_Controller(void) {
    return &sController;
}

int32_t CoopInput_RumbleSlot(int32_t player, int32_t local) {
    if (player == local) {
        return 0;
    }
    return (player == 0) ? local : player;
}
