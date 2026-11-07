#pragma once

// Message ids and the protocol version. The registry and the wire layout are in docs/coop/PROTOCOL.md.

// Bump whenever a message layout changes (PROTOCOL.md rule 2).
#define COOP_PROTOCOL_VERSION 5

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

    // Session handshake (WP-21). Nothing else reaches a peer before SESSION_WELCOME.
    COOP_MSG_SESSION_HELLO = 0x0010,
    COOP_MSG_SESSION_WELCOME = 0x0011,
    COOP_MSG_SESSION_REFUSE = 0x0012,

    // Characters (WP-34): who flies what, after the welcome.
    COOP_MSG_SESSION_PICK = 0x0013,
    COOP_MSG_SESSION_PICK_REFUSE = 0x0014,
    COOP_MSG_SESSION_ROSTER = 0x0015,

    // Level start (WP-24): everyone into the same level from the same state.
    COOP_MSG_SESSION_LEVEL_START = 0x0016,
    COOP_MSG_SESSION_END = 0x0040,

    // Lockstep (WP-22): frame-stamped inputs between all clients.
    COOP_MSG_SYNC_INPUT = 0x0040,

    // Debug and test harness.
    COOP_MSG_TEST_RELIABLE = 0x00C0,
    COOP_MSG_TEST_UNRELIABLE = 0x00C1,
    COOP_MSG_TEST_DONE = 0x00C2,
    COOP_MSG_TEST_SESSION = 0x00C3,
} CoopMessageId;
