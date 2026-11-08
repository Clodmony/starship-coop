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

// src/overlays/ovl_i3/fox_aq.c
extern f32 D_AQ_801C4188;
extern s32 D_i3_801C4190[10];
extern f32 D_i3_801C41B8[30];
extern s32 D_i3_801C4454;
extern f32 D_i3_801C4458;
extern f32 D_i3_801C445C;
extern f32 D_i3_801BFB60;

CoopTankState gCoopTankState[COOP_PLAYERS_MAX];
CoopMarineState gCoopMarineState[COOP_PLAYERS_MAX];

// Which entries of the two fox_aq.c arrays belong to one marine (decision 0009, item 6; NOTES.md, #16).
static const u8 sMarineInts[COOP_MARINE_INTS] = { 0, 1, 3, 4, 5, 7 };
static const u8 sMarineFloats[COOP_MARINE_FLOATS] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 21, 22, 23 };

// The level state a marine's update steps: light and fog (Aquas_801A99D4), the slowdown before the boss, and the one
// lamp (Aquas_BlueMarineShoot). Player 0 steps it, as in one-player play; another marine's writes are undone.
typedef struct {
    s32 ints[10];
    f32 floats[30];
    f32 fog;
    f32 fogTarget;
    f32 slowdown;
    s32 light1[3];
    s32 ambient[3];
    s32 lamp[3];
    f32 lampPos[3];
    f32 lampBrightness;
} AquasLevel;

// Player 0's values while another player's are swapped in.
static CoopTankState sHome;
static s32 sSwapped = -1;

static CoopMarineState sMarineHome;
static AquasLevel sAquasLevel;
static s32 sMarineSwapped = -1;
static s32 sShotSwapped = -1;

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

static void Marine_Save(CoopMarineState* state) {
    s32 i;

    for (i = 0; i < COOP_MARINE_INTS; i++) {
        state->ints[i] = D_i3_801C4190[sMarineInts[i]];
    }
    for (i = 0; i < COOP_MARINE_FLOATS; i++) {
        state->floats[i] = D_i3_801C41B8[sMarineFloats[i]];
    }
    state->torpedoTimer = D_i3_801C4454;
    state->torpedoSpeed = D_i3_801C4458;
    state->torpedoTurn = D_i3_801C445C;
}

static void Marine_Load(const CoopMarineState* state) {
    s32 i;

    for (i = 0; i < COOP_MARINE_INTS; i++) {
        D_i3_801C4190[sMarineInts[i]] = state->ints[i];
    }
    for (i = 0; i < COOP_MARINE_FLOATS; i++) {
        D_i3_801C41B8[sMarineFloats[i]] = state->floats[i];
    }
    D_i3_801C4454 = state->torpedoTimer;
    D_i3_801C4458 = state->torpedoSpeed;
    D_i3_801C445C = state->torpedoTurn;
}

// Whole arrays: the marine's own entries are loaded over them afterwards, and saved before they are restored.
static void AquasLevel_Save(AquasLevel* level) {
    memcpy(level->ints, D_i3_801C4190, sizeof(level->ints));
    memcpy(level->floats, D_i3_801C41B8, sizeof(level->floats));
    level->fog = D_bg_8015F970;
    level->fogTarget = D_AQ_801C4188;
    level->slowdown = D_i3_801BFB60;
    level->light1[0] = gLight1R;
    level->light1[1] = gLight1G;
    level->light1[2] = gLight1B;
    level->ambient[0] = gAmbientR;
    level->ambient[1] = gAmbientG;
    level->ambient[2] = gAmbientB;
    level->lamp[0] = gLight3R;
    level->lamp[1] = gLight3G;
    level->lamp[2] = gLight3B;
    level->lampPos[0] = gLight3x;
    level->lampPos[1] = gLight3y;
    level->lampPos[2] = gLight3z;
    level->lampBrightness = gLight3Brightness;
}

static void AquasLevel_Load(const AquasLevel* level) {
    memcpy(D_i3_801C4190, level->ints, sizeof(level->ints));
    memcpy(D_i3_801C41B8, level->floats, sizeof(level->floats));
    D_bg_8015F970 = level->fog;
    D_AQ_801C4188 = level->fogTarget;
    D_i3_801BFB60 = level->slowdown;
    gLight1R = level->light1[0];
    gLight1G = level->light1[1];
    gLight1B = level->light1[2];
    gAmbientR = level->ambient[0];
    gAmbientG = level->ambient[1];
    gAmbientB = level->ambient[2];
    gLight3R = level->lamp[0];
    gLight3G = level->lamp[1];
    gLight3B = level->lamp[2];
    gLight3x = level->lampPos[0];
    gLight3y = level->lampPos[1];
    gLight3z = level->lampPos[2];
    gLight3Brightness = level->lampBrightness;
}

static s32 OwnSlot(s32 num) {
    return CoopPlayers_IsCoop() && (num > 0) && (num < COOP_PLAYERS_MAX);
}

static s32 OwnMarine(const Player* player) {
    return OwnSlot(player->num) && (gCurrentLevel == LEVEL_AQUAS) && (player->form == FORM_BLUE_MARINE);
}

// A torpedo another marine fired: Aquas_801ABA40 steers it by the lock-on state of whoever owns the arrays.
static s32 OwnTorpedo(const PlayerShot* shot) {
    return (shot->obj.status == SHOT_ACTIVE) && (shot->obj.id == PLAYERSHOT_LOCK_ON) && OwnSlot(shot->sourceId) &&
           (gCurrentLevel == LEVEL_AQUAS) && (gPlayer[shot->sourceId].form == FORM_BLUE_MARINE);
}

static s32 OwnTank(const Player* player) {
    return CoopPlayers_IsCoop() && (player->num > 0) && (player->num < COOP_PLAYERS_MAX) &&
           (player->form == FORM_LANDMASTER);
}

static void CoopVehicles_OnPlayerPreUpdate(IEvent* event) {
    Player* player = ((PlayerPreUpdateEvent*) event)->player;

    if (OwnMarine(player)) {
        AquasLevel_Save(&sAquasLevel);
        Marine_Save(&sMarineHome);
        Marine_Load(&gCoopMarineState[player->num]);
        sMarineSwapped = player->num;
    }
    if (!OwnTank(player)) {
        return;
    }
    Tank_Save(&sHome);
    Tank_Load(&gCoopTankState[player->num]);
    sSwapped = player->num;
}

static void CoopVehicles_OnPlayerPostUpdate(IEvent* event) {
    Player* player = ((PlayerPostUpdateEvent*) event)->player;

    if (sMarineSwapped == player->num) {
        Marine_Save(&gCoopMarineState[sMarineSwapped]);
        AquasLevel_Load(&sAquasLevel);
        Marine_Load(&sMarineHome);
        sMarineSwapped = -1;
    }
    if (sSwapped != player->num) {
        return;
    }
    Tank_Save(&gCoopTankState[sSwapped]);
    Tank_Load(&sHome);
    sSwapped = -1;
}

static void CoopVehicles_OnShotPreUpdate(IEvent* event) {
    PlayerShot* shot = ((PlayerShotPreUpdateEvent*) event)->shot;

    if (!OwnTorpedo(shot)) {
        return;
    }
    Marine_Save(&sMarineHome);
    Marine_Load(&gCoopMarineState[shot->sourceId]);
    sShotSwapped = shot->sourceId;
}

// The shot may be gone by now, so this keys on what the pre-update swapped.
static void CoopVehicles_OnShotPostUpdate(IEvent* event) {
    if (sShotSwapped < 0) {
        return;
    }
    Marine_Save(&gCoopMarineState[sShotSwapped]);
    Marine_Load(&sMarineHome);
    sShotSwapped = -1;
}

void CoopVehicles_MarineLock(int32_t player, int32_t* target, int32_t* homing) {
    if (player == 0) {
        *target = D_i3_801C4190[0];
        *homing = D_i3_801C4190[3];
    } else if ((player > 0) && (player < COOP_PLAYERS_MAX)) {
        *target = gCoopMarineState[player].ints[0]; // sMarineInts[0] = 0
        *homing = gCoopMarineState[player].ints[2]; // sMarineInts[2] = 3
    } else {
        *target = *homing = 0;
    }
}

void CoopVehicles_LevelInit(void) {
    memset(gCoopTankState, 0, sizeof(gCoopTankState));
    memset(gCoopMarineState, 0, sizeof(gCoopMarineState));
    sSwapped = -1;
    sMarineSwapped = -1;
    sShotSwapped = -1;
}

void CoopVehicles_Init(void) {
    REGISTER_LISTENER(PlayerPreUpdateEvent, CoopVehicles_OnPlayerPreUpdate, EVENT_PRIORITY_NORMAL);
    REGISTER_LISTENER(PlayerPostUpdateEvent, CoopVehicles_OnPlayerPostUpdate, EVENT_PRIORITY_NORMAL);
    REGISTER_LISTENER(PlayerShotPreUpdateEvent, CoopVehicles_OnShotPreUpdate, EVENT_PRIORITY_NORMAL);
    REGISTER_LISTENER(PlayerShotPostUpdateEvent, CoopVehicles_OnShotPostUpdate, EVENT_PRIORITY_NORMAL);
}
