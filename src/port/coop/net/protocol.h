#pragma once

// Message ids and the protocol version. The registry and the wire layout are in docs/coop/PROTOCOL.md.

// Bump whenever a message layout changes (PROTOCOL.md rule 2).
#define COOP_PROTOCOL_VERSION 1

typedef enum {
    // Transport. These never reach the layers above it.
    COOP_MSG_CONNECT_REQUEST = 0x0000,
    COOP_MSG_CONNECT_ACCEPT = 0x0001,
    COOP_MSG_CONNECT_REFUSE = 0x0002,
    COOP_MSG_DISCONNECT = 0x0003,
    COOP_MSG_ACK = 0x0004,
    COOP_MSG_PING = 0x0005,
    COOP_MSG_PONG = 0x0006,
    COOP_MSG_TRANSPORT_END = 0x0010,

    // Debug and test harness.
    COOP_MSG_TEST_RELIABLE = 0x00C0,
    COOP_MSG_TEST_UNRELIABLE = 0x00C1,
    COOP_MSG_TEST_DONE = 0x00C2,
} CoopMessageId;
