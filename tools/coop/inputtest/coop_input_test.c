// Unit check of the input sources (WP-14), driven by tools/coop/input_test.py. No game and no ROM: only
// src/port/coop/game/input.c, with a copy of what src/sys/sys_joybus.c does between two frames.
//
// This is the only test of a local index other than 0, as CoopPlayers_LocalIndex() is 0 in the game until #96 and #30.
// Prints a FAIL line per failed check and a last line with the count, exits 0 when nothing failed.

#include <stdio.h>
#include <string.h>

#include "port/coop/game/input.h"

#define PORTS 4
#define FRAMES 240
#define BUTTON_A 0x8000
#define BUTTON_R 0x0010

static OSContPad sHold[PORTS];
static OSContPad sPress[PORTS];
static OSContPad sNext[PORTS]; // sNextController: what the devices say
static OSContPad sPrev[PORTS];
static int sChecks = 0;
static int sFailed = 0;

static void Check(int good, const char* what, int local, int count, int frame) {
    sChecks++;
    if (!good) {
        sFailed++;
        printf("FAIL %s (local %d, %d players, frame %d)\n", what, local, count, frame);
    }
}

static int DeadZone(int stick) {
    int out = 0;

    if (stick > 16) {
        out = stick - 16;
    } else if (stick < -16) {
        out = stick + 16;
    }
    return (out > 60) ? 60 : ((out < -60) ? -60 : out);
}

// Controller_UpdateInput, src/sys/sys_joybus.c. Keeping the last hold of the slot as the previous state is the point:
// that is how a stream written to slot 0 ends up in the press of port 0.
static void UpdateInput(void) {
    int i;

    for (i = 0; i < PORTS; i++) {
        sPrev[i] = sHold[i];
        sHold[i] = sNext[i];
        sPress[i].button = (sHold[i].button ^ sPrev[i].button) & sHold[i].button;
        sPress[i].stick_x = DeadZone(sHold[i].stick_x);
        sPress[i].stick_y = DeadZone(sHold[i].stick_y);
    }
}

static void Reset(void) {
    memset(sHold, 0, sizeof(sHold));
    memset(sPress, 0, sizeof(sPress));
    memset(sNext, 0, sizeof(sNext));
    memset(sPrev, 0, sizeof(sPrev));
    CoopInput_ClearSources();
    CoopInput_Rest();
}

static int SamePad(const OSContPad* a, const OSContPad* b) {
    return (a->button == b->button) && (a->stick_x == b->stick_x) && (a->stick_y == b->stick_y);
}

// A stream: on each frame every player has its own pad, and a press pad that is not what any rule would work out, so
// that a slot made from it can only be the stream's.
static int sFrame = 0;
static int sStreamStops = -1; // from this frame on it has nothing
static int sStreamReads = 0;

static void StreamPad(int player, int frame, CoopInputFrame* out) {
    memset(out, 0, sizeof(*out));
    out->hold.button = (uint16_t) (0x0100 << player) | (uint16_t) (frame & 3);
    out->hold.stick_x = (int8_t) (10 * (player + 1));
    out->hold.stick_y = (int8_t) (frame % 50);
    out->press.button = (uint16_t) (0x1000 << player);
    out->press.stick_x = (int8_t) (-3 * (player + 1));
    out->press.stick_y = (int8_t) (-(frame % 40));
}

static int32_t StreamRead(void* user, int32_t player, CoopInputFrame* out) {
    (void) user;
    sStreamReads++;
    if ((sStreamStops >= 0) && (sFrame >= sStreamStops)) {
        memset(out, 0x55, sizeof(*out)); // a source that returns 0 may have written anything
        return 0;
    }
    StreamPad(player, sFrame, out);
    return 1;
}

static const CoopInputSource sStream = { StreamRead, NULL };

// The local controller's rule: buttons go down and up in runs, with both sticks moving through the dead zone.
static void ControllerPad(int frame, OSContPad* out) {
    memset(out, 0, sizeof(*out));
    out->button = (uint16_t) (((frame % 7) < 3 ? BUTTON_A : 0) | ((frame % 11) < 5 ? BUTTON_R : 0));
    out->stick_x = (int8_t) ((frame % 170) - 85);
    out->stick_y = (int8_t) (85 - (frame % 170));
}

// Ports 1 to 3 are full of input nobody may follow.
static void JunkOnOtherPorts(int frame) {
    int port;

    for (port = 1; port < PORTS; port++) {
        memset(&sNext[port], 0, sizeof(sNext[port]));
        sNext[port].button = (uint16_t) (0x0800 | frame);
        sNext[port].stick_x = 77;
        sNext[port].stick_y = -77;
    }
}

// Every local index and player count: the local slot has port 0, with press edges from its own history, each other
// player its stream exactly, and a slot with no player nothing.
static void TestRouting(int local, int count) {
    OSContPad last;
    int player;

    Reset();
    memset(&last, 0, sizeof(last));
    for (player = 0; player < PORTS; player++) {
        CoopInput_SetSource(player, &sStream);
    }
    for (sFrame = 0; sFrame < FRAMES; sFrame++) {
        OSContPad pad;

        ControllerPad(sFrame, &pad);
        sNext[0] = pad;
        JunkOnOtherPorts(sFrame);
        UpdateInput();
        CoopInput_Route(count, local, sHold, sPress);
        for (player = 0; player < PORTS; player++) {
            CoopInputFrame want;

            if (player >= count) {
                OSContPad zero;

                memset(&zero, 0, sizeof(zero));
                Check(SamePad(&sHold[player], &zero) && SamePad(&sPress[player], &zero), "empty slot stays empty",
                      local, count, sFrame);
            } else if (player == local) {
                Check(SamePad(&sHold[player], &pad), "local slot holds port 0", local, count, sFrame);
                // The first routed frame has no history, so a button already down is not a press.
                Check(sPress[player].button == ((sFrame == 0) ? 0 : (pad.button & ~last.button)),
                      "local press edge from own history", local, count, sFrame);
                Check((sPress[player].stick_x == DeadZone(pad.stick_x)) &&
                          (sPress[player].stick_y == DeadZone(pad.stick_y)),
                      "local sticks after the dead zone", local, count, sFrame);
                Check(SamePad(&CoopInput_Controller()->hold, &pad) &&
                          SamePad(&CoopInput_Controller()->press, &sPress[player]),
                      "controller frame is what the local player used", local, count, sFrame);
            } else {
                StreamPad(player, sFrame, &want);
                Check(SamePad(&sHold[player], &want.hold) && SamePad(&sPress[player], &want.press),
                      "other player has exactly its stream", local, count, sFrame);
            }
        }
        last = pad;
    }
}

// The three cases of the table in NOTES.md (2026-11-06, #68), with slot 0 written by a stream at local index 1: R held
// for five frames while the stream on slot 0 is idle (the press used to repeat) or holds R (the press used to be lost).
static int32_t HoldsR(void* user, int32_t player, CoopInputFrame* out) {
    memset(out, 0, sizeof(*out));
    out->hold.button = (uint16_t) (*(const int*) user ? BUTTON_R : 0);
    (void) player;
    return 1;
}

static void TestPressNotRepeatedOrLost(int streamHoldsR) {
    int held = streamHoldsR;
    const CoopInputSource source = { HoldsR, &held };
    int presses = 0;
    int frame;

    Reset();
    CoopInput_SetSource(0, &source);
    CoopInput_SetSource(2, &source);
    for (frame = 0; frame < 8; frame++) {
        sNext[0].button = ((frame >= 2) && (frame < 7)) ? BUTTON_R : 0;
        UpdateInput();
        CoopInput_Route(2, 1, sHold, sPress);
        presses += (sPress[1].button & BUTTON_R) != 0;
        Check(sHold[1].button == sNext[0].button, "hold of the local player follows the pad", 1, 2, frame);
    }
    Check(presses == 1, streamHoldsR ? "press not lost under a stream that holds it" : "press does not repeat", 1, 2,
          frame);
}

// No source, a source that has nothing, and a source set for the local player.
static void TestSourcesAndIdle(void) {
    OSContPad zero;
    OSContPad pad;

    memset(&zero, 0, sizeof(zero));
    memset(&pad, 0, sizeof(pad));
    pad.button = BUTTON_A;
    pad.stick_x = 50;

    Reset();
    sNext[0] = pad;
    JunkOnOtherPorts(0);
    UpdateInput();
    CoopInput_Route(4, 2, sHold, sPress);
    Check(SamePad(&sHold[2], &pad), "with no source the local player has port 0", 2, 4, 0);
    Check(SamePad(&sHold[0], &zero) && SamePad(&sHold[1], &zero) && SamePad(&sHold[3], &zero),
          "with no source the others idle", 2, 4, 0);

    Reset();
    CoopInput_SetSource(1, &sStream);
    sStreamStops = 0;
    sFrame = 3;
    UpdateInput();
    CoopInput_Route(2, 0, sHold, sPress);
    Check(SamePad(&sHold[1], &zero) && SamePad(&sPress[1], &zero), "a source with nothing leaves the player idle", 0, 2,
          3);
    sStreamStops = -1;

    Reset();
    CoopInput_SetSource(1, &sStream);
    sNext[0] = pad;
    UpdateInput();
    sStreamReads = 0;
    CoopInput_Route(2, 1, sHold, sPress);
    Check(SamePad(&sHold[1], &pad), "a stream set for the local player does not steer it", 1, 2, 0);
    Check(sStreamReads == 0, "a stream set for the local player is not read", 1, 2, 0);

    Reset();
    CoopInput_SetSource(0, &sStream);
    CoopInput_SetSource(1, &sStream);
    CoopInput_SetLocalSource(&sStream);
    sNext[0] = pad;
    sFrame = 9;
    UpdateInput();
    CoopInput_Route(2, 1, sHold, sPress);
    {
        CoopInputFrame want;

        StreamPad(1, 9, &want);
        Check(SamePad(&sHold[1], &want.hold), "a local source takes the place of the controller", 1, 2, 9);
    }
    Check(SamePad(&CoopInput_Controller()->hold, &pad), "the controller is still read under a local source", 1, 2, 9);

    CoopInput_ClearSources();
    UpdateInput();
    CoopInput_Route(2, 1, sHold, sPress);
    Check(SamePad(&sHold[1], &pad) && SamePad(&sHold[0], &zero), "cleared sources: controller and idle again", 1, 2, 9);

    // A local index outside the players: nobody is local, and port 0 reaches no one.
    Reset();
    sNext[0] = pad;
    UpdateInput();
    CoopInput_Route(1, 2, sHold, sPress);
    Check(SamePad(&sHold[0], &zero) && SamePad(&sHold[2], &zero), "no local player: port 0 reaches no slot", 2, 1, 0);
}

static void TestRest(void) {
    Reset();
    sNext[0].button = BUTTON_A;
    UpdateInput();
    CoopInput_Route(2, 0, sHold, sPress);
    Check(sPress[0].button == 0, "a button down when routing starts is not a press", 0, 2, 0);
    CoopInput_Rest();
    UpdateInput();
    CoopInput_Route(2, 0, sHold, sPress);
    Check(sPress[0].button == 0, "nor when it starts again", 0, 2, 1);
    sNext[0].button = 0;
    UpdateInput();
    CoopInput_Route(2, 0, sHold, sPress);
    sNext[0].button = BUTTON_A;
    UpdateInput();
    CoopInput_Route(2, 0, sHold, sPress);
    Check(sPress[0].button == BUTTON_A, "a real press after that counts", 0, 2, 3);
}

// Entry 0 is the local pad, and no two players share an entry.
static void TestRumble(void) {
    int local;
    int player;

    for (local = 0; local < PORTS; local++) {
        int seen = 0;

        for (player = 0; player < PORTS; player++) {
            int slot = CoopInput_RumbleSlot(player, local);

            Check((slot >= 0) && (slot < PORTS), "rumble entry in range", local, PORTS, player);
            Check((slot == 0) == (player == local), "entry 0 is the local player's", local, PORTS, player);
            Check((local != 0) || (slot == player), "local index 0 keeps every entry", local, PORTS, player);
            seen |= 1 << slot;
        }
        Check(seen == 0xF, "every player has its own entry", local, PORTS, 0);
    }
}

int main(void) {
    int local;
    int count;

    for (count = 1; count <= PORTS; count++) {
        for (local = 0; local < count; local++) {
            TestRouting(local, count);
        }
    }
    TestPressNotRepeatedOrLost(0);
    TestPressNotRepeatedOrLost(1);
    TestSourcesAndIdle();
    TestRest();
    TestRumble();
    printf("%d of %d checks passed\n", sChecks - sFailed, sChecks);
    return (sFailed == 0) ? 0 : 1;
}
