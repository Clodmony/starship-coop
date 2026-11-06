#include "test_pad.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "port/hooks/Events.h"

// src/sys/sys_joybus.c
extern OSContPad sNextController[4];

#define TEST_PAD_PORTS 4
#define TEST_PAD_MAX_STEPS 4096
#define TEST_PAD_STICK_RANGE 85 // MAX_AXIS_RANGE in libultraship: a full stick

typedef struct {
    u32 frame;
    s32 port;
    u32 button;
    s32 stickX;
    s32 stickY;
} TestPadStep;

// The default gamepad mappings, from the table in src/port/Engine.cpp.
static const struct {
    u16 button;
    SDL_GameControllerButton sdl;
} sButtons[] = {
    { A_BUTTON, SDL_CONTROLLER_BUTTON_A },
    { B_BUTTON, SDL_CONTROLLER_BUTTON_X },
    { START_BUTTON, SDL_CONTROLLER_BUTTON_START },
    { R_TRIG, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
    { Z_TRIG, SDL_CONTROLLER_BUTTON_LEFTSHOULDER },
};

static TestPadStep* sSteps = NULL;
static s32 sStepCount = 0;
static s32 sNextStep = 0;
static SDL_Joystick* sJoystick = NULL;
static OSContPad sPorts[TEST_PAD_PORTS];
static bool sStarted = false;
static s32 sFrame = -1;

static void CoopTestPad_Attach(void) {
    s32 index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX,
                                          SDL_CONTROLLER_BUTTON_MAX, 0);

    sJoystick = (index < 0) ? NULL : SDL_JoystickOpen(index);
    if (sJoystick == NULL) {
        fprintf(stderr, "test pad: no virtual gamepad: %s\n", SDL_GetError());
    }
}

static void CoopTestPad_PressPort0(const TestPadStep* step) {
    size_t i;

    if (sJoystick == NULL) {
        return;
    }
    for (i = 0; i < ARRAY_COUNT(sButtons); i++) {
        SDL_JoystickSetVirtualButton(sJoystick, sButtons[i].sdl, (step->button & sButtons[i].button) ? 1 : 0);
    }
    SDL_JoystickSetVirtualAxis(sJoystick, SDL_CONTROLLER_AXIS_LEFTX,
                               (Sint16) (step->stickX * SDL_JOYSTICK_AXIS_MAX / TEST_PAD_STICK_RANGE));
    // Up on a stick is positive here and negative on a gamepad.
    SDL_JoystickSetVirtualAxis(sJoystick, SDL_CONTROLLER_AXIS_LEFTY,
                               (Sint16) (-step->stickY * SDL_JOYSTICK_AXIS_MAX / TEST_PAD_STICK_RANGE));
}

static void CoopTestPad_OnGamePreUpdate(IEvent* event) {
    s32 port;

    (void) event;
    if (sJoystick == NULL) {
        CoopTestPad_Attach();
    }
    // As in replay.c: the boot logo waits on the clock, so frames count from GSTATE_START.
    if (!sStarted) {
        if (gGameState != GSTATE_START) {
            return;
        }
        sStarted = true;
    }
    sFrame++;
    while ((sNextStep < sStepCount) && (sSteps[sNextStep].frame <= (u32) sFrame)) {
        const TestPadStep* step = &sSteps[sNextStep++];

        if (step->port == 0) {
            CoopTestPad_PressPort0(step);
        } else {
            sPorts[step->port].button = step->button;
            sPorts[step->port].stick_x = step->stickX;
            sPorts[step->port].stick_y = step->stickY;
        }
    }
    // Controller_ReadData has already run this frame, so this is what the next Controller_UpdateInput takes.
    for (port = 1; port < TEST_PAD_PORTS; port++) {
        sNextController[port] = sPorts[port];
    }
}

void CoopTestPad_Init(void) {
    const char* path = getenv("SF64_COOP_TEST_PAD");
    TestPadStep step;
    FILE* file;

    if ((path == NULL) || (path[0] == '\0')) {
        return;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "test pad: cannot read %s\n", path);
        return;
    }
    sSteps = malloc(sizeof(TestPadStep) * TEST_PAD_MAX_STEPS);
    while ((sSteps != NULL) && (sStepCount < TEST_PAD_MAX_STEPS) &&
           (fscanf(file, "%u %d %x %d %d", &step.frame, &step.port, &step.button, &step.stickX, &step.stickY) == 5)) {
        if ((step.port >= 0) && (step.port < TEST_PAD_PORTS)) {
            sSteps[sStepCount++] = step;
        }
    }
    fclose(file);
    REGISTER_LISTENER(GamePreUpdateEvent, CoopTestPad_OnGamePreUpdate, EVENT_PRIORITY_NORMAL);
}

int CoopTestPad_Frame(void) {
    return sFrame;
}
