#pragma once

// Message ids and the protocol version. The registry and the wire layout are in docs/coop/PROTOCOL.md.

// Bump whenever a message layout changes (PROTOCOL.md rule 2).
#define COOP_PROTOCOL_VERSION 12

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
    // The session's save (decision 0009 item 8): the host's, once, just ahead of the level start.
    COOP_MSG_SESSION_SAVE = 0x0017,
    // Players' names (#258): a client's own after its welcome, the host's list of all of them. Display only.
    COOP_MSG_SESSION_NAME = 0x0018,
    // The session survives (#189's sibling): a player whose connection was lost comes back with the session id of its
    // welcome, in place of a hello; and the host says that the level is going to start again, ahead of the new save
    // and level start. 0x0018 is the name at join (names and letters).
    COOP_MSG_SESSION_REJOIN = 0x0019,
    COOP_MSG_SESSION_RECOVER = 0x001A,
    COOP_MSG_SESSION_END = 0x0040,

    // Lockstep (WP-22): frame-stamped inputs between all clients.
    COOP_MSG_SYNC_INPUT = 0x0040,
    // Desync check (WP-23): state hash totals per frame, and the hash of the first frame that differed.
    COOP_MSG_SYNC_HASH = 0x0041,
    COOP_MSG_SYNC_DESYNC = 0x0042,
    // #189: a game's dump of the desync frame, in numbered parts, so the other game of the pair can name the first
    // field that differs. See src/port/coop/sync/desync_dump.h.
    COOP_MSG_SYNC_DUMP = 0x0043,
    // Decision 0016: the repair of a desync in place. SYNC_REPAIR (host to clients: the frame every game holds at),
    // SYNC_STATE (host to a client: the state at that frame, in numbered parts, the stream of SYNC_DUMP), SYNC_REPAIRED
    // (client to host: it wrote the state, and its hash of the result). See src/port/coop/sync/repair_game.h.
    COOP_MSG_SYNC_REPAIR = 0x0044,
    COOP_MSG_SYNC_STATE = 0x0045,
    COOP_MSG_SYNC_REPAIRED = 0x0046,
    // Decision 0016 part A: a player whose connection was lost comes back into the level where it was. SYNC_RESUME (host
    // to the returned client: the run and frame the host stands at), SYNC_RESUMED (client to host: it can go on, or not).
    // See src/port/coop/sync/lockstep_game.h.
    COOP_MSG_SYNC_RESUME = 0x0047,
    COOP_MSG_SYNC_RESUMED = 0x0048,

    // The handover (#189): before a session closes, every game hands its desync report and the rest of its co-op log
    // to every player still connected. A kind byte first, see src/port/coop/sync/handover.h.
    COOP_MSG_HANDOVER = 0x0080,

    // Debug and test harness.
    COOP_MSG_TEST_RELIABLE = 0x00C0,
    COOP_MSG_TEST_UNRELIABLE = 0x00C1,
    COOP_MSG_TEST_DONE = 0x00C2,
    COOP_MSG_TEST_SESSION = 0x00C3,
    // The co-op log (#172): a batch of log lines of one player, streamed so every player holds every log.
    COOP_MSG_DIAG_LOG = 0x00C4,
} CoopMessageId;
