#pragma once

// A co-op session on top of a transport: host or join, then the handshake of PROTOCOL.md rule 2. A client sends
// SESSION_HELLO with its identity; the host compares it with its own and answers SESSION_WELCOME or SESSION_REFUSE.
// Both sides get the same readable text for every difference. Only after the welcome do other messages pass.
// After the welcome a client picks a character and the host keeps the roster of who flies what (decision 0005): see
// CoopSession_Pick and CoopSession_Roster. The host then starts a level for everyone: see CoopSession_StartLevel.
// Nothing here blocks or reads a clock: call Poll once per frame with the time in ms, until it returns false.

#include <stdbool.h>
#include <stdint.h>

#include "port/coop/game/level_start.h"
#include "port/coop/net/transport.h"
#include "identity.h"

#ifdef __cplusplus
extern "C" {
#endif

// The host waits this long for a connected client's hello, the client this long for the host's answer.
#define COOP_SESSION_HANDSHAKE_MS 10000
// After a refusal the host keeps the client this long at most, so the refusal can arrive.
#define COOP_SESSION_REFUSE_LINGER_MS 3000
// Size of the text in an event.
#define COOP_SESSION_TEXT 1024
// The host and its clients. Player 0 is the host, player n is the client at transport peer n - 1.
#define COOP_SESSION_PLAYERS (COOP_NET_MAX_PEERS + 1)

// What a player slot holds. These are the wire values, see PROTOCOL.md. FOX to PEPPY run in the order of TeamId
// (include/sf64player.h), so that WP-17 can get from one to the other, but this header does not depend on it.
typedef enum {
    COOP_CHARACTER_NO_PLAYER, // nobody is in this slot
    COOP_CHARACTER_UNPICKED,  // a client that joined and holds no character yet
    COOP_CHARACTER_FOX,       // player 0, always
    COOP_CHARACTER_FALCO,
    COOP_CHARACTER_SLIPPY,
    COOP_CHARACTER_PEPPY,
    COOP_CHARACTER_COUNT,
} CoopCharacter;

// Who flies what, by player index; each entry a CoopCharacter. No two players hold the same character.
typedef struct {
    uint8_t character[COOP_SESSION_PLAYERS];
} CoopRoster;

typedef enum {
    COOP_SESSION_EV_JOINED,  // host: client `peer` passed the handshake. Client: the host let us in.
    COOP_SESSION_EV_REFUSED, // host: client `peer` was refused. Client: the host refused us; ENDED follows.
    COOP_SESSION_EV_LEFT,    // host: a client that had joined is gone
    COOP_SESSION_EV_ENDED,   // client: the session is over, see end. Nothing follows.
    COOP_SESSION_EV_MESSAGE, // a message above the session range from a peer that has joined
    COOP_SESSION_EV_ROSTER,  // the roster changed, see CoopSession_Roster
    // Host: client `peer` asked for a taken character. Client: we did. Nothing changes.
    COOP_SESSION_EV_PICK_REFUSED,
    // Host and client alike: enter the level in levelStart. It comes once per session.
    COOP_SESSION_EV_LEVEL_START,
} CoopSessionEventType;

typedef enum {
    COOP_SESSION_END_NONE,
    COOP_SESSION_END_REFUSED,      // the host refused our identity
    COOP_SESSION_END_CLOSED,       // the other side closed
    COOP_SESSION_END_TIMEOUT,      // the connection went quiet
    COOP_SESSION_END_HOST_FULL,    // no free slot on the host
    COOP_SESSION_END_PROTOCOL,     // the other side runs another COOP_PROTOCOL_VERSION
    COOP_SESSION_END_NO_HANDSHAKE, // connected, but the handshake did not finish in time
    COOP_SESSION_END_BAD_MESSAGE,  // the other side sent a session message we could not read
} CoopSessionEnd;

// Reason bit in a refusal next to the CoopIdentityDiff bits: the hello could not be read.
#define COOP_SESSION_REFUSE_MALFORMED (1u << 8)
// The same: a level has started, and nobody joins after that.
#define COOP_SESSION_REFUSE_STARTED (1u << 9)

typedef struct {
    CoopSessionEventType type;
    int peer;            // transport peer index; a client has the host at 0
    uint8_t playerIndex; // JOINED: the player slot, 0 for the host and 1 to 3 for clients in join order of slots
    uint8_t character;   // PICK_REFUSED: the CoopCharacter that was asked for
    uint32_t diff;       // JOINED: what differs but only warns (COOP_ID_DIFF_PLATFORM). REFUSED: the reasons.
    CoopSessionEnd end;  // REFUSED on a client, ENDED, LEFT
    const char* text;    // for the player, "" if there is nothing to say; valid until the next Poll
    uint16_t msgId;      // MESSAGE
    uint16_t size;       // MESSAGE
    const uint8_t* data; // MESSAGE, valid until the next Poll
    const CoopLevelStart* levelStart; // LEVEL_START, valid until the session is closed
} CoopSessionEvent;

typedef struct CoopSession CoopSession;

// The session takes over the transport and closes it in CoopSession_Close. identity is copied.
CoopSession* CoopSession_Host(CoopTransport* transport, const CoopIdentity* identity);
CoopSession* CoopSession_Join(CoopTransport* transport, const CoopIdentity* identity, uint32_t nowMs);
bool CoopSession_Poll(CoopSession* s, uint32_t nowMs, CoopSessionEvent* ev);
// Only to a peer that has joined, and only ids from COOP_MSG_SESSION_END up.
bool CoopSession_Send(CoopSession* s, int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane);
// The identity a joined peer sent (host) or the host's (client); NULL before the handshake.
const CoopIdentity* CoopSession_PeerIdentity(const CoopSession* s, int peer);
// Client: ask the host for Falco, Slippy or Peppy, in place of what we hold. The answer is a ROSTER event with the
// character in our slot, or PICK_REFUSED, which leaves us connected and free to pick again. Picking what we already
// hold has no answer. False if it could not be sent, on a host, before the welcome, for any other character, or once a
// level has started. A pick still on its way when the host starts the level is dropped: LEVEL_START says what we hold.
bool CoopSession_Pick(CoopSession* s, CoopCharacter character);
// Host: start a level for everyone, from start's level, phase and values. The roster goes along as it is now. Polls
// then give a LEVEL_START event on the host and on every client, and from here on nobody picks and nobody joins;
// nothing opens that again yet. False, and nothing changes, while a client that joined holds no character or a client
// is in the handshake, for a level that cannot be started, on a client, and once a level has started.
bool CoopSession_StartLevel(CoopSession* s, const CoopLevelStart* start);
// True if CoopSession_StartLevel would take a level that can be started, right now: what a start button shows.
bool CoopSession_CanStart(const CoopSession* s);
// The current roster, valid until the next Poll. NULL on a client before the first ROSTER event and after the end.
const CoopRoster* CoopSession_Roster(const CoopSession* s);
// The transport's counters for a peer that has joined, rttMs among them. False for anyone else.
bool CoopSession_Stats(const CoopSession* s, int peer, CoopNetStats* out);
// Messages dropped because their sender had not joined, or because they were out of place in the handshake.
uint32_t CoopSession_Ignored(const CoopSession* s);
// Our own player index: 0 on the host, the welcome's on a client, -1 on a client before it.
int CoopSession_Self(const CoopSession* s);
// The player index of a peer that has joined (host: peer + 1, client: 0, the host); -1 for anyone else.
int CoopSession_PeerPlayer(const CoopSession* s, int peer);
// True once a level has started: from then on every player is needed, and one who goes ends the session for all.
bool CoopSession_LevelStarted(const CoopSession* s);
// The session's last line in the co-op log, `session closed`, written now so that it can still reach the other
// players before the connection goes (the handover, #189). Once per session: Close writes it if nobody did.
void CoopSession_SayClosed(CoopSession* s);
void CoopSession_Close(CoopSession* s);

const char* CoopSession_EndText(CoopSessionEnd end);
// "Falco"; "" for a value that is no character.
const char* CoopSession_CharacterName(uint8_t character);

#ifdef __cplusplus
}
#endif
