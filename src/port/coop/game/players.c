#include "players.h"

#include "input.h"
#include "vehicles.h"

#include "global.h"
#include "public/bridge/consolevariablebridge.h"

// Set once per level by CoopPlayers_LevelInit, so the count cannot change while gPlayer is allocated for it.
static s32 sCoopPlayerCount = 1;

// Players 1 and up in co-op. A player that is not JOINED is parked: allocated but not updated, not given a camera
// update and not drawn (its draw flag is 0). They start WAITING and join when player 0 flies a section that supports
// them (see CoopPlayers_SectionSupported). A joined player whose section ends is AWAY: it comes back with its own
// shields, wings and weapons if player 0 flies again in this level (rails, the cut to all-range, all-range), and
// otherwise stays parked until the next Play_Init. A joined player that goes down is DOWN: it comes back beside
// player 0 for a life from the pool (decision 0007 rule 4, CoopPlayers_ReturnFromDown), or at the next Play_Init for
// free. OUT is a slot nobody in the roster flies, for the whole level.
typedef enum {
    COOP_SLOT_JOINED, // also every player outside co-op, and player 0
    COOP_SLOT_WAITING,
    COOP_SLOT_AWAY,
    COOP_SLOT_OUT,
    COOP_SLOT_DOWN, // last, so the numbers above keep what they meant in a hash log
} CoopSlot;

static CoopSlot sSlot[COOP_PLAYERS_MAX];

// Decision 0007 rule 4: a player 2-4 who went down comes back once its counter has run out, the pool (gLifeCount[0])
// holds a life and player 0 flies a section co-op players fly in. The counter is the delay versus uses
// (src/engine/fox_versus.c:594, :622-623): 150 game frames, 5 s at 30 frames a second. It counts in
// CoopPlayers_SkipUpdate, so in game frames, inside the player loop, and not while paused. Simulation state: hashed.
#define COOP_RETURN_FRAMES 150
static s32 sReturnTimer[COOP_PLAYERS_MAX];

// The mercyTimer a returning player gets, in game frames: no collisions while it runs (Player_CollisionCheck,
// Player_Collide). 0007 leaves the value to WP-16; 30 (1 s) is a starting value, to tune by play. A hit gives 20.
#define COOP_RETURN_MERCY_FRAMES 30

// The session's roster, and how many players it makes; 0 without one.
static u8 sRoster[COOP_PLAYERS_MAX];
static s32 sRosterCount = 0;

// The player this client looks through, as asked for. Only counts in co-op, and only for a player in the level.
static s32 sLocalIndex = 0;

// How far a joining player is placed from player 0, per player index (x to the side, y up), in world units.
#define COOP_JOIN_OFFSET_X 250.0f
#define COOP_JOIN_OFFSET_Y 80.0f

void CoopPlayers_LevelInit(void) {
    s32 count = 1;
    s32 i;

    if (gCamCount == 1) {
        count = (sRosterCount > 0) ? sRosterCount : CVarGetInteger(COOP_PLAYERS_DEBUG_CVAR, 1);
        if (count < 1) {
            count = 1;
        }
        if (count > COOP_PLAYERS_MAX) {
            count = COOP_PLAYERS_MAX;
        }
    }
    sCoopPlayerCount = count;
    CoopVehicles_LevelInit();

    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        sReturnTimer[i] = 0; // a return still waiting is dropped: the player joins again below, for free
        sSlot[i] = ((gCamCount == 1) && (i > 0) && (i < count)) ? COOP_SLOT_WAITING : COOP_SLOT_JOINED;
        if ((sSlot[i] == COOP_SLOT_WAITING) && (sRosterCount > 0) && (sRoster[i] == 0)) {
            sSlot[i] = COOP_SLOT_OUT;
        }
    }
}

void CoopPlayers_SetRoster(const uint8_t* roster) {
    s32 i;

    sRosterCount = 0;
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        sRoster[i] = (roster != NULL) ? roster[i] : 0;
        if (sRoster[i] != 0) {
            sRosterCount = i + 1;
        }
    }
}

int32_t CoopPlayers_Character(int32_t index) {
    return ((index >= 0) && (index < COOP_PLAYERS_MAX)) ? sRoster[index] : 0;
}

int32_t CoopPlayers_Count(void) {
    if (gCamCount != 1) {
        return gCamCount; // versus: one player per camera, as upstream
    }
    return sCoopPlayerCount;
}

int32_t CoopPlayers_IsCoop(void) {
    return CoopPlayers_Count() > gCamCount;
}

int32_t CoopPlayers_LocalIndex(void) {
    if (!CoopPlayers_IsCoop() || (sLocalIndex >= CoopPlayers_Count())) {
        return 0;
    }
    return sLocalIndex;
}

int32_t CoopPlayers_ViewIndex(void) {
    return CoopPlayers_IsParked(CoopPlayers_LocalIndex()) ? 0 : CoopPlayers_LocalIndex();
}

int32_t CoopPlayers_IsViewed(int32_t index) {
    return !CoopPlayers_IsCoop() || (index == CoopPlayers_ViewIndex());
}

int32_t CoopPlayers_HudIndex(void) {
    return CoopPlayers_IsCoop() ? CoopPlayers_ViewIndex() : gPlayerNum;
}

void CoopPlayers_SetLocalIndex(int32_t index) {
    sLocalIndex = ((index < 0) || (index >= COOP_PLAYERS_MAX)) ? 0 : index;
}

int32_t CoopPlayers_DmgSource(int32_t sourceId) {
    if (CoopPlayers_IsCoop() && (sourceId > 0) && (sourceId < COOP_PLAYERS_MAX)) {
        return COOP_DMG_SRC_PLAYER + sourceId;
    }
    return sourceId + 1;
}

int32_t CoopPlayers_IsParked(int32_t index) {
    if ((index < 0) || (index >= COOP_PLAYERS_MAX) || !CoopPlayers_IsCoop()) {
        return 0;
    }
    return sSlot[index] != COOP_SLOT_JOINED;
}

// Player i becomes a copy of player 0, set beside it.
static void CoopPlayers_PlaceBesidePlayer0(s32 i) {
    Player* p = &gPlayer[i];

    *p = gPlayer[0];
    p->num = i;
    p->sfx.srcPos = p->sfxSource; // the copy points at player 0's, and the engine sound keeps the pointer it is given
    p->sfx.srcVel = p->sfxVel;
    p->pos.x += COOP_JOIN_OFFSET_X * i;
    p->pos.y += COOP_JOIN_OFFSET_Y * i;
    p->cam.eye.x += COOP_JOIN_OFFSET_X * i;
    p->cam.eye.y += COOP_JOIN_OFFSET_Y * i;
    p->cam.at.x += COOP_JOIN_OFFSET_X * i;
    p->cam.at.y += COOP_JOIN_OFFSET_Y * i;
}

// Player i joins for the first time in this Play_Init. Player_Setup is not run for it: besides the player struct it
// resets level-wide state (radio, music, the level start screen, intro flags), which would disturb player 0. So the
// per-player arrays that Player_Setup fills for its player index are copied from player 0 instead.
static void CoopPlayers_JoinFromPlayer0(s32 i) {
    CoopPlayers_PlaceBesidePlayer0(i);

#define COPY_FROM_0(arr) (arr)[i] = (arr)[0]
    COPY_FROM_0(gLeftWingHealth);
    COPY_FROM_0(gRightWingHealth);
    COPY_FROM_0(gUturnDownTimers);
    COPY_FROM_0(gUturnBrakeTimers);
    COPY_FROM_0(gLoopDownTimers);
    COPY_FROM_0(gLoopBoostTimers);
    COPY_FROM_0(gShieldTimer);
    COPY_FROM_0(gHasShield);
    COPY_FROM_0(gChargeTimers);
    COPY_FROM_0(gRightWingFlashTimer);
    COPY_FROM_0(gLeftWingFlashTimer);
    COPY_FROM_0(gRightWingDebrisTimer);
    COPY_FROM_0(gLeftWingDebrisTimer);
    COPY_FROM_0(D_ctx_80177990);
    COPY_FROM_0(D_ctx_801779A8);
    COPY_FROM_0(gMuzzleFlashScale);
    COPY_FROM_0(gShieldAlpha);
    COPY_FROM_0(gLaserStrength);
    COPY_FROM_0(gBombCount);
    COPY_FROM_0(gLifeCount);
#undef COPY_FROM_0
}

// Player i comes back for the next section of the level. What it lost or picked up so far stays its own.
static void CoopPlayers_RejoinFromPlayer0(s32 i) {
    Player* p = &gPlayer[i];
    s32 shields = p->shields;
    s32 damage = p->damage;
    s32 heal = p->heal;
    s32 rightWing = p->arwing.rightWingState;
    s32 leftWing = p->arwing.leftWingState;

    CoopPlayers_PlaceBesidePlayer0(i);
    p->shields = shields;
    p->damage = damage;
    p->heal = heal;
    p->arwing.rightWingState = rightWing;
    p->arwing.leftWingState = leftWing;
}

// Player i comes back after it went down (decision 0007 rule 4): beside player 0 as at a join, and as player 0 comes
// back after a death (Player_Setup, src/engine/fox_play.c): full shields, both wings, 3 bombs, a single laser, no
// timers or damage left from the copy, plus a short mercyTimer. The life is spent here, from the pool, not at the
// crash (0007 rule 4).
static void CoopPlayers_ReturnFromDown(s32 i) {
    Player* p = &gPlayer[i];

    gLifeCount[0]--;
    CoopPlayers_PlaceBesidePlayer0(i);
    p->shields = Play_GetMaxShields(); // 255, or 383 with the team's shield upgrade from three gold rings
    p->damage = p->heal = 0;
    p->hitTimer = p->dmgEffectTimer = p->dmgEffect = p->radioDamageTimer = 0;
    p->damageShake = 0.0f;
    p->knockback.x = p->knockback.y = p->knockback.z = 0.0f;
    p->mercyTimer = COOP_RETURN_MERCY_FRAMES;
    p->arwing.rightWingState = p->arwing.leftWingState = WINGSTATE_INTACT;
    p->arwing.laserGunsYpos = 0.0f; // single laser: the guns in (Player_Shoot)
    CoopPlayers_SetLaserGunsX(p);

    gLeftWingHealth[i] = gRightWingHealth[i] = gExpertMode ? 10 : 60;
    gUturnDownTimers[i] = gUturnBrakeTimers[i] = gLoopDownTimers[i] = gLoopBoostTimers[i] = gShieldTimer[i] =
        gHasShield[i] = gChargeTimers[i] = 0;
    gRightWingFlashTimer[i] = gLeftWingFlashTimer[i] = gRightWingDebrisTimer[i] = gLeftWingDebrisTimer[i] =
        D_ctx_80177990[i] = D_ctx_801779A8[i] = gMuzzleFlashScale[i] = gShieldAlpha[i] = 0.0f;
    gLaserStrength[i] = LASERS_SINGLE;
    gBombCount[i] = 3;
    gLifeCount[i] = gLifeCount[0]; // decides nothing (0007 rule 1); the HUD of this player's client shows it (WP-28)
}

static void CoopPlayers_SetDown(s32 i) {
    sSlot[i] = COOP_SLOT_DOWN;
    sReturnTimer[i] = COOP_RETURN_FRAMES;
    gPlayer[i].draw = false;
    Audio_StopPlayerNoise(i);
}

// Nonzero while the level is in a section co-op players fly in: all-range (WP-11), rails in an Arwing (WP-12), or a
// vehicle level that WP-18 has proven (#16): the Landmaster in Titania and Macbeth, the Blue Marine in Aquas (decision
// 0009, item 6).
static s32 CoopPlayers_SectionSupported(void) {
    if (gLevelMode == LEVELMODE_ALL_RANGE) {
        return 1;
    }
    if (gLevelMode != LEVELMODE_ON_RAILS) {
        return 0;
    }
    return (gPlayer[0].form == FORM_ARWING) ||
           ((gPlayer[0].form == FORM_LANDMASTER) &&
            ((gCurrentLevel == LEVEL_TITANIA) || (gCurrentLevel == LEVEL_MACBETH))) ||
           ((gPlayer[0].form == FORM_BLUE_MARINE) && (gCurrentLevel == LEVEL_AQUAS));
}

int32_t CoopPlayers_SkipUpdate(int32_t index) {
    if ((index <= 0) || !CoopPlayers_IsCoop() || (index >= COOP_PLAYERS_MAX)) {
        return 0;
    }
    switch (sSlot[index]) {
        case COOP_SLOT_JOINED:
            // Leave with player 0: once it is no longer flying (level complete, down, the cut from rails to all-range),
            // this player's section is over.
            if (!CoopPlayers_SectionSupported() ||
                ((gPlayer[0].state != PLAYERSTATE_ACTIVE) && (gPlayer[0].state != PLAYERSTATE_U_TURN))) {
                sSlot[index] = COOP_SLOT_AWAY;
                gPlayer[index].draw = false;
                Audio_StopPlayerNoise(index);
                return 1;
            }
            return 0;

        case COOP_SLOT_WAITING:
            if (CoopPlayers_SectionSupported() && (gPlayer[0].state == PLAYERSTATE_ACTIVE)) {
                CoopPlayers_JoinFromPlayer0(index);
                sSlot[index] = COOP_SLOT_JOINED;
                Audio_StartPlayerNoise(index); // Player_Setup does this for player 0
            }
            return 1; // a player that joined this frame starts updating on the next one

        case COOP_SLOT_AWAY:
            if (CoopPlayers_SectionSupported() && (gPlayer[0].state == PLAYERSTATE_ACTIVE)) {
                CoopPlayers_RejoinFromPlayer0(index);
                sSlot[index] = COOP_SLOT_JOINED;
                Audio_StartPlayerNoise(index);
            }
            return 1;

        case COOP_SLOT_DOWN:
            // Player_Update turns draw back on after a Player_Down in the same frame, so hide it again here.
            gPlayer[index].draw = false;
            if (sReturnTimer[index] > 0) {
                sReturnTimer[index]--;
            }
            // Player 0 updates first, so a return due on the frame it goes down is refused, and with one life left
            // and several waiting the lowest index takes it (0007, "Every client decides alike"). At pool 0 the player
            // stays out until a life comes in or the next Play_Init (rule 5).
            if ((sReturnTimer[index] == 0) && (gLifeCount[0] > 0) && CoopPlayers_SectionSupported() &&
                (gPlayer[0].state == PLAYERSTATE_ACTIVE)) {
                CoopPlayers_ReturnFromDown(index);
                sSlot[index] = COOP_SLOT_JOINED;
                Audio_StartPlayerNoise(index);
            }
            return 1; // as for a join: it starts updating on the next frame

        case COOP_SLOT_OUT:
        default:
            gPlayer[index].draw = false;
            return 1;
    }
}

// Forward speed on the rails is the team's (#194): any player's boost speeds the level for everyone, and when nobody
// boosts the strongest brake slows it. boostSpeed is what single player uses (ramps +2 a frame to 30, -1 a frame back,
// brake to -20), so the level speeds up as smoothly as it does for Fox alone. Boost wins over a brake. Only players
// that fly count; player 0 always does, as it owns the path. Every game reads the same players in the same order, so
// the result is the same everywhere. A player later in the loop is read as it stood after the previous frame.
f32 CoopPlayers_PathBoostSpeed(const Player* player) {
    f32 hi = gPlayer[0].boostSpeed;
    f32 lo = hi;
    s32 i;

    if (!CoopPlayers_IsCoop()) {
        return player->boostSpeed;
    }
    for (i = 1; i < CoopPlayers_Count(); i++) {
        if (!CoopPlayers_IsParked(i) && (gPlayer[i].state == PLAYERSTATE_ACTIVE)) {
            hi = MAX(hi, gPlayer[i].boostSpeed);
            lo = MIN(lo, gPlayer[i].boostSpeed);
        }
    }
    return (hi > 0.0f) ? hi : lo;
}

// Player 0 owns the path: everything that hangs off it reads player 0 or the globals player 0 writes (object loading,
// culling, path turns, floor collision, ground scroll), and it is the one player every client has. See NOTES.md, WP-12.
int32_t CoopPlayers_FollowPath(Player* player) {
    const Player* owner = &gPlayer[0];

    if ((player->num <= 0) || !CoopPlayers_IsCoop()) {
        return 0;
    }
    player->pos.z = owner->pos.z;
    player->vel.z = owner->vel.z;
    player->baseSpeed = owner->baseSpeed;
    player->zPath = owner->zPath;
    player->zPathVel = owner->zPathVel;
    player->xPath = owner->xPath;
    player->yPath = owner->yPath;
    player->xPathTarget = owner->xPathTarget;
    player->yPathTarget = owner->yPathTarget;
    player->pathStep = owner->pathStep;
    player->pathChangeTimer = owner->pathChangeTimer;
    player->pathChangeYaw = owner->pathChangeYaw;
    player->pathChangePitch = owner->pathChangePitch;
    player->yRot_114 = owner->yRot_114;
    player->xRot_120 = owner->xRot_120;

    // The rest of Player_UpdatePath that is about the player itself.
    player->trueZpos = player->pos.z + player->camDist;
    player->bankAngle = player->rot.z + player->zRotBank + player->zRotBarrelRoll;
    while (player->bankAngle > 360.0f) {
        player->bankAngle -= 360.0f;
    }
    while (player->bankAngle < -360.0f) {
        player->bankAngle += 360.0f;
    }
    return 1;
}

void CoopPlayers_SelectInput(int32_t index) {
    if (!CoopPlayers_IsCoop()) {
        return;
    }
    gInputHold = &gControllerHold[index];
    gInputPress = &gControllerPress[index];
    gControllerRumble = &gControllerRumbleFlags[CoopInput_RumbleSlot(index, CoopPlayers_LocalIndex())];
}

int32_t CoopPlayers_PadOf(int32_t index) {
    return CoopPlayers_IsCoop() ? index : gMainController;
}

int32_t CoopPlayers_ReleaseLockOns(Actor* actor) {
    bool rapidFire;
    s32 i;

    if (!CoopPlayers_IsCoop()) {
        return 0;
    }
    rapidFire = CVarGetInteger("gRapidFire", 0) == 1;
    for (i = 0; i < CoopPlayers_Count(); i++) {
        if (actor->lockOnTimers[i] != 0) {
            if (!(gControllerHold[i].button & A_BUTTON) || rapidFire) {
                actor->lockOnTimers[i]--;
            }
            gChargeTimers[i] = 0;
        }
    }
    return 1;
}

int32_t CoopPlayers_OnPlayerDown(Player* player) {
    s32 i = player->num;

    if ((i <= 0) || (i >= COOP_PLAYERS_MAX) || !CoopPlayers_IsCoop()) {
        return 0;
    }
    CoopPlayers_SetDown(i);
    return 1;
}

int32_t CoopPlayers_SkipLowHealthMsg(Player* player) {
    return (player->num > 0) && CoopPlayers_IsCoop();
}

void CoopPlayers_SetLaserGunsX(Player* player) {
    if (!CoopPlayers_IsCoop()) {
        return;
    }
    // Display_ArwingWings, src/engine/fox_display.c
    player->arwing.laserGunsXpos = 0.0f;
    if (player->arwing.laserGunsYpos < -7.0f) {
        player->arwing.laserGunsXpos = (-player->arwing.laserGunsYpos - 7.0f) * 2.5f;
    }
}

int32_t CoopPlayers_PausePressed(void) {
    s32 i;

    if (!CoopPlayers_IsCoop()) {
        return gControllerPress[gMainController].button & START_BUTTON;
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        if (gControllerPress[i].button & START_BUTTON) {
            return 1;
        }
    }
    return 0;
}

int32_t CoopPlayers_SlotState(int32_t index) {
    if ((index < 0) || (index >= COOP_PLAYERS_MAX)) {
        return COOP_SLOT_JOINED;
    }
    return sSlot[index];
}

void CoopPlayers_SetSlotState(int32_t index, int32_t state) {
    if ((index >= 0) && (index < COOP_PLAYERS_MAX)) {
        sSlot[index] = (CoopSlot) state;
    }
}

// The rate of Play_UpdateLevel's LEVEL_SOLAR drain for a height: one point every (mask + 1) frames. Same steps as
// there (src/engine/fox_play.c), which keeps its own copy for player 0 so that co-op off stays upstream's code.
static s32 SolarHeatMask(f32 y) {
    if (y > 500.0f) {
        return 8 - 1;
    }
    if (y > 300.0f) {
        return 4 - 1;
    }
    if (y > 200.0f) {
        return 2 - 1;
    }
    return 0;
}

void CoopPlayers_SolarHeat(void) {
    s32 i;

    if (!CoopPlayers_IsCoop() || CVarGetInteger("gInvincible", 0)) {
        return;
    }
    for (i = 1; i < CoopPlayers_Count(); i++) {
        Player* player = &gPlayer[i];

        if (CoopPlayers_IsParked(i) || (player->state != PLAYERSTATE_ACTIVE) ||
            ((gGameFrameCount & SolarHeatMask(player->pos.y)) != 0)) {
            continue;
        }
        player->shields--;
        if (player->shields <= 0) {
            player->shields = 0;
        }
        if ((player->heal == 0) && CoopPlayers_IsViewed(i)) {
            if (player->shields == 50) {
                AUDIO_PLAY_SFX(NA_SE_SHIELD_WARNING1, gDefaultSfxSource, 4);
            } else if (player->shields == 100) {
                AUDIO_PLAY_SFX(NA_SE_SHIELD_WARNING0, gDefaultSfxSource, 4);
            }
        }
    }
}

int32_t CoopPlayers_IsDown(int32_t index) {
    return (index >= 0) && (index < COOP_PLAYERS_MAX) && (sSlot[index] == COOP_SLOT_DOWN);
}

int32_t CoopPlayers_ReturnTimer(int32_t index) {
    return ((index >= 0) && (index < COOP_PLAYERS_MAX)) ? sReturnTimer[index] : 0;
}

void CoopPlayers_SetReturnTimer(int32_t index, int32_t frames) {
    if ((index >= 0) && (index < COOP_PLAYERS_MAX)) {
        sReturnTimer[index] = frames;
    }
}
