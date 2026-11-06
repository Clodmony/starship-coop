#pragma once

// The co-op transport over Steam's relay network (decision 0004, WP-26): no IP addresses, no port forwarding. The same
// CoopTransport as UDP, on Steam's own reliability. Peers are SteamIDs; the wire form is in docs/coop/PROTOCOL.md.
//
// CoopSteamBackend is the seam: everything the transport needs from Steam, and nothing else. So the transport runs on
// the fake of tools/coop/steamtest as well as on the real library, and building it needs no Steamworks SDK.

#include <stddef.h>

#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t CoopSteamId; // a SteamID64

typedef enum {
    COOP_STEAM_SENT,
    COOP_STEAM_BUSY,   // the send queue is full, or the message was not taken for another reason: try again later
    COOP_STEAM_BROKEN, // the session failed or the peer closed it. It stays broken until closeSession.
} CoopSteamSendResult;

typedef enum {
    COOP_STEAM_SESSION_REQUEST, // someone we have no session with sent a message; it waits until we accept
    COOP_STEAM_SESSION_FAILED,  // the session broke. Steam says nothing when a peer closes on purpose.
} CoopSteamEventType;

typedef struct {
    CoopSteamEventType type;
    CoopSteamId peer;
} CoopSteamEvent;

typedef struct {
    CoopSteamId from;
    const uint8_t* data; // valid until release
    uint32_t size;
    void* handle; // the backend's
} CoopSteamMessage;

typedef struct CoopSteamBackend CoopSteamBackend;

typedef struct {
    CoopSteamId (*self)(CoopSteamBackend* b);
    uint32_t (*nowMs)(CoopSteamBackend* b);
    // A number that must not repeat between two connections from this user.
    uint32_t (*nonce)(CoopSteamBackend* b);
    // Opens the session if there is none, and accepts one the peer asked for.
    CoopSteamSendResult (*send)(CoopSteamBackend* b, CoopSteamId to, const void* data, uint32_t size, CoopNetLane lane);
    // The next message from anyone we have a session with. True if out was filled in; then release it.
    bool (*receive)(CoopSteamBackend* b, CoopSteamMessage* out);
    void (*release)(CoopSteamBackend* b, CoopSteamMessage* message);
    // True if out was filled in. Call until it returns false.
    bool (*event)(CoopSteamBackend* b, CoopSteamEvent* out);
    void (*accept)(CoopSteamBackend* b, CoopSteamId peer);
    // Frees a session at once, and what it has not sent yet. Required to get past a broken session.
    void (*closeSession)(CoopSteamBackend* b, CoopSteamId peer);
    void (*destroy)(CoopSteamBackend* b);
} CoopSteamBackendOps;

struct CoopSteamBackend {
    const CoopSteamBackendOps* ops;
};

// The transport takes over the backend and destroys it in close, or here on failure (NULL). Connect returns at once;
// poll gives the outcome. It fails on an empty SteamID and on our own: one user cannot talk to itself. Finding the
// host's SteamID is up to the caller: there are no lobbies yet.
// A connection attempt lasts config->timeoutMs, or 20000 ms if that is 0: Steam needs time to find a route.
CoopTransport* CoopNetSteam_Host(CoopSteamBackend* backend, const CoopNetConfig* config);
CoopTransport* CoopNetSteam_Connect(CoopSteamBackend* backend, CoopSteamId host, const CoopNetConfig* config);

// The real backend: loads the Steam library at run time and starts Steam as app 480 (Spacewar). library is a path, or
// NULL for the usual name, first next to the program and then on the system's search path. NULL on failure, with the
// reason in error. Steam must be running and logged in. 64-bit only.
CoopSteamBackend* CoopSteamFlat_Open(const char* library, char* error, size_t errorSize);

#ifdef __cplusplus
}
#endif
