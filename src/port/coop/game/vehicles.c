#include "vehicles.h"

#include <string.h>

#include "global.h"
#include "port/hooks/Events.h"

// src/engine/fox_tank.c
extern u8 D_800C9F00;
extern u8 D_800C9F04;
extern u8 D_800C9F08;
extern f32 D_800C9F20;
extern f32 D_800C9F24;
extern u8 D_800C9F3C;

CoopTankState gCoopTankState[COOP_PLAYERS_MAX];

// Player 0's values while another player's are swapped in.
static CoopTankState sHome;
static s32 sSwapped = -1;

static void Tank_Save(CoopTankState* state) {
    state->boostStart = D_800C9F24;
    state->jetStart = D_800C9F20;
    state->knockback = D_800C9F00;
    state->hitFlag = D_800C9F04;
    state->wobble = D_800C9F08;
    state->soundGate = D_800C9F3C;
}

static void Tank_Load(const CoopTankState* state) {
    D_800C9F24 = state->boostStart;
    D_800C9F20 = state->jetStart;
    D_800C9F00 = state->knockback;
    D_800C9F04 = state->hitFlag;
    D_800C9F08 = state->wobble;
    D_800C9F3C = state->soundGate;
}

static s32 OwnTank(const Player* player) {
    return CoopPlayers_IsCoop() && (player->num > 0) && (player->num < COOP_PLAYERS_MAX) &&
           (player->form == FORM_LANDMASTER);
}

static void CoopVehicles_OnPlayerPreUpdate(IEvent* event) {
    Player* player = ((PlayerPreUpdateEvent*) event)->player;

    if (!OwnTank(player)) {
        return;
    }
    Tank_Save(&sHome);
    Tank_Load(&gCoopTankState[player->num]);
    sSwapped = player->num;
}

static void CoopVehicles_OnPlayerPostUpdate(IEvent* event) {
    Player* player = ((PlayerPostUpdateEvent*) event)->player;

    if (sSwapped != player->num) {
        return;
    }
    Tank_Save(&gCoopTankState[sSwapped]);
    Tank_Load(&sHome);
    sSwapped = -1;
}

void CoopVehicles_LevelInit(void) {
    memset(gCoopTankState, 0, sizeof(gCoopTankState));
    sSwapped = -1;
}

void CoopVehicles_Init(void) {
    REGISTER_LISTENER(PlayerPreUpdateEvent, CoopVehicles_OnPlayerPreUpdate, EVENT_PRIORITY_NORMAL);
    REGISTER_LISTENER(PlayerPostUpdateEvent, CoopVehicles_OnPlayerPostUpdate, EVENT_PRIORITY_NORMAL);
}
