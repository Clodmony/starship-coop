#pragma once

// A co-op session on top of a transport: host or join, then the handshake of PROTOCOL.md rule 2. A client sends
// SESSION_HELLO with its identity; the host compares it with its own and answers SESSION_WELCOME or SESSION_REFUSE.
// Both sides get the same readable text for every difference. Only after the welcome do other messages pass.
// Nothing here blocks or reads a clock: call Poll once per frame with the time in ms, until it returns false.

#include <stdbool.h>
#include <stdint.h>

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

typedef enum {
    COOP_SESSION_EV_JOINED,  // host: client `peer` passed the handshake. Client: the host let us in.
    COOP_SESSION_EV_REFUSED, // host: client `peer` was refused. Client: the host refused us; ENDED follows.
    COOP_SESSION_EV_LEFT,    // host: a client that had joined is gone
    COOP_SESSION_EV_ENDED,   // client: the session is over, see end. Nothing follows.
    COOP_SESSION_EV_MESSAGE, // a message above the session range from a peer that has joined
} CoopSessionEventType;

typedef enum {
    COOP_SESSION_END_NONE,
    COOP_SESSION_END_REFUSED,      // the host refused our identity
    COOP_SESSION_END_CLOSED,       // the other side closed
    COOP_SESSION_END_TIMEOUT,      // the connection went quiet
    COOP_SESSION_END_HOST_FULL,    // no free slot on the host
    COOP_SESSION_END_PROTOCOL,     // the other side runs another COOP_PROTOCOL_VERSION
    COOP_SESSION_END_NO_HANDSHAKE, // connected, but the handshake did not finish in time
    COOP_SESSION_END_BAD_MESSAGE,  // the other side sent a handshake message we could not read
} CoopSessionEnd;

// Reason bit in a refusal next to the CoopIdentityDiff bits: the hello could not be read.
#define COOP_SESSION_REFUSE_MALFORMED (1u << 8)

typedef struct {
    CoopSessionEventType type;
    int peer;            // transport peer index; a client has the host at 0
    uint8_t playerIndex; // JOINED: the player slot, 0 for the host and 1 to 3 for clients in join order of slots
    uint32_t diff;       // JOINED: warnings (COOP_ID_DIFF_PLATFORM). REFUSED: the reasons.
    CoopSessionEnd end;  // REFUSED on a client, ENDED, LEFT
    const char* text;    // for the player, "" if there is nothing to say; valid until the next Poll
    uint16_t msgId;      // MESSAGE
    uint16_t size;       // MESSAGE
    const uint8_t* data; // MESSAGE, valid until the next Poll
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
// Messages dropped because their sender had not joined, or because they were out of place in the handshake.
uint32_t CoopSession_Ignored(const CoopSession* s);
void CoopSession_Close(CoopSession* s);

const char* CoopSession_EndText(CoopSessionEnd end);

#ifdef __cplusplus
}
#endif
