# Wire protocol

Status: the transport (WP-20), the session handshake (WP-21), the characters (WP-34) and the level start (WP-24) exist;
`COOP_PROTOCOL_VERSION` is 4. Sync and game messages come with their work packages. This
page sets the rules so parallel work does not collide.

## Rules

1. **Every message has an id from the registry below**, reserved in the same PR that adds the message. Ranges per
   area keep parallel branches from picking the same number; take the next free id in your area's range.
2. **Versioned handshake.** The first exchange carries a protocol version, the build id (git commit), the ROM version
   (the game version of each loaded archive), the values of the simulation-affecting CVars and the platform (OS,
   CPU, compiler). A different protocol version, build, ROM or CVar refuses the connection with a message the player
   can read. A different platform only warns: the first two-PC tests run Windows against Linux, and whether the two
   compute the same game is unknown (decision 0002). Bump `COOP_PROTOCOL_VERSION` whenever a message layout changes.
3. **Explicit layout.** Little-endian, fixed-size integer types, no raw `memcpy` of game structs (they hold pointers
   and padding). Write a serializer per message.
4. **Frame-stamped.** Messages that belong to a game frame carry that frame number.
5. **No game data on the wire** beyond what the protocol needs (no asset or ROM bytes).
6. Document each message here: id, name, direction, fields, when it is sent.

## Transport over UDP

Decided in [0004](decisions/0004-transport.md); the code is in `src/port/coop/net/`. A datagram holds one message and
at most 1200 bytes, so a payload holds at most 1176. Larger messages are refused, there is no fragmentation.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | CRC-32 of everything after this field |
| 4 | 2 | Magic `0x4353` |
| 6 | 1 | `COOP_PROTOCOL_VERSION` |
| 7 | 1 | Flags. Bit 0: reliable lane |
| 8 | 2 | Message id |
| 10 | 2 | Payload size |
| 12 | 4 | Connection id, the nonce in the client's connect request |
| 16 | 2 | Sequence number of this message on its lane |
| 18 | 2 | Ack: the first reliable sequence number still missing here |
| 20 | 4 | Ack bits: bit n set means sequence number ack + 1 + n has arrived |
| 24 | size | Payload |

The first 16 bytes keep their meaning in every version, and so does the refuse message, so that two versions can tell
each other apart. A datagram with a bad checksum, a bad magic or a size that does not match is dropped unread.

- **Reliable lane:** ordered, delivered exactly once. Up to 128 messages in flight per direction; after that `send`
  returns false. A message is resent until acknowledged, first after the measured round trip plus margin (50 to
  1000 ms), then at twice that delay each time, at most every 1000 ms.
- **Unreliable lane:** may be lost or arrive out of order, never delivered twice. Sequence numbers count separately,
  and only serve to drop duplicates. Something 64 or more behind the newest number counts as a duplicate.
- **Connecting:** the client repeats `CONNECT_REQUEST` every 200 ms until the host accepts or refuses, or the timeout
  passes. The host keeps peers apart by address and connection id. A client ignores the sender's address and goes by
  the connection id, because a host with several addresses may answer from another one.
- **Staying connected:** both sides ping every 250 ms, which also measures the round trip. A connection that hears
  nothing valid for 5000 ms ends with a timeout. A goodbye is sent three times and not acknowledged.
- **Not provided:** encryption, peer authentication, IPv6.

## Session handshake

The code is in `src/port/coop/session/`. All three messages go on the reliable lane.

1. When the transport reports the connection, the client sends `SESSION_HELLO` with its identity.
2. The host compares it with its own. On a match it answers `SESSION_WELCOME`, otherwise `SESSION_REFUSE`. Both
   carry the host's identity, so the client builds the same text the host shows: what differs, host value against
   joining player value.
3. A refused client closes. The host drops it when the refusal is acknowledged, when the client goes, or after 3000 ms.
4. Nothing but the hello passes before the welcome. Other messages from a client that has not joined are dropped and
   counted. The host refuses a client that sends no hello within 10000 ms; the client gives up when the host does not
   answer within 10000 ms.

**Identity**, in this order:

| Size | Field |
|---|---|
| 1 + n | Build id: length n (below 48), then n bytes of printable ASCII. The git commit, `-dirty` with uncommitted changes. |
| 1 + n | Platform, same form, for example `Windows x86-64 MSVC 1944` |
| 1 + 4 r | Count r (at most 8), then the game version of each archive that has one, sorted. It is the ROM's header CRC as Torch writes it into `sf64.o2r`. |
| 4 | FNV-1a hash of the names in the simulation CVar table, in order |
| 2 + 4 c | Count c (at most 160), then each CVar's value, in table order. Floats as their bit pattern. |

The CVar table is `src/port/coop/session/sim_cvars.c`; why each CVar is there is in NOTES.md (WP-21).

## Characters

Who flies what, from [decision 0005](decisions/0005-players-and-characters.md). The code is in
`src/port/coop/session/session.c`. All three messages go on the reliable lane, after the welcome. The host holds the
roster and is the only one that changes it; a client's copy is what the host last sent.

**Roster**, four bytes, one per player: player 0 is the host, player n the client at transport peer n - 1. So the slot
is the player index in `SESSION_WELCOME`.

| Value | Slot |
|---|---|
| 0 | Free |
| 1 | A client that joined and holds no character yet |
| 2 | Fox: player 0, and only player 0 |
| 3 | Falco |
| 4 | Slippy |
| 5 | Peppy |

Values 2 to 5 follow the order of `TeamId` (`include/sf64player.h`).

1. The host sends `SESSION_ROSTER` to every client that has joined, whenever the roster changes: a client joined, a
   pick was taken, a client left. A new client's first one follows its welcome. Until then a client has no roster.
2. A client sends `SESSION_PICK` with the character it wants, in place of what it holds. A client that has joined holds
   nothing (value 1) until then; the host gives out nothing by itself.
3. A character someone else holds, Fox included, gets `SESSION_PICK_REFUSE` to that client only. The roster stays as it
   was, the client stays connected and may pick again. A free character goes to the client, its old one is free, and
   the roster goes out. A client that picks what it already holds gets no answer, because nothing changed.
4. A client that leaves, for whatever reason, frees its slot and its character at once. Nothing is kept for it: if it
   comes back it is a new client, which may find the character taken.
5. The host orders picks by arrival, so of two picks of one character the first one wins.
6. If the transport refuses a send because its window is full, the host tries again on the next poll, with the roster
   of that moment and the last refusal. A client can miss a roster or a refusal that way, never the last one.

What does not fit, as in the handshake:

- A message out of place is dropped and counted (`CoopSession_Ignored`): a pick before the welcome or from a refused
  client, a roster or a refusal before the welcome, any of the three in the wrong direction.
- `SESSION_PICK` that cannot be read (size other than 1, or a value other than 2 to 5) ends that client as a hello
  that cannot be read would: the host says goodbye and tells its game the client left with `BAD_MESSAGE`. The other
  clients see a roster without it. Its side only sees the host close, because a joined client takes no
  `SESSION_REFUSE`.
- A client ends with `BAD_MESSAGE` on a `SESSION_PICK_REFUSE` of a size other than 1 or a value other than 2 to 5, and
  on a `SESSION_ROSTER` of a size other than 4, without Fox at player 0, with Fox or a value above 5 anywhere else,
  with a wingman twice, or with its own slot free.

Nothing here reads or writes game state. Until WP-17 the character changes nothing in the level. Picks stop at the
level start, see below.

## Level start

The host starts one level for everyone. `SESSION_LEVEL_START` goes on the reliable lane, after the welcome. The code is
in `src/port/coop/session/session.c`; what the values are is in `src/port/coop/game/level_start.c`.

1. The host's game asks for a level with what it holds now. The host turns that down while a client that joined holds
   value 1, or while a client is in the handshake, which ends within 10000 ms. Nothing changes then, and it can ask
   again. So a client never flies with nothing: nobody starts until it has picked or left.
2. When the host takes it, the level, the phase, the values and the roster of that moment are fixed. A client that
   leaves afterwards does not change what the others start from, so every game comes to the same player count.
3. From then on nobody picks and nobody joins, and nothing opens that again yet. A `SESSION_PICK` on its way is dropped
   and counted; the client learns what it holds from the level start. A hello gets `SESSION_REFUSE` with bit 9, whatever
   its identity.
4. The host sends the message to every client that joined, on the next poll, and tries again on the next one while the
   window is full. Its own game hears of it on that poll too. Each game then enters the level from the message, the
   host's as well, so no game goes by its own history.

| Size | Field |
|---|---|
| 1 | Level: a `LevelId` below 20, so not versus |
| 1 | Phase: `gLevelPhase` |
| 4 | Roster, as in Characters |
| 4 v | The values, each a little-endian 32-bit integer, in the order of the table in `level_start.c`: `COOP_LEVEL_START_VALUES`, 86 now |

Each game goes to the level by itself, as soon as it hears: no frame is shared yet, so the games are not in step. That
is WP-22, whose first frame will be what holds them together.

What does not fit, as before:

- A second level start, one before the welcome, and one from a client are dropped and counted (`CoopSession_Ignored`).
- A client ends with `BAD_MESSAGE` on a level start of another size, with a level of 20 or more, with a roster that
  `SESSION_ROSTER` could not hold, or with value 1 anywhere in the roster.

## Id ranges

| Range | Area |
|---|---|
| 0x0000-0x000F | Transport and handshake (hello, version, ping, disconnect) |
| 0x0010-0x003F | Session and lobby |
| 0x0040-0x007F | Sync: inputs, frame acks, state hashes, snapshots |
| 0x0080-0x00BF | Game events that are not part of the sync model (chat, pause requests) |
| 0x00C0-0x00EF | Debug and test harness |
| 0x00F0-0x00FF | Reserved |

## Registry

| Id | Name | Direction | Fields | Added in |
|---|---|---|---|---|
| 0x0000 | `CONNECT_REQUEST` | client to host | none; the connection id field carries the client's nonce | WP-20 |
| 0x0001 | `CONNECT_ACCEPT` | host to client | none | WP-20 |
| 0x0002 | `CONNECT_REFUSE` | host to client | u8 reason: 1 host full, 2 other protocol version | WP-20 |
| 0x0003 | `DISCONNECT` | both | none | WP-20 |
| 0x0004 | `ACK` | both | none; sent when there is no other datagram to carry the ack | WP-20 |
| 0x0005 | `PING` | both | u32 sender's clock in ms | WP-20 |
| 0x0006 | `PONG` | both | u32 the clock value from the ping | WP-20 |
| 0x0010 | `SESSION_HELLO` | client to host | identity | WP-21 |
| 0x0011 | `SESSION_WELCOME` | host to client | u8 player index (1 to 3), then the host's identity | WP-21 |
| 0x0012 | `SESSION_REFUSE` | host to client | u32 reasons: bit 0 build, 1 ROM, 2 CVars, 3 platform (never alone), 8 the hello could not be read, 9 a level has started (WP-24); 0 means no hello in time. Then the host's identity. | WP-21 |
| 0x0013 | `SESSION_PICK` | client to host | u8 character asked for (3 to 5; 2 is refused as taken) | WP-34 |
| 0x0014 | `SESSION_PICK_REFUSE` | host to client | u8 character asked for (2 to 5). The only reason is that someone else holds it. | WP-34 |
| 0x0015 | `SESSION_ROSTER` | host to client | u8 for each of the 4 players, see Characters | WP-34 |
| 0x0016 | `SESSION_LEVEL_START` | host to client | level, phase, roster and values, see Level start | WP-24 |
| 0x00C0 | `TEST_RELIABLE` | both | test pattern, only sent by `coop_net_test` | WP-20 |
| 0x00C1 | `TEST_UNRELIABLE` | both | test pattern, only sent by `coop_net_test` | WP-20 |
| 0x00C2 | `TEST_DONE` | both | none, only sent by `coop_net_test` | WP-20 |
| 0x00C3 | `TEST_SESSION` | both | the text `session test`, only sent by `coop_session_test` | WP-21 |
