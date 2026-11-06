#pragma once

// The co-op transport (decision 0004): whole messages between a host and its clients, on one of two lanes. Every
// transport implements CoopTransportOps, so the layers above do not know which one they use. Nothing here blocks:
// call poll once per frame until it returns false.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Clients on one host, so four players in all.
#define COOP_NET_MAX_PEERS 3
// One message fits one datagram; there is no fragmentation.
#define COOP_NET_MAX_DATAGRAM 1200
#define COOP_NET_HEADER_SIZE 24
#define COOP_NET_MAX_MESSAGE (COOP_NET_MAX_DATAGRAM - COOP_NET_HEADER_SIZE)

typedef enum {
    COOP_NET_UNRELIABLE, // may be lost or arrive out of order, never delivered twice
    COOP_NET_RELIABLE,   // ordered, delivered exactly once
} CoopNetLane;

typedef enum {
    COOP_NET_EVENT_NONE,
    COOP_NET_EVENT_CONNECTED,
    COOP_NET_EVENT_DISCONNECTED,
    COOP_NET_EVENT_MESSAGE,
} CoopNetEventType;

typedef enum {
    COOP_NET_END_NONE,
    COOP_NET_END_CLOSED,          // the peer closed the connection
    COOP_NET_END_TIMEOUT,         // nothing heard for timeoutMs
    COOP_NET_END_REFUSED_FULL,    // the host has no free slot
    COOP_NET_END_REFUSED_VERSION, // the host runs another COOP_PROTOCOL_VERSION
} CoopNetEnd;

typedef struct {
    CoopNetEventType type;
    int peer;            // a client has one peer, the host, at index 0
    CoopNetEnd end;      // COOP_NET_EVENT_DISCONNECTED
    uint16_t msgId;      // COOP_NET_EVENT_MESSAGE
    uint16_t size;       // COOP_NET_EVENT_MESSAGE
    const uint8_t* data; // COOP_NET_EVENT_MESSAGE, valid until the next poll
} CoopNetEvent;

typedef struct {
    uint32_t timeoutMs; // 0: 5000. Must be longer than the longest stall of the game loop.
    uint32_t pingMs;    // 0: 250
    uint8_t maxPeers;   // host only. 0: COOP_NET_MAX_PEERS
} CoopNetConfig;

typedef struct {
    uint32_t sent;           // datagrams sent to this peer, resends included
    uint32_t received;       // valid datagrams from this peer
    uint32_t resent;         // reliable messages sent again
    uint32_t pending;        // reliable messages not yet acknowledged
    uint32_t duplicates;     // messages dropped because they had already arrived
    uint32_t overflow;       // messages dropped because the receiver was not polling fast enough
    uint32_t rttMs;          // smoothed round trip time, 0 until the first pong
    uint32_t rttMinMs;       // lowest round trip seen
    uint32_t rttMaxMs;       // highest round trip seen
    uint32_t badDatagrams;   // whole transport: failed the checksum or the format check
    uint32_t strayDatagrams; // whole transport: valid, but from nobody we are connected to
} CoopNetStats;

typedef struct CoopTransport CoopTransport;

typedef struct {
    // False if the peer is not connected, the message is too large or the reliable window is full (try again later).
    bool (*send)(CoopTransport* t, int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane);
    // True if ev was filled in. Peers end with one COOP_NET_EVENT_DISCONNECTED, after their last message.
    bool (*poll)(CoopTransport* t, CoopNetEvent* ev);
    // Says goodbye and drops the peer at once: unacknowledged reliable messages are lost. No event follows.
    void (*disconnect)(CoopTransport* t, int peer);
    bool (*stats)(CoopTransport* t, int peer, CoopNetStats* out);
    // Disconnects every peer and frees t.
    void (*close)(CoopTransport* t);
} CoopTransportOps;

struct CoopTransport {
    const CoopTransportOps* ops;
};

// SDL2_net over UDP, IPv4 only. Port 0 picks a free port. Both return NULL on failure, see SDLNet_GetError.
// Connect returns at once; a COOP_NET_EVENT_CONNECTED or COOP_NET_EVENT_DISCONNECTED from poll gives the outcome.
// The host name is resolved inside Connect, which can block on a DNS lookup.
CoopTransport* CoopNetSdl_Host(uint16_t port, const CoopNetConfig* config);
CoopTransport* CoopNetSdl_Connect(const char* host, uint16_t port, const CoopNetConfig* config);
uint16_t CoopNetSdl_Port(CoopTransport* t);

#ifdef __cplusplus
}
#endif
