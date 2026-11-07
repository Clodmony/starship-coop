#pragma once

// Deterministic lockstep (decision 0002): every client runs the whole game and only inputs travel. A player's input for
// frame F is made on that player's frame F - delay and goes to everyone. Frame F runs on a client once it holds every
// player's input for F, so all clients run the same frames with the same inputs, however far apart in time.
//
// No game code in here and no clock: tools/coop/locksteptest runs it in a virtual network, and lockstep_game.c connects
// it to the game. The wire form is SYNC_INPUT in docs/coop/PROTOCOL.md. It goes on the unreliable lane with what the
// other end has not confirmed yet, so a lost datagram costs one send and not a resend timer.

#include <stdbool.h>
#include <stdint.h>

#include "port/coop/game/input.h"
#include "port/coop/net/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

// Frames from a local input to the frame it steers. At the game's 30 frames a second this is 100 ms, which is also how
// far a message can be late before anybody waits. Nothing but the sender uses it: its first frames are empty, and every
// frame after that carries a stamp.
#define COOP_LOCKSTEP_INPUT_DELAY 3
#define COOP_LOCKSTEP_MAX_DELAY 16
// Frames kept per player. Clients stay within the delay of each other, and resends only reach back about that far.
#define COOP_LOCKSTEP_RING 64
// Frames of one player in one message, so that a message fits one datagram with every player in it.
#define COOP_LOCKSTEP_BLOCK_FRAMES 32
// What a frame of one player takes on the wire.
#define COOP_LOCKSTEP_FRAME_SIZE 8
// With nothing new, a message still goes out this often: it repeats what was not confirmed, and it confirms.
#define COOP_LOCKSTEP_RESEND_MS 30
// A player that sends no input for this long, while its connection stays up, ends the session.
#define COOP_LOCKSTEP_STALL_MS 10000
// The same before the first frame: a slow PC may still be loading the level.
#define COOP_LOCKSTEP_START_MS 60000

typedef struct {
    // One SYNC_INPUT to a peer. Lost or refused is the same to this code: the next send repeats it.
    bool (*send)(void* user, int peer, const void* data, uint16_t size);
    void* user;
    int32_t self;     // our player index
    bool host;        // player n is then the client at transport peer n - 1; a client has the host at peer 0
    uint32_t present; // bit i: player i is in the level
    uint32_t delay;   // COOP_LOCKSTEP_INPUT_DELAY, unless a test asks for another one
} CoopLockstepConfig;

typedef struct {
    bool open;
    uint32_t next[COOP_PLAYERS_MAX]; // the first frame of each player that this end has not confirmed
    uint32_t sentMs;
    bool dirty; // there is something new for this end
} CoopLockstepLink;

typedef struct {
    CoopLockstepConfig config;
    uint32_t frame;                  // the frame that runs next
    uint32_t next[COOP_PLAYERS_MAX]; // the first frame of each player that we do not hold
    uint8_t ring[COOP_PLAYERS_MAX][COOP_LOCKSTEP_RING][COOP_LOCKSTEP_FRAME_SIZE];
    CoopLockstepLink links[COOP_NET_MAX_PEERS];
    uint32_t progressMs; // when `frame` last moved
} CoopLockstep;

// Starts at frame 0. Our own first `delay` frames are empty and need no input.
void CoopLockstep_Start(CoopLockstep* l, const CoopLockstepConfig* config, uint32_t nowMs);

// The lowest player whose input for the current frame is missing, -1 when the frame can run.
int32_t CoopLockstep_Waiting(const CoopLockstep* l);

// What `player` does on the given frame, which has to be one we hold. A player not in the level is empty.
void CoopLockstep_Read(const CoopLockstep* l, uint32_t frame, int32_t player, CoopInputFrame* out);

// The current frame has run, with `local` read from the local controller on it: that becomes our input for the frame
// `delay` ahead. Goes out at once.
void CoopLockstep_Advance(CoopLockstep* l, const CoopInputFrame* local, uint32_t nowMs);

// A SYNC_INPUT from `peer`. False if it could not be read or is not this peer's to send: the session ends on that.
bool CoopLockstep_Receive(CoopLockstep* l, int peer, const uint8_t* data, uint16_t size, uint32_t nowMs);

// Sends what is due. Call it while waiting too, or nothing is repeated.
void CoopLockstep_Tick(CoopLockstep* l, uint32_t nowMs);

// The player we have waited too long for, -1 if we have not.
int32_t CoopLockstep_StalledOn(const CoopLockstep* l, uint32_t nowMs);

#ifdef __cplusplus
}
#endif
